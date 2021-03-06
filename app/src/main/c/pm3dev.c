#include "pm3dev.h"

#include <malloc.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

#include "pm3uart.h"
#include "pm3util.h"
#include "pm3relayd.h"
#include "glue.h"

enum { DEVTYPE_INVALID, DEVTYPE_DIRECT, DEVTYPE_RELAYED, DEVTYPE_TEST }
        pm3dev_type = DEVTYPE_INVALID;
int pm3dev_fd = -1;
int pm3dev_relayd_outfd = -1;
int pm3dev_relayd_child_stdoutfd = -1;
pid_t pm3dev_relayd_pid = -1;
char *pm3dev_relayd_path = NULL;
int *pm3dev_thread_quit = NULL;

int pm3dev_relay_run(const char *devpath) {
    if (!pm3dev_relayd_path) {
        fprintf(stderr, "pm3dev_relay_run: relayd_path unset\n");
        return -1;
    }

    if (pm3dev_relayd_child_stdoutfd == -1) {
        fprintf(stderr, "pm3dev_relay_run: relayd_child_stdoutfd unset\n");
        return -1;
    }

    int outp[2];
    if (pipe(outp) == -1) {
        perror("pm3dev_relay_run: pipe(outp)");
        return -1;
    }

    char sharg[1024] = {0};
    if (snprintf(sharg, sizeof(sharg), "exec %s %s", pm3dev_relayd_path, devpath) > sizeof(sharg)) {
        fprintf(stderr, "pm3dev_relay_run: pm3dev_relay_run: snprintf buffer too small\n");
        close(outp[0]);
        close(outp[1]);
        return -1;
    }

    const char *const argv[] = {"su", "root", "sh", "-c", sharg, 0};
    fprintf(stderr, "pm3dev_relay_run: pm3dev_relay_run: %s\n", sharg);
    pid_t pid = fork();
    if (pid == 0) {
        if (dup2(outp[0], STDIN_FILENO) != STDIN_FILENO ||
                dup2(pm3dev_relayd_child_stdoutfd, STDOUT_FILENO) != STDOUT_FILENO) {
            perror("pm3dev_relay_run: pm3dev_relay_run: dup2");
            _exit(-1);
        }
        execvp("su", (char *const *) argv);
        _exit(-1);
    } else if (pid > 0) {
        close(outp[0]);
        pm3dev_relayd_pid = pid;
        pm3dev_relayd_outfd = outp[1];
        pm3dev_type = DEVTYPE_RELAYED;
    } else {
        perror("pm3dev_relay_run: vfork");
        close(outp[0]);
        close(outp[1]);
        return -1;
    }

    return 0;
}

void pm3dev_relay_shutdown(void) {
    enum pm3relayd_cmd cmd = RELAYDCMD_EXIT;
    if (pm3util_write(pm3dev_relayd_outfd, &cmd, sizeof(cmd)) == -1) {
        perror("pm3dev_relay_shutdown: write(outfd) for RELAYDCMD_EXIT");
    }
}

int pm3dev_relay_send(const uint8_t *bytes, const size_t size) {
    enum pm3relayd_cmd cmd = RELAYDCMD_SEND;
    if (pm3util_write(pm3dev_relayd_outfd, &cmd, sizeof(cmd)) == -1) {
        perror("pm3dev_relay_send: write(outfd) for RELAYDCMD_SEND");
        return -1;
    }
    if (pm3util_write(pm3dev_relayd_outfd, &size, sizeof(size)) == -1) {
        perror("pm3dev_relay_send: write(outfd) for RELAYDCMD_SEND size");
        return -1;
    }
    if (pm3util_write(pm3dev_relayd_outfd, bytes, size) == -1) {
        perror("pm3dev_relay_send: write(outfd) for RELAYDCMD_SEND data");
        return -1;
    }
    return 0;
}

int pm3dev_relay_thread(void) {
    size_t cmdsize = pm3glue_usbcmd_size();
    void *buf = malloc(cmdsize);
    if (!buf) {
        perror("pm3dev_relay_thread: malloc");
    }

    int inp[2];
    if (pipe(inp) == -1) {
        perror("pm3dev_relay_thread: pipe(inp)");
        return -1;
    }
    pm3dev_relayd_child_stdoutfd = inp[1];

    while (1) {
        if (pm3util_read(inp[0], buf, cmdsize) == -1) {
            perror("pm3dev_relay_thread: read");
            return -1;
        }
        pm3glue_recv_cmd(buf);
    }
}

int pm3dev_change(const char *newpath) {
    switch (pm3dev_type) {
        case DEVTYPE_DIRECT:
            pm3dev_type = DEVTYPE_INVALID;
            close(pm3dev_fd);
            pm3dev_fd = -1;
            break;
        case DEVTYPE_RELAYED:
            pm3dev_type = DEVTYPE_INVALID;
            pm3dev_relay_shutdown();
            break;
        case DEVTYPE_TEST:
        case DEVTYPE_INVALID:
            break;
    }

    if (*newpath) {
        /* TODO: implement direct reading (need to spawn a thread to read from ttyACM
         * TODO: and call UsbCommandReceived)
         */
#if 0
        int newfd = pm3uart_open(newpath);
        if (newfd == -1) {
#endif
        return pm3dev_relay_run(newpath);
#if 0
        } else {
            pm3dev_fd = newfd;
            pm3dev_type = DEVTYPE_DIRECT;
            return 0;
        }
#endif
    } else {
        pm3dev_type = DEVTYPE_TEST;
        return 0;
    }
}

int pm3dev_write(const uint8_t *bytes, const size_t size) {
    switch (pm3dev_type) {
        case DEVTYPE_TEST: {
            printf("USB write: ");
            for (size_t i = 0; i < size; i++) {
                printf("%02X", bytes[i]);
            }
            printf("\n");
            break;
        }
        case DEVTYPE_INVALID:
            fprintf(stderr, "pm3dev_write: pm3dev_type is DEVTYPE_INVALID\n");
            return -1;
        case DEVTYPE_DIRECT:
            if (pm3util_write(pm3dev_fd, bytes, size) == -1) {
                perror("pm3dev: write(pm3dev_fd)");
                return -1;
            }
        case DEVTYPE_RELAYED:
            return pm3dev_relay_send(bytes, size);
    }

    return 0;
}
