#include <sys/ucontext.h>
#define _POSIX_C_SOURCE 200809L

#include <bits/time.h>
#include <time.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <signal.h>

#include "samba/samba.h"
#include "systemd/systemd.h"
#include "utils.h"
#include "smtp/smtp.h"

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
    // send_report_smtp(filename, report_dir);
    if (send_mail_report(filename) != 0) {
        fprintf(stderr, "Couldn't send report\n");
    }
}

int main(int argc, char* argv[]) {
    struct timespec now, next_1, next_2;
    clock_gettime(CLOCK_MONOTONIC, &now);
    next_1 = next_2 = now;
    
    add_ms(&next_1, 1000);
    add_ms(&next_2, 1000);

    int opt;

    printf("args: \n");
    for (int i = 0; i < argc; i++) {
        printf("- %s\n", argv[i]);
    }

    char* filename = get_config_path();

    unsigned int watch = 0;
    unsigned int email_watch = 0;

    while ((opt = getopt(argc, argv, "f:s:hw:d:e:m:it")) != -1) {
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
                printf("Usage: %s [options]\n", argv[0]);
                printf("  -f <file>     Config file path\n");
                printf("  -w <seconds>  Watch interval (service check frequency)\n");
                printf("                Enables continuous monitoring mode\n");
                printf("  -e <seconds>  Email interval (mail report frequency)\n");
                printf("                Only active if -w is also specified\n");
                printf("  -s <service>  Add service to watch list\n");
                printf("  -d <service>  Remove service from watch list\n");
                printf("  -m <email>    Add mail recipient\n");
                printf("  -i            SMTP configuration wizard\n");
                printf("  -t            Send test mail report\n");
                printf("  -h            Show this help\n");
                printf("\nConfig defaults (from properties in config.json):\n");
                printf("  watch-interval-sec:    Service check interval in seconds\n");
                printf("  send-mail-interval-hr: Email report interval in hours\n");
                printf("  send-mail:             Enable email reports (1=enabled, 0=disabled)\n");
                printf("\nBehavior:\n");
                printf("  Plain run (no args): Service check once and exit\n");
                printf("  With -w only:        Continuous service monitoring\n");
                printf("  With -w and -e:      Service monitoring + periodic email reports\n");
                printf("\nExamples:\n");
                printf("  %s                        # Service check once and exit\n", argv[0]);
                printf("  %s -w 60                  # Service check every 60 seconds\n", argv[0]);
                printf("  %s -w 3600 -e 86400       # Check every hour, email every 24hrs\n", argv[0]);
                return 0;
            case 's':
                return add_service(filename, optarg);
            case 'd':
                return rem_service(filename, optarg);
            case 'm':
                return add_mail_recipient(filename, optarg);    
            case 'i':
                return smtp_wizard(filename);
            case 't':
                return send_mail_report(filename);        
            default:
                continue;
        }
    }

    

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    // If watch interval was provided via CLI and is non-zero, optionally load email interval default
    // Otherwise, plain run = service check once and exit (no defaults loaded)
    if (watch > 0 && email_watch == 0) {
        cJSON *config = parse_json(filename);
        if (config != NULL) {
            cJSON *properties = cJSON_GetObjectItemCaseSensitive(config, "properties");
            if (properties != NULL) {
                // Load email interval default if -e wasn't specified but -w was
                cJSON *email_interval = cJSON_GetObjectItemCaseSensitive(properties, "send-mail-interval-hr");
                if (email_interval != NULL && cJSON_IsNumber(email_interval)) {
                    // Only load if send-mail property is enabled
                    cJSON *send_mail = cJSON_GetObjectItemCaseSensitive(properties, "send-mail");
                    if (send_mail != NULL && cJSON_IsBool(send_mail) && cJSON_IsTrue(send_mail)) {
                        email_watch = (unsigned int)(email_interval->valuedouble * 3600);
                    }
                }
            }
            cJSON_Delete(config);
        }
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
        if (email_watch > 0 && !before(&now, &next_2)) {
            smtp_watcher(filename);
            add_ms(&next_2, (long) email_watch * 1000);
        }

        const struct timespec *wake = before(&next_1, &next_2) ? &next_1 : &next_2;
        int ret;
        do {
            ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, wake, NULL);
        } while (ret == EINTR);
        
        if (ret != 0) {
            fprintf(stderr, "clock_nanosleep error: %d\n", ret);
        }
    }

    

    log_peak_memory();

    return 0;
}