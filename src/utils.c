#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/resource.h>
#include <time.h>
#include "utils.h"
#include "reporter/reporter.h"

// typedef enum { MODE_DEV, MODE_SERVICE } RunMode;

RunMode get_run_mode(void) {
    const char *mode = getenv("DLOGGER_MODE");
    if (mode && strcmp(mode, "dev") == 0) return MODE_DEV;
    return MODE_SERVICE;
}

const char* get_data_dir(void) {
    if (get_run_mode() == MODE_SERVICE) return "/var/lib/dlogger";
    return "./data";
}

char* get_config_path(void) {
    if (get_run_mode() == MODE_SERVICE) return "/etc/dlogger/config.json";
    return "./data/config.json";
}

const char* get_reports_dir(void) {
    static char reports_path[512];
    const char *data_dir = get_data_dir();
    snprintf(reports_path, sizeof(reports_path), "%s/reports", data_dir);
    return reports_path;
}

cJSON* parse_json(char file_path[256]) {
    FILE *file = fopen(file_path, "rb");
    if(file == NULL) {
        perror("Error opening config\n");
        printf("file_path: %s\n", file_path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Failed to seek config");
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || (uintmax_t)length >= SIZE_MAX) {
        fprintf(stderr, "Invalid config file size\n");
        fclose(file);
        return NULL;
    }

    // printf("Length of file: %ld\n", length);

    char *buffer_config = malloc((size_t)length + 1);
    if (buffer_config == NULL) {
        perror("Memory allocation failed for some reason\n");
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("Failed to rewind config");
        free(buffer_config);
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(buffer_config, 1, (size_t)length, file);
    if (read_size != (size_t)length) {
        fprintf(stderr, "Failed to read complete config: %s\n", file_path);
        free(buffer_config);
        fclose(file);
        return NULL;
    }
    buffer_config[read_size] = '\0';

    // printf("Contents of config:\n---\n%s\n---\n", buffer_config);
    
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
    if (item_count < 0) {
        fprintf(stderr, "Service list is too large\n");
        return NULL;
    }

    char ** services = calloc((size_t)item_count + 1, sizeof *services);
    if (services == NULL) {
        fprintf(stderr, "Failed to allocate service list\n");
        return NULL;
    }

    cJSON *service;
    cJSON_ArrayForEach(service, json) {
        if (service_count >= (size_t)item_count) {
            fprintf(stderr, "Service list exceeds expected size\n");
            break;
        }
        if(!cJSON_IsString(service) || service->valuestring == NULL) continue;
        size_t length = strlen(service->valuestring) + 1;
        services[service_count] = malloc(length);

        if(services[service_count] == 0) {
            fprintf(stderr, "Failed to allocate service name\n");
            for (size_t i = 0; i < service_count; i++) free(services[i]);
            free(services);
            service_count=0;
            return NULL;
        }
        
        memcpy(services[service_count], service->valuestring, length);
        (service_count)++;
        
    }

    return services;
}


char ** parse_procs_array(cJSON* json_procs, size_t *proc_count) {
    *proc_count=0;

    if(!cJSON_IsArray(json_procs)) return NULL;

    int item_count = cJSON_GetArraySize(json_procs);
    if (item_count < 0) {
        fprintf(stderr, "Process list is too large\n");
        return NULL;
    }
    if (item_count == 0)
        return NULL;
    char **procs = calloc((size_t)item_count + 1, sizeof *procs);

    if (procs == NULL) {
        fprintf(stderr, "Failed to allocate process list\n");
        return NULL;
    }

    
    cJSON *proc;
    cJSON_ArrayForEach(proc, json_procs) {
        if (*proc_count >= (size_t)item_count) {
            fprintf(stderr, "Process list exceeds expected size\n");
            break;
        }
            
        if (!cJSON_IsString(proc) || proc->valuestring == NULL) continue;
        
        size_t length = strlen(proc->valuestring) + 1;
        procs[*proc_count] = malloc(length);

        if(procs[*proc_count] == 0) {
            fprintf(stderr, "Failed to allocate process name\n");
            for (size_t i = 0; i < *proc_count; i++) free(procs[i]);
            free(procs);
            *proc_count=0;
            return NULL;
        }
        
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

int rem_service(char *file_path, char *service)
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
    int index = 0;

    cJSON_ArrayForEach(item, services) {
        if (cJSON_IsString(item) && item->valuestring != NULL &&
            strcmp(item->valuestring, service) == 0) {
            cJSON_DeleteItemFromArray(services, index);
            result = write_json(file_path, json);
            goto cleanup;
        }
        index++;
    }

    fprintf(stderr, "No match found for %s\n", service);

cleanup:
    cJSON_Delete(json);
    return result;
}


int append_string(char ***array, size_t *count, const char *new_str)
{
    if (array == NULL || count == NULL || new_str == NULL)
        return -1;

    if (*count >= (SIZE_MAX / sizeof(**array)) - 1)
        return -1;
    char **temp = realloc(*array, (*count + 2) * sizeof(*temp));
    if (temp == NULL)
        return -1;
    *array = temp;

    (*array)[*count] = strdup(new_str);
    if ((*array)[*count] == NULL)
        return -1;

    (*count)++;
    (*array)[*count] = NULL;
    return 0;
}


int write_report(char *file_path, char *plain_text)
{
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        fprintf(stderr, "Could not get current time\n");
        return 1;
    }

    struct tm *utc_time = gmtime(&now);
    if (utc_time == NULL) {
        fprintf(stderr, "Could not convert time to UTC\n");
        return 1;
    }

    char timestamp[32];
    if (strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%SZ", utc_time) == 0) {
        fprintf(stderr, "Could not format timestamp\n");
        return 1;
    }

    char filename[512];
    int length = snprintf(filename, sizeof(filename), "%s/report-%s.txt",
                          file_path, timestamp);
    if (length < 0 || (size_t)length >= sizeof(filename)) {
        fprintf(stderr, "Report filename is too long\n");
        return 1;
    }

    FILE *file = fopen(filename, "wx");
    if (file == NULL) {
        perror("Could not create report");
        return 1;
    }

    int result = 0;
    if (fputs(plain_text, file) == EOF) {
        perror("Could not write report");
        result = 1;
    }
    if (fclose(file) == EOF) {
        perror("Could not close report");
        result = 1;
    }

    return result;
}

int send_report_smtp(char *config_path, const char * report_dir) {
    char **files = NULL;
    size_t count = 0;
    if (read_report_files(config_path, report_dir, &files, &count) != 0) {
        return 1;
    }
    
    char** accumilated_reports = NULL;
    size_t accumilated_reports_count = 0;


    for (size_t i = 0; i < count; i++) {
        // printf("file %d, %s\n", (int)i, files[(int)i]);
        size_t size = strlen(report_dir)+ strlen(files[i]) + 2;
        char* file_path = malloc(size);

        if (file_path == NULL) {
            perror("Couldn't allocate file path\n");
            break;
        }

        snprintf(file_path, size, "%s/%s", report_dir ,files[i]);
        
        char *content = NULL;
        get_content_from_report(file_path, &content);
        // printf("---\nContent:\n%s\n---\n", content);
    
        append_string(&accumilated_reports, &accumilated_reports_count, content);

        free(content);
        free(file_path);
        

    }

    if (accumilated_reports == NULL) {
        perror("Couldn't read the accumilated reports\n");
        for (size_t i = 0; i < count; i++)
            free(files[i]);
        free(files);
        return 1;
    }

    for (size_t i = 0; i < accumilated_reports_count; i++)
        free(accumilated_reports[i]);
    free(accumilated_reports);

    for (size_t i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);
    return 0;
}

void log_peak_memory(void) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        perror("getrusage error\n");
        return;
    }

    printf("Peak memory %ld KiB\n", usage.ru_maxrss);
}

