#include <stddef.h>
#include <stdint.h>
#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "reporter.h"
#include "../utils.h"

static bool is_report_in_range(const char *name, const char *start_date,
                               const char *end_date)
{
    if (strlen(name) != 27 || strncmp(name, "report-", 7) != 0 ||
        name[15] != 'T' || name[22] != 'Z' || strcmp(name + 23, ".txt") != 0)
        return false;

    for (size_t i = 7; i < 22; i++) {
        if (i != 15 && (name[i] < '0' || name[i] > '9'))
            return false;
    }

    return strncmp(name + 7, start_date, 8) >= 0 &&
           strncmp(name + 7, end_date, 8) <= 0;
}

static void free_report_files(char **files, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(files[i]);
    free(files);
}

int get_report_files(const char *report_dir, const char *start_date,
                            const char *end_date, char ***files, size_t *count)
{
    DIR *dir = opendir(report_dir);
    if (dir == NULL) {
        perror("Could not open report directory");
        return 1;
    }

    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                perror("Could not read report directory");
                goto error;
            }
            break;
        }

        if (is_report_in_range(entry->d_name, start_date, end_date) &&
            append_string(files, count, entry->d_name) != 0) {
            fprintf(stderr, "Could not allocate report filename\n");
            goto error;
        }
    }

    if (closedir(dir) != 0) {
        perror("Could not close report directory");
        goto free_files;
    }
    return 0;

error:
    if (closedir(dir) != 0)
        perror("Could not close report directory");
free_files:
    free_report_files(*files, *count);
    *files = NULL;
    *count = 0;
    return 1;
}

static int format_days_ago(time_t now, int days, char date_string[9])
{
    struct tm date;
    if (gmtime_r(&now, &date) == NULL) {
        fprintf(stderr, "Could not convert current time to UTC\n");
        return 1;
    }

    date.tm_hour = 0;
    date.tm_min = 0;
    date.tm_sec = 0;
    date.tm_mday -= days;
    time_t timestamp = timegm(&date);
    if (timestamp == (time_t)-1 || gmtime_r(&timestamp, &date) == NULL ||
        date.tm_year < -1900 || date.tm_year > 8099 ||
        strftime(date_string, 9, "%Y%m%d", &date) != 8) {
        fprintf(stderr, "Could not calculate UTC report date\n");
        return 1;
    }
    return 0;
}

int get_configured_range(char *config_path, int *near_days, int *far_days)
{
    cJSON *json = parse_json(config_path);
    if (json == NULL) {
        fprintf(stderr, "Could not parse configuration file: %s\n", config_path);
        return 1;
    }

    cJSON *properties = cJSON_GetObjectItemCaseSensitive(json, "properties");
    cJSON *report_properties = cJSON_GetObjectItemCaseSensitive(properties, "report-properties");
    cJSON *range = cJSON_GetObjectItemCaseSensitive(report_properties, "day-range");
    cJSON *near = cJSON_GetArrayItem(range, 0);
    cJSON *far = cJSON_GetArrayItem(range, 1);
    int result = 1;

    if (!cJSON_IsObject(properties) || !cJSON_IsObject(report_properties) ||
        !cJSON_IsArray(range) || cJSON_GetArraySize(range) != 2 ||
        !cJSON_IsNumber(near) || !cJSON_IsNumber(far) ||
        !(near->valuedouble >= 0 && near->valuedouble <= INT_MAX) ||
        !(far->valuedouble >= 0 && far->valuedouble <= INT_MAX) ||
        near->valuedouble != (double)(int)near->valuedouble ||
        far->valuedouble != (double)(int)far->valuedouble ||
        near->valuedouble > far->valuedouble) {
        fprintf(stderr, "Invalid properties.report-properties.day-range: "
                        "expected [near_days, far_days] as non-negative integers "
                        "with near_days <= far_days\n");
        goto cleanup;
    }

    *near_days = (int)near->valuedouble;
    *far_days = (int)far->valuedouble;
    result = 0;

cleanup:
    cJSON_Delete(json);
    return result;
}

int read_report_files(char *config_path, const char *report_dir,
                      char ***files, size_t *count)
{
    if (files == NULL || count == NULL) {
        fprintf(stderr, "Report list and count are required\n");
        return 1;
    }
    *files = NULL;
    *count = 0;
    if (config_path == NULL || report_dir == NULL) {
        fprintf(stderr, "Configuration path and report directory are required\n");
        return 1;
    }

    int near_days, far_days;
    if (get_configured_range(config_path, &near_days, &far_days) != 0)
        return 1;

    time_t now = time(NULL);
    if (now == (time_t)-1) {
        fprintf(stderr, "Could not get current time\n");
        return 1;
    }
    char start_date[9], end_date[9];
    if (format_days_ago(now, far_days, start_date) != 0 ||
        format_days_ago(now, near_days, end_date) != 0)
        return 1;

    return get_report_files(report_dir, start_date, end_date, files, count);
}


int get_content_from_report(char *file_path, char **content) {
    FILE *file = fopen(file_path, "rb");
    if(file == NULL) {
        perror("Error opening report\n");
        printf("file_path: %s\n", file_path);
        return 1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Failed to seek report");
        fclose(file);
        return 1;
    }
    long length = ftell(file);
    if (length < 0 || (uintmax_t)length >= SIZE_MAX) {
        fprintf(stderr, "Invalid report file size\n");
        fclose(file);
        return 1;
    }

    // printf("Length of file: %ld\n", length);

    char *buffer_report = malloc((size_t)length + 1);
    if (buffer_report == NULL) {
        perror("Memory allocation failed for some reason\n");
        fclose(file);
        return 1;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("Failed to rewind report");
        free(buffer_report);
        fclose(file);
        return 1;
    }
    size_t read_size = fread(buffer_report, 1, (size_t)length, file);
    if (read_size != (size_t)length) {
        fprintf(stderr, "Failed to read complete report: %s\n", file_path);
        free(buffer_report);
        fclose(file);
        return 1;
    }
    buffer_report[read_size] = '\0';

    // printf("Contents of report:\n---\n%s\n---\n", buffer_report);

    *content = buffer_report;
    fclose(file);

    return 0;
}