#include <stdint.h>

struct Service_Data {
    char* name; 
    char* sub_state;
    uint32_t main_pid;
};

struct Service_List{
    struct Service_Data *services;
    int count;
};

struct Service_List list_service(void);

int get_conf_services_data(char *file_path);