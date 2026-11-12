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

/* On success, the caller must release the list with free_service_list(). */
int list_service(struct Service_List *result);
void free_service_list(struct Service_List *list);

int get_conf_services_data(char *file_path);