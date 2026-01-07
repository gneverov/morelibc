// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "morelib/lwip/socket.h"

#define SOCKET_TLS_FLAG_SERVER_SIDE 1
#define SOCKET_TLS_FLAG_DO_HANDSHAKE_ON_CONNECT 2
#define SOCKET_TLS_FLAG_SUPPRESS_RAGGED_EOFS 4

#ifdef MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
struct socket_tls_ca_cert {
    struct socket_tls_ca_cert *next;
    unsigned char subject_hash[20];
    size_t len;
    unsigned char buf[];
};
#endif

struct socket_tls_context {
    int ref_count;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
#ifdef MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
    struct socket_tls_ca_cert *ca_chain;
    char *capath;
#else
    mbedtls_x509_crt *ca_chain;
#endif
    int *ciphersuites;
#ifdef MBEDTLS_SSL_ALPN
    char **alpn_protocols;
#endif
#ifdef MBEDTLS_ECDH_C
    mbedtls_ecp_group_id curves[2];
#endif
};

struct socket_tls {
    struct socket base;
    struct poll_waiter desc;
    struct mbedtls_ssl_context ssl;
    struct socket *inner;
    struct socket_tls_context *context;
    TickType_t timeout;
    int state;
    int flags;
};

int socket_tls_check_ret(int ret);

struct socket_tls_context *socket_tls_context_alloc(int endpoint);
void socket_tls_context_free(struct socket_tls_context *context);
struct socket_tls_context *socket_tls_context_copy(struct socket_tls_context *context);

int socket_tls_load_cert_chain(struct socket_tls_context *context, const char *certfile, const char *keyfile, const char *password);
int socket_tls_load_verify_locations(struct socket_tls_context *ctx, const char *ca_file, const char *ca_path);

int socket_tls_wrap(int fd, struct socket_tls_context *context, int flags);
struct socket_tls *socket_tls_acquire(int fd);
void socket_tls_release(struct socket_tls *ssl);

int socket_tls_errno(void);

int socket_tls_handshake(struct socket_tls *socket);