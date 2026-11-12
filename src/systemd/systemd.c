#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include "systemd.h"
#include "../utils.h"

void free_service_list(struct Service_List *list)
{
    for (int i = 0; i < list->count; i++) {
        free(list->services[i].name);
        free(list->services[i].sub_state);
    }
    free(list->services);
    list->services = NULL;
    list->count = 0;
}

int list_service(struct Service_List *result)
{
    if (result == NULL) {
        fprintf(stderr, "Cannot list services without an output list\n");
        return -1;
    }
    *result = (struct Service_List){0};
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int status = -1;
    int capacity = 0;
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-r));
        goto cleanup;
    }

    r = sd_bus_call_method(bus, "org.freedesktop.systemd1",
                           "/org/freedesktop/systemd1",
                           "org.freedesktop.systemd1.Manager", "ListUnits",
                           &error, &reply, "");
    if (r < 0) {
        fprintf(stderr, "Failed to list units: %s\n",
                error.message ? error.message : strerror(-r));
        goto cleanup;
    }

    r = sd_bus_message_enter_container(reply, 'a', "(ssssssouso)");
    if (r <= 0) {
        fprintf(stderr, "Invalid ListUnits reply\n");
        goto cleanup;
    }

    const char *name, *desc, *load_state, *active_state, *sub_state;
    const char *following, *unit_path, *job_type, *job_path;
    uint32_t job_id;
    while ((r = sd_bus_message_read(reply, "(ssssssouso)",
                                    &name, &desc, &load_state, &active_state,
                                    &sub_state, &following, &unit_path, &job_id,
                                    &job_type, &job_path)) > 0) {
        if (strcmp(active_state, "active") != 0 || strstr(name, ".service") == NULL)
            continue;

        uint32_t main_pid = 0;
        sd_bus_error_free(&error);
        r = sd_bus_get_property_trivial(bus, "org.freedesktop.systemd1", unit_path,
                                        "org.freedesktop.systemd1.Service", "MainPID",
                                        &error, 'u', &main_pid);
        if (r < 0) {
            fprintf(stderr, "Failed to get MainPID for %s: %s\n", name,
                    error.message ? error.message : strerror(-r));
            goto cleanup;
        }

        if (result->count == capacity) {
            if (capacity > INT_MAX / 2) {
                fprintf(stderr, "Too many services\n");
                goto cleanup;
            }
            int new_capacity = capacity ? capacity * 2 : 10;
            if ((size_t)new_capacity > SIZE_MAX / sizeof(*result->services)) {
                fprintf(stderr, "Service list is too large\n");
                goto cleanup;
            }
            struct Service_Data *temp = realloc(result->services,
                                                (size_t)new_capacity * sizeof(*temp));
            if (temp == NULL) {
                fprintf(stderr, "Failed to allocate service list\n");
                goto cleanup;
            }
            result->services = temp;
            capacity = new_capacity;
        }

        char *name_copy = strdup(name);
        char *state_copy = strdup(sub_state);
        if (name_copy == NULL || state_copy == NULL) {
            fprintf(stderr, "Failed to allocate service data\n");
            free(name_copy);
            free(state_copy);
            goto cleanup;
        }
        result->services[result->count++] = (struct Service_Data){
            .name = name_copy, .sub_state = state_copy, .main_pid = main_pid
        };
    }
    if (r < 0 || (r = sd_bus_message_exit_container(reply)) < 0) {
        fprintf(stderr, "Failed to read unit list: %s\n", strerror(-r));
        goto cleanup;
    }
    status = 0;

cleanup:
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    if (status != 0)
        free_service_list(result);
    return status;
}

static int get_service_states(char *services[], size_t count)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int result = -1;
    if (count == 0)
        return 0;

    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-r));
        goto cleanup;
    }

    for (size_t i = 0; i < count; i++) {
        r = sd_bus_call_method(bus, "org.freedesktop.systemd1",
                               "/org/freedesktop/systemd1",
                               "org.freedesktop.systemd1.Manager", "GetUnitProcesses",
                               &error, &reply, "s", services[i]);
        if (r < 0) {
            fprintf(stderr, "Failed to get processes for %s: %s\n", services[i],
                    error.message ? error.message : strerror(-r));
            goto cleanup;
        }
        r = sd_bus_message_enter_container(reply, 'a', "(sus)");
        if (r <= 0) {
            fprintf(stderr, "Invalid process reply for %s\n", services[i]);
            goto cleanup;
        }

        const char *name, *cgroup;
        uint32_t pid;
        printf("Service: %s\n", services[i]);
        while ((r = sd_bus_message_read(reply, "(sus)", &cgroup, &pid, &name)) > 0)
            printf("    PID: %" PRIu32 " Name: %s Cgroup: %s\n", pid, name, cgroup);
        if (r < 0 || (r = sd_bus_message_exit_container(reply)) < 0) {
            fprintf(stderr, "Failed to read processes for %s: %s\n",
                    services[i], strerror(-r));
            goto cleanup;
        }
        reply = sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
    }
    result = 0;

cleanup:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    sd_bus_unref(bus);
    return result;
}

static int append_string(char ***array, size_t *count, const char *new_str)
{
    if (*count >= SIZE_MAX / sizeof(**array))
        return -1;
    char **temp = realloc(*array, (*count + 1) * sizeof(*temp));
    if (temp == NULL)
        return -1;
    *array = temp;

    (*array)[*count] = strdup(new_str);
    if ((*array)[*count] == NULL)
        return -1;

    (*count)++;
    return 0;
}

int get_conf_services_data(char *file_path)
{
    printf("Getting configured services.\n---\n");
    if (file_path == NULL)
        file_path = "./data/config.json";

    cJSON *json = parse_json(file_path);
    if (json == NULL) {
        fprintf(stderr, "Could not parse configuration file: %s\n", file_path);
        return 1;
    }

    cJSON *services_array = cJSON_GetObjectItemCaseSensitive(json, "services");
    if (!cJSON_IsArray(services_array)) {
        fprintf(stderr, "Configuration has no valid services array\n");
        cJSON_Delete(json);
        return 1;
    }
    cJSON *cs;
    cJSON_ArrayForEach(cs, services_array) {
        if (!cJSON_IsString(cs) || cs->valuestring == NULL) {
            fprintf(stderr, "Configuration contains a non-string service\n");
            cJSON_Delete(json);
            return 1;
        }
    }

    struct Service_List found_services = {0};
    char **use_services = NULL;
    size_t use_count = 0;
    int result = 1;
    if (list_service(&found_services) != 0)
        goto cleanup;

    for (int i = 0; i < found_services.count; i++) {
        struct Service_Data f_service = found_services.services[i];
        cJSON_ArrayForEach(cs, services_array) {
            if (strcmp(cs->valuestring, f_service.name) == 0) {
                if (append_string(&use_services, &use_count, cs->valuestring) < 0) {
                    fprintf(stderr, "Failed to allocate service name\n");
                    goto cleanup;
                }
                printf("Service found\n---\nSERVICE: %s\nSUB_STATE: %s\nMAIN_PID: %" PRIu32 "\n---\n\n",
                       cs->valuestring, f_service.sub_state, f_service.main_pid);
            }
        }
    }
    printf("---\n");
    if (get_service_states(use_services, use_count) != 0)
        goto cleanup;
    result = 0;

cleanup:
    for (size_t i = 0; i < use_count; i++)
        free(use_services[i]);
    free(use_services);
    free_service_list(&found_services);
    cJSON_Delete(json);
    return result;
}
