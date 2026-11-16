#define _POSIX_C_SOURCE 200809L

#include <bits/time.h>
#include <time.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "samba/samba.h"
#include "systemd/systemd.h"
#include "utils.h"

static void add_ms(struct timespec *t, long ms) {
    t->tv_nsec += ms * 1000000L;
    while (t->tv_nsec >= 1000000000L) {t->tv_nsec -= 1000000000L; t->tv_sec++;}
}

static int before(const struct timespec *a, const struct timespec *b) {
    return a->tv_sec < b->tv_sec || (a->tv_sec == b->tv_sec && a->tv_nsec < b->tv_nsec);
}

void service_watcher(char* filename) {    
    if (get_conf_services_data(filename) != 0)
        perror("Error getting services data\n");

    fflush(stdout);
    
    log_peak_memory();
}

void smtp_watcher(char* filename) {
    printf("sending nothing...\n");
    const char *report_dir = get_reports_dir();
    send_report_smtp(filename, report_dir);

    
}

int main(int argc, char* argv[]) {
    struct timespec now, next_1, next_2;
    clock_gettime(CLOCK_MONOTONIC, &now);
    next_1 = next_2 = now;

    int opt;

    printf("args: \n");
    for (int i = 0; i < argc; i++) {
        printf("- %s\n", argv[i]);
    }

    char* filename = NULL;

    unsigned int watch = 0;
    unsigned int email_watch = 0;

    while ((opt = getopt(argc, argv, "f:s:hw:d:e:")) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
                printf("Found arg f: %s\n", filename);
                break;
            case 'w': {
                char *end;
                errno = 0;
                long seconds = strtol(optarg, &end, 10);

                if (errno == ERANGE || end == optarg || *end != '\0' ||
                    seconds <= 0 || (unsigned long)seconds > UINT_MAX) {
                    fprintf(stderr, "Invalid service watch interval: %s\n", optarg);
                    return 1;
                }

                watch = (unsigned int)seconds;
                break;
            }
            case 'e': {
                char *end;
                errno = 0;
                long seconds = strtol(optarg, &end, 10);

                if (errno == ERANGE || end == optarg || *end != '\0' ||
                    seconds <= 0 || (unsigned long)seconds > UINT_MAX) {
                    fprintf(stderr, "Invalid email watch interval: %s\n", optarg);
                    return 1;
                }

                email_watch = (unsigned int)seconds;
                break;
            }
            case 'h':
                printf("Usage: %s -f <filename>\n", argv[0]);
                return 0;
            case 's':
                return add_service(filename, optarg);
            case 'd':
                return rem_service(filename, optarg);
            
            default:
                continue;
        }
    }

    if (filename == NULL) {
        filename = get_config_path();
    }


    if (watch==0) {
        service_watcher(filename);
        return 0;
    }


    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (!before(&now, &next_1)) {
            service_watcher(filename);
            add_ms(&next_1, (long) watch * 1000);
        }
        if (!before(&now, &next_2)) {
            smtp_watcher(filename);
            add_ms(&next_2, email_watch * 1000);
        }

        const struct timespec *wake = before(&next_1, &next_2) ? &next_1 : &next_2;
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, wake, NULL) == EINTR);
    }

    

    log_peak_memory();

    return 0;
}