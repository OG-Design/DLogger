#include "../third_party/cjson/cJSON.h"
#include <time.h>
typedef enum { MODE_DEV, MODE_SERVICE } RunMode;
RunMode get_run_mode(void);
const char* get_data_dir(void);
char* get_config_path(void);
const char* get_reports_dir(void);
cJSON* parse_json(char file_path[256]);
int write_json(char file_path[256], cJSON* json);
/* Caller frees each string and the NULL-terminated array. */
char **parse_services(cJSON *json);
char ** parse_procs_array(cJSON* json_procs, size_t *proc_count);
int add_service(char* file_path, char* service);
int rem_service(char* file_path, char* service);
int append_string(char ***array, size_t *count, const char *new_str);
int write_report(char* file_path, char* plain_text);
int send_report_smtp(char *config_path, const char * report_dir);
void log_peak_memory(void);
int gen_now_date(struct tm *utc_time);
struct tm add_days(struct tm current_time, int days_later);
struct tm parse_date_str(char *date_str);
int is_not_expired_tm(struct tm current, struct tm expiry);
int safein(char *buffer, char* msg, int retry);
int decrypt_smtp_password(char *output, size_t output_len);
int get_reports_and_format_content(char *config_path, char *report_dir, char ***contents, size_t *count);