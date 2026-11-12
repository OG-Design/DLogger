#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include "../../third_party/cjson/cJSON.h"
#include "../utils.h"

void attach_samba_procs() {

}

pid_t get_pid(const char *process_name) {
    DIR *dir;
    struct dirent *entry;
    pid_t pid = -1;
    
    dir = opendir("/proc");
    if (dir == NULL) {
        perror("Failed to open proc");
        return -1;
    }

    while((entry = readdir(dir)) != NULL) {
        int is_pid = 1;
        for (int i = 0; entry -> d_name[i] != '\0'; i++) {
            if(!isdigit((unsigned char)entry->d_name[i])) {
                is_pid = 0;
                break;
            }
        }

        if (is_pid) {
            char comm_path[256];
            char current_name[256];

            FILE *comm_file;

            snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);
            comm_file = fopen(comm_path, "r");
            if (comm_file) {
                if (fgets(current_name, sizeof(current_name), comm_file)) {
                    current_name[strcspn(current_name, "\n")] = '\0';

                    if (strcmp(current_name, process_name) == 0) {
                        pid = strtol(entry->d_name, NULL, 10);
                        fclose(comm_file);
                        break;
                    }
                }
                fclose(comm_file);
            }

        }
    }

    closedir(dir);
    return pid;

}

int samba() {
    
    printf("Running samba logger\n");
    char fp [256] ="./data/config.json";
    cJSON *json = parse_json(fp);

    cJSON *procs = cJSON_GetObjectItemCaseSensitive(json, "procs");

    size_t len = 1000;
    char ** parsed_procs = parse_procs_array(procs, &len);

    size_t procs_len = len;


    for (size_t i = 0; i < procs_len; i++) {
        printf("%s\n---\n", parsed_procs[i]);
    }
    
    if(cJSON_IsArray(procs)) {
        cJSON *proc;
        cJSON_ArrayForEach(proc, procs) {
            if (cJSON_IsString(proc) && proc->valuestring != NULL) {
                printf("%s\n", proc->valuestring);
            }
        }
    }

    pid_t pid = get_pid("systemd");

    if(pid != -1) {
        printf("pid: %d\n", pid);
    } else {
        printf("Proccess not found\n");
    }

    cJSON_Delete(json);
    
    return 0;
}