int gen_now_date(struct tm *utc_time) {
    if (utc_time == NULL) {
        fprintf(stderr, "UTC output time is required\n");
        return 1;
    }
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        fprintf(stderr, "Could not get current time\n");
        return 1;
    }

    if (gmtime_r(&now, utc_time) == NULL) {
        fprintf(stderr, "Could not convert time to UTC\n");
        return 1;
    }

    return 0;
}

struct tm add_days(struct tm current_time, int days_later) {
    time_t epoch = timegm(&current_time);

    epoch += (time_t)days_later * 86400;

    struct tm result;

    gmtime_r(&epoch, &result);

    return result;
}

struct tm parse_date_str(char *date_str) {
    struct tm expires_time = {0};
    if(sscanf(date_str, "%4d%2d%2d", &expires_time.tm_year, &expires_time.tm_mon, &expires_time.tm_mday) != 3) {
        fprintf(stderr, "Invalid expires format %s\n", date_str);
    }
    expires_time.tm_year -= 1900;
    expires_time.tm_mon -= 1;
    return expires_time;
}

// 0 == expired, 1 == valid 
int is_not_expired_tm(struct tm current, struct tm expiry) {
    time_t current_epoch = timegm(&current);
    time_t expiry_epoch = timegm(&expiry);
    if (current_epoch > expiry_epoch) return 0;
    return 1;
}

int safein(char *buffer, char *msg, int retry) {
    
    if (retry <= 0) {
        retry = 1;
    }

    int fail = 1;
    
    for (int i = 0; i < retry; i++) {
        printf("%s", msg);
        fflush(stdout);
        
        if (fgets(buffer, 512, stdin) == NULL) {
            fprintf(stderr, "Error reading input\n");
            continue;
        }
        
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
            len--;
        }

        if (len >= 1) {
            fail = 0;
            break;
        }
    }

    if (fail) {
        fprintf(stderr, "Failed to get input after %d retries\n", retry);
    }
    return fail;
}

