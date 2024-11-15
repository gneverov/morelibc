#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "newlib/newlib.h"

#include "FreeRTOS.h"
#include "task.h"


static void init_task(void *params) {
    // Mount devfs filesystem.
    mount(NULL, "/dev", "devfs", 1, NULL);

    // Open uart serial as controlling tty.
    int fd = open("/dev/ttyS0", O_RDWR, 0);
    // Successful open of tty installs it on stdio fds, so we an close our fd.
    close(fd);

    // Print the current time every second.
    for (;;) {
        time_t now = time(NULL);
        const char* time = ctime(&now);
        fputs(time, stdout);
        sleep(1);
    }

    vTaskDelete(NULL);
}

int main(int argc, char **argv) {
    xTaskCreate(init_task, "init", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    vTaskStartScheduler();
    return 1;
}
