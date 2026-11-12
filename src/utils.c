#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "../third_party/cjson/cJSON.h"

cJSON* parse_json(char file_path[256]) {
    FILE *file = fopen(file_path, "rb");
    if(file == NULL) {
        perror("Error opening config\n");
        printf("file_path: %s\n", file_path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);

    printf("Length of file: %ld\n", length);

    char *buffer_config = malloc(length + 1);
    if (buffer_config == NULL) {
        perror("Memory allocation failed for some reason\n");
        return NULL;
    }

    
    
    rewind(file);

    
    size_t read_size = fread(buffer_config, 1, length, file);
    buffer_config[read_size] = '\0';

    printf("Contents of config:\n---\n%s\n---\n", buffer_config);
    
    cJSON *json = cJSON_Parse(buffer_config);

    free(buffer_config);
    fclose(file);

    return json;

}

int write_json(char file_path[256], cJSON* json) {
    
    char*buff = cJSON_Print(json);
    if (buff == NULL) {
        fprintf(stderr, "Failed to buffer config\n");
        return 1;
    }

    FILE * file = fopen(file_path, "w"); 
    if (file == NULL) {
        perror("Failed to open config");
        cJSON_free(buff);
        return 1;
    }

    int result = 0;
    if (fputs(buff, file) == EOF) {
        perror("Failed to write config");
        result = 1;
    }
    if (fclose(file)==EOF) {
        perror("Failed to close config");
        result = 1;
    }

    cJSON_free(buff);
    return result;
}

char ** parse_services(cJSON* json) {
    size_t service_count = 0;

    if (!cJSON_IsArray(json)) return NULL;
    int item_count = cJSON_GetArraySize(json);

    char ** services = calloc((size_t)item_count, sizeof *services);

    cJSON *service;
    cJSON_ArrayForEach(service, json) {
        if(!cJSON_IsString(service) || service->valuestring == NULL) continue;
        size_t length = strlen(service->valuestring) + 1;
        services[service_count] = malloc(length);

        if(services[service_count] == 0) {
            for (size_t i = 0; i < service_count; i++) free(services[i]);
            free(services);
            service_count=0;
            return NULL;
        }
        printf("%s\n", service->valuestring);
        
        memcpy(services[service_count], service->valuestring, length);
        (service_count)++;
        
    }

    return services;
}


char ** parse_procs_array(cJSON* json_procs, size_t *proc_count) {
    *proc_count=0;

    if(!cJSON_IsArray(json_procs)) return NULL;

    int item_count = cJSON_GetArraySize(json_procs);
    char **procs = calloc((size_t)item_count, sizeof *procs);

    if (item_count > 0 && procs == NULL) return NULL;

    
    cJSON *proc;
    cJSON_ArrayForEach(proc, json_procs) {
            
        if (!cJSON_IsString(proc) || proc->valuestring == NULL) continue;
        
        size_t length = strlen(proc->valuestring) + 1;
        procs[*proc_count] = malloc(length);

        if(procs[*proc_count] == 0) {
            for (size_t i = 0; i < *proc_count; i++) free(procs[i]);
            free(procs);
            *proc_count=0;
            return NULL;
        }
        printf("%s\n", proc->valuestring);
        
        memcpy(procs[*proc_count], proc->valuestring, length);
        (*proc_count)++;
        
    }
    return procs;
}

int add_service(char *file_path, char *service)
{
    cJSON *json = parse_json(file_path);
    if (json == NULL) {
        fprintf(stderr, "Could not parse configuration file: %s\n", file_path);
        return 1;
    }

    int result = 1;
    cJSON *services = cJSON_GetObjectItemCaseSensitive(json, "services");

    if (!cJSON_IsArray(services)) {
        fprintf(stderr, "Could not parse services: %s\n", file_path);
        goto cleanup;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, services) {
        if (cJSON_IsString(item) && item->valuestring != NULL &&
            strcmp(item->valuestring, service) == 0) {
            printf("Service %s is already in the config\n", service);
            result = 0;
            goto cleanup;
        }
    }

    cJSON *new_service = cJSON_CreateString(service);
    if (new_service == NULL) {
        fprintf(stderr, "Failed to create service entry\n");
        goto cleanup;
    }

    if (!cJSON_AddItemToArray(services, new_service)) {
        fprintf(stderr, "Failed to add service entry\n");
        cJSON_Delete(new_service);
        goto cleanup;
    }

    result = write_json(file_path, json);

cleanup:
    cJSON_Delete(json);
    return result;
}
