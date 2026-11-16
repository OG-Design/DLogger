#include "../third_party/cjson/cJSON.h"
typedef enum { MODE_DEV, MODE_SERVICE } RunMode;
RunMode get_run_mode(void);
const char* get_data_dir(void);
char* get_config_path(void);
const char* get_reports_dir(void);
cJSON* parse_json(char file_path[256]);
/* Caller frees each string and the NULL-terminated array. */
char **parse_services(cJSON *json);
char ** parse_procs_array(cJSON* json_procs, size_t *proc_count);
int add_service(char* file_path, char* service);
int rem_service(char* file_path, char* service);
int append_string(char ***array, size_t *count, const char *new_str);
int write_report(char* file_path, char* plain_text);
int send_report_smtp(char *config_path, const char * report_dir);
void log_peak_memory(void);