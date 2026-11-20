#define _POSIX_C_SOURCE 200809L

#include "reporter/reporter.h"
#include <sys/ucontext.h>

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

int service_watcher(char* filename) {
    if (get_conf_services_data(filename) != 0) {
        fprintf(stderr, "Error getting services data\n");
        return 1;
    }

    if (rem_older_than_conf_range(filename) != 0) {
        fprintf(stderr, "Warning: failed deleting old reports\n");
    }
    

    fflush(stdout);
    
    log_peak_memory();

    return 0;
}

int smtp_watcher(char* filename) {
    printf("sending nothing...\n");
    if (send_mail_report(filename) != 0) {
        fprintf(stderr, "Couldn't send report\n");
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    struct timespec now, next_1, next_2;
    clock_gettime(CLOCK_MONOTONIC, &now);
    next_1 = next_2 = now;
    
    add_ms(&next_1, 1000);
    add_ms(&next_2, 1000);

    int opt;

    char* filename = get_config_path();

    unsigned int watch = 0;
    unsigned int email_watch = 0;

    while ((opt = getopt(argc, argv, "f:s:hw:d:e:m:it")) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
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
                printf("  -e <seconds>  Email interval (mail report frequency)\n");
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
                printf("  Plain run (no args):   Load all intervals from config and run as daemon\n");
                printf("  With -w only:          Use -w value, optionally load email from config\n");
                printf("  With -w and -e:        Use both -w and -e values\n");
                printf("  Other options:         One-time operations (no daemon mode)\n");
                printf("\nExamples:\n");
                printf("  %s                        # Use config defaults, run as daemon\n", argv[0]);
                printf("  %s -w 60                  # Service check every 60 seconds\n", argv[0]);
                printf("  %s -w 3600 -e 86400       # Check every hour, email every 24hrs\n", argv[0]);
                printf("  %s -t                     # Send test mail report (one-time)\n", argv[0]);
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
                if (send_mail_report(filename) != 0) {
                    fprintf(stderr, "Test mail report failed\n");
                    return 1;
                }
                printf("Test mail report completed\n");
                return 0;
            default:
                continue;
        }
    }

    

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    // Load intervals from config if neither -w nor -e were provided via CLI
    if (watch == 0 && email_watch == 0) {
        // Both unspecified - load from config
        cJSON *config = parse_json(filename);
        if (config != NULL) {
            cJSON *properties = cJSON_GetObjectItemCaseSensitive(config, "properties");
            if (properties != NULL) {
                // Load watch interval
                cJSON *watch_interval = cJSON_GetObjectItemCaseSensitive(properties, "watch-interval-sec");
                if (watch_interval != NULL && cJSON_IsNumber(watch_interval)) {
                    watch = (unsigned int)watch_interval->valuedouble;
                }
                
                // Load email interval if send-mail is enabled
                cJSON *send_mail = cJSON_GetObjectItemCaseSensitive(properties, "send-mail");
                if (send_mail != NULL && cJSON_IsBool(send_mail) && cJSON_IsTrue(send_mail)) {
                    cJSON *email_interval = cJSON_GetObjectItemCaseSensitive(properties, "send-mail-interval-hr");
                    if (email_interval != NULL && cJSON_IsNumber(email_interval)) {
                        email_watch = (unsigned int)(email_interval->valuedouble * 3600);
                    }
                }
            }
            cJSON_Delete(config);
        }
    } else if (watch > 0 && email_watch == 0) {
        // Only -w specified, optionally load email from config if enabled
        cJSON *config = parse_json(filename);
        if (config != NULL) {
            cJSON *properties = cJSON_GetObjectItemCaseSensitive(config, "properties");
            if (properties != NULL) {
                cJSON *send_mail = cJSON_GetObjectItemCaseSensitive(properties, "send-mail");
                if (send_mail != NULL && cJSON_IsBool(send_mail) && cJSON_IsTrue(send_mail)) {
                    cJSON *email_interval = cJSON_GetObjectItemCaseSensitive(properties, "send-mail-interval-hr");
                    if (email_interval != NULL && cJSON_IsNumber(email_interval)) {
                        email_watch = (unsigned int)(email_interval->valuedouble * 3600);
                    }
                }
            }
            cJSON_Delete(config);
        }
    }

    if (watch==0) {
        return service_watcher(filename);
    }

    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (!before(&now, &next_1)) {
            if (service_watcher(filename) != 0) {
                fprintf(stderr, "Error in service watcher\n");
            }
            add_ms(&next_1, (long) watch * 1000);
        }
        if (email_watch > 0 && !before(&now, &next_2)) {
            if(smtp_watcher(filename) != 0) {
                fprintf(stderr, "Error in smtp watcher\n");
            }
            add_ms(&next_2, (long) email_watch * 1000);
        }

        // Select wake time: use only next_1 if email is disabled
        const struct timespec *wake;
        if (email_watch == 0) {
            wake = &next_1;
        } else {
            wake = before(&next_1, &next_2) ? &next_1 : &next_2;
        }
        
        int ret;
        do {
            ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, wake, NULL);
        } while (ret == EINTR);
    }

    

    log_peak_memory();

    return 0;
}