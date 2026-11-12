#include <asm-generic/errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
// #include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus-protocol.h>
#include <systemd/sd-bus.h>
#include <errno.h>
#include <time.h>

#include "../utils.h"

struct Service_Data {
    char* name; 
    char* sub_state;
    uint32_t main_pid;
};

struct Service_List {
    struct Service_Data *services;
    int count;
};

struct Service_List list_service() {
    
    struct Service_List result = {
        .services = NULL,
        .count = 0,
    };

    // printf("Running list_service\n");

    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(r));
        
        return result;
    }

    r = sd_bus_call_method(
        bus, 
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", 
        "ListUnits", 
        &error, 
        &reply, 
        ""
    );

    if (r < 0) {
        fprintf(stderr, "Failed to issue method call: %s\n", error.message);
        return result;
    }

    r = sd_bus_message_enter_container(reply, 'a', "(ssssssouso)");
    if (r < 0) return result;

    const char *name, *desc, *load_state, *active_state, *sub_state, *following, *unit_path, *job_type, *job_path;
    uint32_t job_id;

    int capacity = 10;
    result.services = malloc(sizeof(struct Service_Data) * capacity);

    while ((r = sd_bus_message_read(reply, "(ssssssouso)",
            &name, &desc, &load_state, &active_state, 
            &sub_state, &following, &unit_path, &job_id, 
            &job_type, &job_path
    )) > 0) {

        if (strcmp(active_state, "active") == 0 && strstr(name, ".service") != NULL) {
            uint32_t main_pid = 0;



            sd_bus_get_property_trivial(bus, "org.freedesktop.systemd1", unit_path, "org.free.systemd1.Service", "MainPID", NULL, 'u', &main_pid);

            if (result.count >= capacity) {
                capacity *= 2;
                result.services = realloc(result.services, sizeof(struct Service_Data) * capacity);
            }

            result.services[result.count].name=strdup(name);
            result.services[result.count].sub_state=strdup(sub_state);
            result.services[result.count].main_pid=main_pid;
            result.count++;

            // printf("Unit %-35s | State: %-10s | MainPID: %u\n", name, sub_state, main_pid);

            

        }
    }

    sd_bus_message_exit_container(reply);
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);

    return result;
}


int get_service_states(char* services[], int count) {
    sd_bus* bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int result = 0;
    int r = sd_bus_open_system(&bus);

    if(r < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", error.message);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        r = sd_bus_call_method(bus,
            "org.freedesktop.systemd1", 
            "/org/freedesktop/systemd1", 
            "org.freedesktop.systemd1.Manager", 
            "GetUnitProcesses", 
            &error, 
            &reply, 
            "s", 
            services[i]
        );

        if(r < 0) {
            fprintf(stderr, "Failed to issue method call: %s\n", error.message);
            break;
        }

        r = sd_bus_message_enter_container(reply, 'a', "(sus)");
        if (r < 0) {
            result = r < 0 ? r : -EBADMSG;
            fprintf(stderr, "Invalid process reply for: %s: %d\n", services[i], result);
            break;
        }

        const char *name, *cgroup; 
        uint32_t pid;

        printf("Service: %s\n", services[i]);

        while ((r = sd_bus_message_read(
            reply, 
            "(sus)", 
            &cgroup, 
            &pid, 
            &name
        )) > 0) {
            
            printf("    PID: %" PRIu32 " Name: %s Cgroup: %s\n", pid, name, cgroup);

        }
        

    }

    return 0;
}

int append_string(char ***array, size_t *count, const char *new_str) {

    char **temp = realloc(*array, (*count + 1) * sizeof(char*));
    if (temp == NULL) {
        return -1;
    }
    *array = temp;

    size_t len = strlen(new_str);
    
    (*array)[*count] = malloc(len + 1);
    
    if ((*array)[*count] == NULL) return -1;

    strcpy((*array)[*count], new_str);

    (*count)++;
    return 0;
}

int get_conf_services_data(char* file_path) {
    printf("Getting configured services.\n---\n");
    
    if (file_path == NULL) {
        file_path = "./data/config.json";
    }

    cJSON *json = parse_json(file_path);
    if (json == NULL) {
        fprintf(stderr, "Could not parse configuration file: %s\n", file_path);
        return 1;
    }

    cJSON *services_array =
        cJSON_GetObjectItemCaseSensitive(json, "services");

    if (!cJSON_IsArray(services_array)) {
        fprintf(stderr, "Configuration has no valid services array\n");
        cJSON_Delete(json);
        return 1;
    }

    struct Service_List found_services = list_service();

    char ** use_services = NULL;
    size_t use_count=0;

    int result = 0;

    for (int i = 0; i < found_services.count; i++) {
        struct Service_Data f_service = found_services.services[i];
        cJSON * cs;
        cJSON_ArrayForEach(cs, services_array){
            // printf("Comparing %s to %s\n", cs->valuestring, f_service.name);
            if (strcmp(cs->valuestring,f_service.name) == 0) {
                if(append_string(&use_services, &use_count, cs->valuestring) < 0) {
                    fprintf(stderr, "Failed to allocate service name\n");
                    result = 1;
                    goto cleanup;
                }
                printf("Service found\n---\nSERVICE: %s\nSUB_STATE: %s\nMAIN_PID: %u\n---\n\n", cs->valuestring, f_service.sub_state, f_service.main_pid ? (unsigned int)f_service.main_pid : 0U);
            }
        }
    }
    printf("---\n");

    get_service_states(use_services, use_count);

    // Memory cleanup
    cleanup:
    for (size_t i = 0; i < use_count; i++) {
        free(use_services[i]);
    }
    free(use_services);

    for (int i = 0; i < found_services.count; i++) {
        free(found_services.services[i].name);
        free(found_services.services[i].sub_state);
    }
    free(found_services.services);
    cJSON_Delete(json);

    return 0;
}

