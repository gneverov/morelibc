// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include "morelib/poll.h"
#include "morelib/thread.h"

#include "FreeRTOS.h"
#include "task.h"

#include "mbedtls/asn1write.h"
#include "mbedtls/debug.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "mbedtls/sha1.h"
#include "mbedtls/ssl_internal.h"

#include "morelib/lwip/tls.h"


static mbedtls_entropy_context socket_tls_entropy;

__attribute__((constructor))
void socket_tls_init() {
#ifdef MBEDTLS_DEBUG_C
    mbedtls_debug_set_threshold(1);
#endif
    mbedtls_entropy_init(&socket_tls_entropy);
}

static void socket_tls_init_debug(void *ctx, int level, const char *file, int line, const char *str) {
    thread_t *thread = thread_current();
    fprintf(stderr, "[%lu] %s:%04d: %s", thread ? thread->id : 0, file, line, str);
    // fflush(stderr);
}

static __thread int _socket_tls_errno;

int socket_tls_check_ret(int ret) {
    if (ret >= 0) {
        _socket_tls_errno = 0;
        return ret;
    }
    else if (ret > -256) {
        _socket_tls_errno = 0;
        errno = -ret;
        return -1;
    }
    else {
        _socket_tls_errno = ret;
        errno = EPROTO;
        return -1;
    }
}

int socket_tls_errno(void) {
    return _socket_tls_errno;
}

static int socket_tls_x509_crt_init(mbedtls_x509_crt **crt) {
    if (*crt) {
        return 0;
    }
    *crt = calloc(1, sizeof(mbedtls_x509_crt));
    if (!*crt) {
        return MBEDTLS_ERR_X509_ALLOC_FAILED;
    }
    mbedtls_x509_crt_init(*crt);
    return 0;
}

static void socket_tls_x509_crt_free(mbedtls_x509_crt **crt) {
    mbedtls_x509_crt_free(*crt);
    free(*crt);
    *crt = NULL;
}

static int socket_tls_ca_cb(void *p_ctx, mbedtls_x509_crt const *child, mbedtls_x509_crt **candidate_cas);

struct socket_tls_context *socket_tls_context_alloc(int endpoint) {
    // Allocate the SSL context
    struct socket_tls_context *context = calloc(1, sizeof(struct socket_tls_context));
    if (!context) {
        return NULL;
    }
    context->ref_count = 1;
    mbedtls_ssl_config_init(&context->conf);
    mbedtls_ctr_drbg_init(&context->ctr_drbg);

    // Seed the random number generator
    if (socket_tls_check_ret(mbedtls_ctr_drbg_seed(&context->ctr_drbg, mbedtls_entropy_func, &socket_tls_entropy, NULL, 0)) < 0) {
        goto exit;
    }

