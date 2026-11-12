#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "samba/samba.h"
#include "systemd/systemd.h"
#include "utils.h"


int main(int argc, char* argv[]) {
    // struct Service_List service = list_service();
    // if (service.count == 0) {
    //     printf("Error in list_service: check for sudo\n");
    //     return -1;
    // }
    
    int opt;

    printf("args: \n");
    for (int i = 0; i < argc; i++) {
        printf("- %s\n", argv[i]);
    }

    char* filename = "./data/config.json";

    unsigned int watch = 0;

    while ((opt = getopt(argc, argv, "f:s:hw:")) != -1) {
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
                    fprintf(stderr, "Invalid watch interval: %s\n", optarg);
                    return 1;
                }

                watch = (unsigned int)seconds;
                break;
            }
            case 'h':
                printf("Usage: %s -f <filename>\n", argv[0]);
                return 0;
            case 's':
                return add_service(filename, optarg);
            
            default:
                filename = "./data/config.json";
                continue;
        }
    }
    
    do {
        if (get_conf_services_data(filename) != 0)
            return 1;

        fflush(stdout);
        if (watch > 0)
            sleep(watch);
    } while (watch > 0);

    int services = get_conf_services_data(filename);
    

    // int samba_result = samba();

    // if (samba_result != 0) {
    //     return samba_result;
    // }
    end:
    return 0;
}