int decrypt_smtp_password(char *output, size_t output_len) {
    FILE *key_file = NULL;
    FILE *cred_file = NULL;
    char key[512] = {0};
    char encrypted[1024] = {0};
    char cmd[2048] = {0};
    int ret = 1;

    const char *key_path = NULL;
    const char *cred_path = NULL;

    if (get_run_mode() == MODE_SERVICE) {
        key_path = "/etc/dlogger/dlogger.key";
        cred_path = "/etc/dlogger/smtp_password.cred";
    } else {
        key_path = "./data/dlogger.key";
        cred_path = "./data/smtp_password.cred";
    }

    key_file = fopen(key_path, "r");
    if (key_file == NULL) {
        fprintf(stderr, "Failed to open key file: %s\n", key_path);
        return 1;
    }

    if (fgets(key, sizeof(key), key_file) == NULL) {
        fprintf(stderr, "Failed to read encryption key\n");
        fclose(key_file);
        return 1;
    }
    fclose(key_file);

    size_t key_len = strlen(key);
    if (key_len > 0 && key[key_len-1] == '\n') {
        key[key_len-1] = '\0';
    }

    cred_file = fopen(cred_path, "r");
    if (cred_file == NULL) {
        fprintf(stderr, "Failed to open credential file: %s\n", cred_path);
        return 1;
    }

    if (fgets(encrypted, sizeof(encrypted), cred_file) == NULL) {
        fprintf(stderr, "Failed to read encrypted password\n");
        fclose(cred_file);
        return 1;
    }
    fclose(cred_file);

    size_t enc_len = strlen(encrypted);
    if (enc_len > 0 && encrypted[enc_len-1] == '\n') {
        encrypted[enc_len-1] = '\0';
    }

    snprintf(cmd, sizeof(cmd), "echo '%s' | openssl enc -aes-256-cbc -d -base64 -pass 'pass:%s' 2>/dev/null", encrypted, key);

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL) {
        fprintf(stderr, "Failed to execute openssl\n");
        return 1;
    }

    if (fgets(output, output_len, pipe) == NULL) {
        fprintf(stderr, "Failed to decrypt password\n");
        pclose(pipe);
        return 1;
    }

    int status = pclose(pipe);
    if (status != 0) {
        fprintf(stderr, "Decryption failed with status: %d\n", status);
        return 1;
    }

    size_t dec_len = strlen(output);
    if (dec_len > 0 && output[dec_len-1] == '\n') {
        output[dec_len-1] = '\0';
    }

    return 0;
}

// Remember to free
char* transform_prefix_sufix(char* pre, char* ctx, char *suf) {
    size_t pre_len = strlen(pre);
    size_t ctx_len = strlen(ctx);
    size_t suf_len = strlen(suf);

    char *result = malloc(pre_len + ctx_len + suf_len + 1);
    if (!result) return NULL;

    memcpy(result, pre, pre_len);
    memcpy(result + pre_len, ctx, ctx_len);
    memcpy(result + pre_len + ctx_len, suf, suf_len + 1);

    return result;
}

int get_reports_and_format_content(char *config_path, char *report_dir, char ***contents, size_t *count) {
    char ** files = NULL;
    size_t files_count = 0;
    if (read_report_files(config_path, report_dir, &files, &files_count) != 0) {
        fprintf(stderr, "Couldn't read report files\n");
        return 1;
    }

    if (files_count == 0 || files == NULL) {
        perror("No reports found\n");
        return 0;
    }

    for (int i = 0; i < files_count; i++) {
        size_t size = strlen(report_dir) + strlen(files[i]) + 2;
        char *file_path = malloc(size);
        if (file_path == NULL) {
            fprintf(stderr, "Failed to allocate file_path\n");
            continue;
        }
        snprintf(file_path, size, "%s/%s", report_dir, files[i]);
        
        char *content = NULL;
        int res = get_content_from_report(file_path, &content);
        
        if (res != 0 || content == NULL) {
            content = malloc(100);
            if (content != NULL) {
                snprintf(content, 100, "Couldn't read report, it may be empty.\n");
            }
        }

        char *pre = malloc(256);
        if (pre != NULL) {
            snprintf(pre, 256, "\nFile %s\n", files[i]);
        }

        char *content_transform = transform_prefix_sufix(pre, content, "\n");

        if (content_transform == NULL) {
            fprintf(stderr, "Failed to format prefix & suffix\n");
        } else {
            if (content != NULL) free(content);
            content = content_transform;
        }

        if (append_string(contents, count, content) != 0) {
            fprintf(stderr, "Error occurred while appending string to contents\n");
        }

        if (content != NULL) free(content);
        if (pre != NULL) free(pre);
        free(file_path);
    }

    for (size_t i = 0; i < files_count; i++) {
        free(files[i]);
    }
    free(files);

    return 0;
}