    // Load default configurations
    if (socket_tls_check_ret(mbedtls_ssl_config_defaults(&context->conf, endpoint, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) < 0) {
        goto exit;
    }    

    // Set the SSL context configuration
    mbedtls_ssl_conf_rng(&context->conf, mbedtls_ctr_drbg_random, &context->ctr_drbg);
    mbedtls_ssl_conf_dbg(&context->conf, socket_tls_init_debug, NULL);
#ifdef MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
    mbedtls_ssl_conf_ca_cb(&context->conf, socket_tls_ca_cb, context);
#endif
#ifdef MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
    if (mbedtls_ssl_conf_max_frag_len(&context->conf, MBEDTLS_SSL_MAX_FRAG_LEN_512) < 0) {
        goto exit;
    }
#endif
    return context;

exit:
    socket_tls_context_free(context);
    return NULL;
}

void socket_tls_context_free(struct socket_tls_context *context) {
    taskENTER_CRITICAL();
    int ref_count = --context->ref_count;
    taskEXIT_CRITICAL();
    if (ref_count == 0) {
        mbedtls_ssl_key_cert *key_cert = context->conf.key_cert;
        while (key_cert) {
            socket_tls_x509_crt_free(&key_cert->cert);
            mbedtls_pk_free(key_cert->key);
            free(key_cert->key);
            key_cert->key = NULL;
            key_cert = key_cert->next;
        }

        mbedtls_ssl_config_free(&context->conf);
        mbedtls_ctr_drbg_free(&context->ctr_drbg);
        #ifdef MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
        while (context->ca_chain) {
            struct socket_tls_ca_cert *next = context->ca_chain->next;
            free(context->ca_chain);
            context->ca_chain = next;
        }
        free(context->capath);
        #else
        socket_tls_x509_crt_free(&context->ca_chain);
        #endif
        free(context->ciphersuites);
        #ifdef MBEDTLS_SSL_ALPN
        free(context->alpn_protocols);
        #endif
        free(context);
    }
}

struct socket_tls_context *socket_tls_context_copy(struct socket_tls_context *context) {
    taskENTER_CRITICAL();
    context->ref_count++;
    taskEXIT_CRITICAL();
    return context; 
}

#ifdef MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
static int socket_tls_name_hash(const mbedtls_x509_buf *name_raw, unsigned char hash[20]) {
    size_t len = name_raw->len;
    unsigned char *buf = calloc(1, len);
    if (!buf) {
        return MBEDTLS_ERR_X509_ALLOC_FAILED;
    }
    memcpy(buf, name_raw->p, len);

    unsigned char *p = buf;
    int ret = mbedtls_asn1_get_tag(&p, p + len, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
    if (ret) {
        goto cleanup;
    }

    unsigned char *begin = p;
    unsigned char *end = p + len;
    while (p < end) {
        unsigned char *tag = p++;
        ret = mbedtls_asn1_get_len(&p, end, &len);
        if (ret) {
            goto cleanup;
        }
        if (*tag & MBEDTLS_ASN1_CONSTRUCTED) {
            continue;
        }
        if (MBEDTLS_ASN1_IS_STRING_TAG(*tag)) {
            *tag = MBEDTLS_ASN1_UTF8_STRING;
            for (size_t i = 0; i < len; i++) {
                p[i] = tolower(p[i]);
            }
        }
        p += len;
    }

    mbedtls_sha1_context sha1;
    mbedtls_sha1_init(&sha1);
    mbedtls_sha1_starts_ret(&sha1);
    mbedtls_sha1_update_ret(&sha1, begin, end - begin);
    mbedtls_sha1_finish_ret(&sha1, hash);

cleanup:
    free(buf);
    return ret;
}

static int socket_tls_find_ca(struct socket_tls_context *context, unsigned char issuer_hash[20], mbedtls_x509_crt **candidate_cas) {
    mbedtls_x509_crt *ca_chain = NULL;
    int num_certs = 0;
    int ret = 0;
    struct socket_tls_ca_cert *ca_cert = context->ca_chain;
    while (ca_cert) {
        if (memcmp(ca_cert->subject_hash, issuer_hash, 20) == 0) {
            ret = socket_tls_x509_crt_init(&ca_chain);
            if (ret) {
                goto cleanup;
            }
            ret = mbedtls_x509_crt_parse_der_nocopy(ca_chain, ca_cert->buf, ca_cert->len);
            if (ret) {
                goto cleanup;
            }            
            num_certs++;
        }
        ca_cert = ca_cert->next;
    }
    if (num_certs) {
        *candidate_cas = ca_chain;
        ca_chain = NULL;
        ret = 0;
    }
    else {
        ret = MBEDTLS_ERR_SSL_PEER_VERIFY_FAILED;
    }

cleanup:
    socket_tls_x509_crt_free(&ca_chain);
    return ret;    
}

static int socket_tls_crt_parse_file(struct socket_tls_context *context, const char *path) {
    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);
    int ret = mbedtls_x509_crt_parse_file(&chain, path);
    if (ret != 0) {
        goto cleanup;
    }

    struct socket_tls_ca_cert **tail = &context->ca_chain;
    while (*tail) {
        tail = &(*tail)->next;
    }

    mbedtls_x509_crt *crt = &chain;
    int num_certs = 0;
    while (crt && crt->version) {
        struct socket_tls_ca_cert *ca_cert = calloc(1, sizeof(struct socket_tls_ca_cert) + crt->raw.len);
        if (!ca_cert) {
            ret = MBEDTLS_ERR_X509_ALLOC_FAILED;
            goto cleanup;
        }

        memcpy(ca_cert->buf, crt->raw.p, crt->raw.len);
        ca_cert->len = crt->raw.len;

        ret = socket_tls_name_hash(&crt->subject_raw, ca_cert->subject_hash);
        if (ret != 0) {
            free(ca_cert);            
            goto cleanup;
        }

        *tail = ca_cert;
        tail = &ca_cert->next;
        crt = crt->next;
        num_certs++;
    }
    ret = 0;

cleanup:
    mbedtls_x509_crt_free(&chain);
    return ret;
}

static int socket_tls_ca_cb(void *p_ctx, mbedtls_x509_crt const *child, mbedtls_x509_crt **candidate_cas) {
    struct socket_tls_context *context = p_ctx;

    unsigned char issuer_hash[20];
    int ret = socket_tls_name_hash(&child->issuer_raw, issuer_hash);
    if (ret) {
        return ret;
    }

    ret = socket_tls_find_ca(context, issuer_hash, candidate_cas);
    if (ret != MBEDTLS_ERR_SSL_PEER_VERIFY_FAILED) {
        return ret;
    }

    char path[MBEDTLS_X509_MAX_FILE_PATH_LEN];
    int i = 0;
    int num_certs = 0;
    while (i < 10) {
        ret = snprintf(path, sizeof(path), "%s/%08lx.%d", context->capath, *(uint32_t *)issuer_hash, i++);
        if ((ret < 0) || (ret >= sizeof(path))) {
            return MBEDTLS_ERR_X509_BUFFER_TOO_SMALL;
        }

        struct stat sb;
        if (stat(path, &sb) < 0) {
            break;
        }

        ret = socket_tls_crt_parse_file(context, path);
        if (ret == 0) {
            num_certs++;
        }
    }

    if (num_certs) {
        return socket_tls_find_ca(context, issuer_hash, candidate_cas);
    }
    else {
        return MBEDTLS_ERR_SSL_PEER_VERIFY_FAILED;
    }
}
#endif

static int socket_tls_load_verify_file(struct socket_tls_context *context, const char *cafile) {
#ifdef MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
    int ret = socket_tls_crt_parse_file(context, cafile);
#else
    socket_tls_x509_crt_init(&context->ca_chain);
    int ret = mbedtls_x509_crt_parse_file(context->ca_chain, cafile);
    if (ret >= 0) {
        mbedtls_ssl_conf_ca_chain(&context->conf, context->ca_chain, NULL);
    }
#endif
    return socket_tls_check_ret(ret);
}

static int socket_tls_load_verify_dir(struct socket_tls_context *context, const char *capath) {
#ifdef MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
    DIR *dir = opendir(capath);
    if (!dir) {
        return -1;
    }
    free(context->capath);
    context->capath = strdup(capath);

    int ret = 0;
#ifndef NDEBUG 
    // Check that all files in capath hash correctly.
    mbedtls_sha1_context sha1;
    mbedtls_sha1_init(&sha1);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        char *endptr;
        uint32_t hash = strtoul(entry->d_name, &endptr, 16);
        if ((endptr != entry->d_name + 8) || (*endptr != '.')) {
            continue;
        }

        char path[MBEDTLS_X509_MAX_FILE_PATH_LEN];
        int snp_ret = snprintf(path, sizeof(path), "%s/%s", capath, entry->d_name);
        if (snp_ret < 0 || snp_ret >= sizeof(path)) {
            ret = MBEDTLS_ERR_X509_BUFFER_TOO_SMALL;
            goto cleanup;
        }

        mbedtls_x509_crt cert;
        mbedtls_x509_crt_init(&cert);
        if (mbedtls_x509_crt_parse_file(&cert, path) < 0) {
            printf("could not parse file %s\n", entry->d_name);
            mbedtls_x509_crt_free(&cert);
            continue;
        }

        unsigned char subject_hash[20];
        ret = socket_tls_name_hash(&cert.subject_raw, subject_hash);
        mbedtls_x509_crt_free(&cert);
        if (ret != 0) {
            goto cleanup;
        }
 
        uint32_t computed_hash = *(uint32_t *)subject_hash;
        if (computed_hash != hash) {
            printf("computed hash %08lx for file %s\n", computed_hash, entry->d_name);
        }
    }
cleanup:
#endif

