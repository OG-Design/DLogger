#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/systemd/systemd.h"
#include "../src/utils.h"

static int fail_malloc;
static int fail_realloc;
static int fail_strdup_after;

void *__real_malloc(size_t size);
void *__real_realloc(void *ptr, size_t size);
char *__real_strdup(const char *str);

void *__wrap_malloc(size_t size)
{
    if (fail_malloc) {
        fail_malloc = 0;
        return NULL;
    }
    return __real_malloc(size);
}

void *__wrap_realloc(void *ptr, size_t size)
{
    if (fail_realloc) {
        fail_realloc = 0;
        return NULL;
    }
    return __real_realloc(ptr, size);
}

char *__wrap_strdup(const char *str)
{
    if (fail_strdup_after > 0 && --fail_strdup_after == 0)
        return NULL;
    return __real_strdup(str);
}

static int count_fds(void)
{
    DIR *dir = opendir("/proc/self/fd");
    if (dir == NULL)
        return -1;
    int count = 0;
    while (readdir(dir) != NULL)
        count++;
    closedir(dir);
    return count;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 1;
    int before = count_fds();
    if (before < 0)
        return 1;

    cJSON *array = cJSON_Parse("[\"first\", 1, \"second\"]");
    if (array == NULL)
        return 1;
    char **services = parse_services(array);
    if (services == NULL || strcmp(services[0], "first") != 0 ||
        strcmp(services[1], "second") != 0 || services[2] != NULL) {
        fprintf(stderr, "Service list was not correctly terminated\n");
        cJSON_Delete(array);
        return 1;
    }
    free(services[0]);
    free(services[1]);
    free(services);
    cJSON_Delete(array);

    fail_malloc = 1;
    cJSON *json = parse_json(argv[1]);
    if (fail_malloc || json != NULL || count_fds() != before) {
        fprintf(stderr, "Config allocation failure was not cleaned up\n");
        cJSON_Delete(json);
        return 1;
    }

    for (int failure = 0; failure < 3; failure++) {
        struct Service_List list = {0};
        if (failure == 0)
            fail_realloc = 1;
        else
            fail_strdup_after = failure;
        int result = list_service(&list);
        if (result == 0 || fail_realloc || fail_strdup_after ||
            list.services != NULL || list.count != 0 || count_fds() != before) {
            fprintf(stderr, "Service allocation failure %d was not cleaned up\n", failure);
            free_service_list(&list);
            return 1;
        }
    }
    return 0;
}
