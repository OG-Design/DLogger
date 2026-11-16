#include "../utils.h"
#include <curl/curl.h>
int add_mail_recipient(char *file_path) {
    cJSON *json = parse_json(file_path);

    return 0;
}

struct SMTP_Client{
    char* user;
    char* pass;
    char* url;
    char* mailfrom;
    char** recipients;
};

int send_mail(struct SMTP_Client smtp) {
    
    CURL *curl = curl_easy_init();

    return 0;
}