    closedir(dir);
    return socket_tls_check_ret(ret);
#else
    errno = ENOSYS;
    return -1;
#endif
}

int socket_tls_load_verify_locations(struct socket_tls_context *context, const char *cafile, const char *capath) {
    int ret = -1;
    if (cafile) {
        ret = socket_tls_load_verify_file(context, cafile);
    }
    if (capath) {
        ret = socket_tls_load_verify_dir(context, capath);
    }
    return ret;
}

int socket_tls_load_cert_chain(struct socket_tls_context *context, const char *certfile, const char *keyfile, const char *password) {
    mbedtls_x509_crt *own_cert = NULL;
    mbedtls_pk_context *pk_key = NULL;
    int ret = socket_tls_x509_crt_init(&own_cert);
    if (ret != 0) {
        goto cleanup;
    }

    ret = mbedtls_x509_crt_parse_file(own_cert, certfile);
    if (ret != 0) {
        goto cleanup;
    }

    pk_key = calloc(1, sizeof(mbedtls_pk_context));
    if (!pk_key) {
        ret = MBEDTLS_ERR_X509_ALLOC_FAILED;
        goto cleanup;
    }
    mbedtls_pk_init(pk_key);

    ret = mbedtls_pk_parse_keyfile(pk_key, keyfile, password);
    if (ret != 0) {
        goto cleanup;
    }

    ret = mbedtls_ssl_conf_own_cert(&context->conf, own_cert, pk_key);
    if (ret != 0) {
        goto cleanup;
    }

    own_cert = NULL;
    pk_key = NULL;

cleanup:
    socket_tls_x509_crt_free(&own_cert);
    mbedtls_pk_init(pk_key);
    free(pk_key);
    return socket_tls_check_ret(ret);
}