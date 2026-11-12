#include "../third_party/cjson/cJSON.h"
cJSON* parse_json(char file_path[256]);
/* Caller frees each string and the NULL-terminated array. */
char **parse_services(cJSON *json);
char ** parse_procs_array(cJSON* json_procs, size_t *proc_count);
int add_service(char* file_path, char* service);