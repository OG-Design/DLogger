#include "../utils.h"
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/typecheck-gcc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int add_mail_recipient(char *file_path, char *recipient) {
    int result = 0;

    cJSON *json = parse_json(file_path);
    cJSON *service_accounts = cJSON_GetObjectItemCaseSensitive(json, "service-accounts");
    if (service_accounts == NULL || !cJSON_IsArray(service_accounts)) {
        fprintf(stderr, "Error getting service accounts, it may not exist, or it may not be an array\n");
        result=1;
        cJSON_Delete(json);
        return result;
    }

    int size = cJSON_GetArraySize(service_accounts);

    cJSON *account = NULL;
    cJSON_ArrayForEach(account, service_accounts) {
        cJSON *to = cJSON_GetObjectItemCaseSensitive(account, "to");
        if (to == NULL || !cJSON_IsString(to)) {
            fprintf(stderr, "Invalid 'to' field in service-accounts\n");
            continue;
        }
        if (to->valuestring && strcmp(to->valuestring, recipient) == 0) {            
            result=0;
            cJSON_Delete(json);
            return result;    
        }
    }

    
    cJSON * mail_object = cJSON_CreateObject();
    if(mail_object == NULL) {
        fprintf(stderr, "Failed to create mail object\n");
        cJSON_Delete(json);
        return 1;
    }
    
    cJSON * json_recipient = cJSON_CreateString(recipient);
    if (json_recipient == NULL) {
        fprintf(stderr, "Failed to create json string of recipient, invalid string\n");
        result=1;
        cJSON_Delete(json);
        cJSON_Delete(mail_object);
        return result;
    }

    if (!cJSON_AddItemToObject(mail_object, "to", json_recipient)) {
        fprintf(stderr, "Failed to add recipient to object\n");
        result=1;
        cJSON_Delete(json);
        return result;
    }

    struct tm utc_time;
    
    if (gen_now_date(&utc_time) != 0) {
        result = 1;
        fprintf(stderr, "Failed to get date");
        return result;
    }

    struct tm expires = add_days(utc_time, 30);

    char time[32];
    if (strftime(time, sizeof(time), "%Y%m%dT%H%M%SZ", &expires) == 0) {
        return 1;
    }

    cJSON * json_time = cJSON_CreateString(time);
    if (json_time == NULL) {
        fprintf(stderr, "Failed to create json string of time, invalid string\n");
        result=1;
        cJSON_Delete(mail_object);
        cJSON_Delete(json);
        return result;
    }

    if (!cJSON_AddItemToObject(mail_object, "expires", json_time)) {
        fprintf(stderr, "Failed to add recipient to object\n");
        result=1;
        cJSON_Delete(json);
        return result;
    }

    if (!cJSON_AddItemToArray(service_accounts, mail_object)) {
        fprintf(stderr, "Failed to add recipient object to array\n");
        result=1;
        cJSON_Delete(json);
        return result;
    }

    if (write_json(file_path, json) != 0) {
        result = 1;
        cJSON_Delete(json);
        return result;    
    }

    cJSON_Delete(json);

    return result;
}

struct SMTP_Client{
    char user[256];
    char url[512];
    char mailfrom[256];
};

int create_smtp_config(char *file_path, struct SMTP_Client smtp) {
    int result = 0;
    cJSON *json = parse_json(file_path);
    

    if (json == NULL) {
        perror("Couldn't get json");
        cJSON_Delete(json);
        return 1;
    }

    cJSON *properties = cJSON_GetObjectItemCaseSensitive(json, "properties");
    

    if (properties == NULL) {
        perror("Couldn't get json properties");
        cJSON_Delete(json);
        return 1;
    }

    cJSON *config_object = cJSON_CreateObject();
    cJSON *user_string = cJSON_CreateString(smtp.user);
    cJSON *url_string = cJSON_CreateString(smtp.url);
    cJSON *mailfrom_string = cJSON_CreateString(smtp.mailfrom);
   
    if (config_object == NULL ||
        user_string == NULL ||
        url_string == NULL ||
        mailfrom_string == NULL
    ) 
    {
        perror("Didn't recieve all required fields\n");
        result=1;
        goto free_json;
        
    }

    if (!cJSON_AddItemToObject(config_object, "user", user_string)) {
        perror("Failed to add user to config\n");
        cJSON_Delete(user_string);
        cJSON_Delete(config_object);
        result = 1;    
        goto free_json;
    }
    
    if (!cJSON_AddItemToObject(config_object, "url", url_string)) {
        perror("Failed to add url to config\n");
        cJSON_Delete(url_string);
        cJSON_Delete(config_object);
        result = 1;
        goto free_json;
        
    }
    
    if (!cJSON_AddItemToObject(config_object, "mailfrom", mailfrom_string)) {
        perror("Failed to add mailfrom to config\n");
        cJSON_Delete(mailfrom_string);
        cJSON_Delete(config_object);
        result = 1;
        goto free_json;
        
    }
    
    cJSON *smtp_config = cJSON_GetObjectItemCaseSensitive(properties, "smtp-config");
    
    if (smtp_config != NULL) {
        cJSON_ReplaceItemInObject(properties, "smtp-config", config_object);
    } else {
        cJSON_AddItemToObject(properties, "smtp-config", config_object);
    }

    if(write_json(file_path, json) != 0) {
        fprintf(stderr, "Couldn't write changes to json\n");
        result = 1;
        goto free_json;
    }

free_json:
    cJSON_Delete(json);
    
    return result;
}

int smtp_wizard(char *file_path) {
    struct SMTP_Client smtp = {};
    printf("Welcome to the SMTP Setup Wizard for DLogger!\n\nType the url with colon -> : and add the SMTP PORT after it.\n\n");
    if (safein(smtp.url, "\tSMTP URL: ", 1) != 0) return 1;
    if (safein(smtp.user, "\tSMTP USER: ", 1) != 0) return 1;
    if (safein(smtp.mailfrom, "\tSMTP FROM <nospaces>: ", 1) != 0) return 1;

    if (create_smtp_config(file_path, smtp) != 0) {
        fprintf(stderr, "Error occurred while creating smtp config\n");
        return 1;
    }


    printf("%s\n", smtp.url);

    return 0;
}
struct mailcfg {
    char* fromaddr;
    char* toaddr;
    char* frommail;
    char* tomail;
    char* ccmail;
};

int payload_text_constructor(char ***payload_text, struct mailcfg mcfg, char **inserted_text, size_t inserted_text_count) {
    if (payload_text == NULL) {
        fprintf(stderr, "payload_text pointer is NULL\n");
        return 1;
    }
    
    struct tm current_time = {0};
    time_t now = time(NULL);
    gmtime_r(&now, &current_time);
    
    char date_buf[128];
    if (strftime(date_buf, sizeof(date_buf), "Date: %a, %d %b %Y %H:%M:%S +0000\r\n", &current_time) == 0) {
        fprintf(stderr, "Failed to format date\n");
        return 1;
    }
    
    char message_id[256];
    if (snprintf(message_id, sizeof(message_id), "Message-ID: <%lld.%d@dlogger.local>\r\n", 
                 (long long)now, (int)getpid()) < 0) {
        fprintf(stderr, "Failed to create Message-ID\n");
        return 1;
    }
    
    char subject_buf[256];
    if (snprintf(subject_buf, sizeof(subject_buf), "Subject: DLogger Report - %04d-%02d-%02d\r\n\r\n",
                 current_time.tm_year + 1900,
                 current_time.tm_mon + 1,
                 current_time.tm_mday) < 0) {
        fprintf(stderr, "Failed to create Subject\n");
        return 1;
    }
    
    size_t count = 0;
    
    if (append_string(payload_text, &count, date_buf) != 0) return 1;
    
    if (mcfg.tomail && strlen(mcfg.tomail) > 0) {
        if (append_string(payload_text, &count, "To: ") != 0) return 1;
        if (append_string(payload_text, &count, mcfg.tomail) != 0) return 1;
        if (append_string(payload_text, &count, "\r\n") != 0) return 1;
    }
    
    if (mcfg.frommail && strlen(mcfg.frommail) > 0) {
        if (append_string(payload_text, &count, "From: ") != 0) return 1;
        if (append_string(payload_text, &count, mcfg.frommail) != 0) return 1;
        if (append_string(payload_text, &count, "\r\n") != 0) return 1;
    }
    
    if (mcfg.ccmail && strlen(mcfg.ccmail) > 0) {
        if (append_string(payload_text, &count, "Cc: ") != 0) return 1;
        if (append_string(payload_text, &count, mcfg.ccmail) != 0) return 1;
        if (append_string(payload_text, &count, "\r\n") != 0) return 1;
    }
    
    if (append_string(payload_text, &count, message_id) != 0) return 1;
    if (append_string(payload_text, &count, subject_buf) != 0) return 1;
    
    if (inserted_text != NULL && inserted_text_count > 0) {
        for (size_t i = 0; i < inserted_text_count; i++) {
            if (inserted_text[i] != NULL) {
                if (append_string(payload_text, &count, inserted_text[i]) != 0) return 1;
                if (append_string(payload_text, &count, "\r\n") != 0) return 1;
            }
        }
    } else {
        if (append_string(payload_text, &count, "No reports available.\r\n") != 0) return 1;
    }
    
    return 0;
} 

struct upload_status {
    size_t bytes_read;
    char **payload_text;
    size_t payload_lines;
};

static size_t read_cb(char *ptr, size_t size, size_t nmemb, void *userp) {
    struct upload_status *upload_ctx = (struct upload_status *)userp;
    const char *data;
    size_t room = size * nmemb;
    size_t len;

    if((size == 0) || (nmemb == 0) || ((size * nmemb) < 1)) {
        return 0;
    }

    if (upload_ctx->payload_text == NULL || upload_ctx->bytes_read >= upload_ctx->payload_lines) {
        return 0;
    }

    data = upload_ctx->payload_text[upload_ctx->bytes_read];
    if (data == NULL) {
        return 0;
    }

    len = strlen(data);
    if(room < len) len = room;
    memcpy(ptr, data, len);
    upload_ctx->bytes_read++;

    return len;
}

int send_mail_report(char *file_path) {
    printf("Sending mail report\n");
    int result = 0;

    // Get json config file
    cJSON *json = parse_json(file_path);
    
    if (json == NULL) {
        perror("Couldn't get json");
        cJSON_Delete(json);
        return 1;
    }

    // Get properties object
    cJSON *properties = cJSON_GetObjectItemCaseSensitive(json, "properties");
    
    if (properties == NULL) {
        perror("Couldn't get json properties");    
        result = 1;
        goto free_json;
    }

    // Get smtp configurations object
    cJSON *smtp_config = cJSON_GetObjectItemCaseSensitive(properties, "smtp-config");
    
    if (smtp_config == NULL) {
        perror("Couldn't get smtp_config");
        result = 1;
        goto free_json;
    }

    // Get user string
    cJSON *user = cJSON_GetObjectItemCaseSensitive(smtp_config, "user");
    if (user == NULL || !cJSON_IsString(user)) {
        perror("Property: user not found or not string in smtp_config");
        result = 1;
        goto free_json;
    }


    // Get smtp server url
    cJSON *url = cJSON_GetObjectItemCaseSensitive(smtp_config, "url");
    if (url == NULL || !cJSON_IsString(url)) {
        perror("Property: url not found or not string in smtp_config");
        result = 1;
        goto free_json;
    }
    
    char smtp_url[512];
    if (strstr(url->valuestring, "://") == NULL) {
        // No scheme, check port to determine protocol
        if (strstr(url->valuestring, ":465") != NULL) {
            // Port 465 = SMTPS (implicit TLS)
            if (snprintf(smtp_url, sizeof(smtp_url), "smtps://%s", url->valuestring) < 0) {
                perror("Failed to construct SMTP URL");
                result = 1;
                goto free_json;
            }
        } else {
            // Default to smtp:// (supports STARTTLS on 587)
            if (snprintf(smtp_url, sizeof(smtp_url), "smtp://%s", url->valuestring) < 0) {
                perror("Failed to construct SMTP URL");
                result = 1;
                goto free_json;
            }
        }
    } else {
        // Has scheme, use as is
        if (snprintf(smtp_url, sizeof(smtp_url), "%s", url->valuestring) < 0) {
            perror("Failed to copy SMTP URL");
            result = 1;
            goto free_json;
        }
    }

    // Get mailfrom
    cJSON *mailfrom = cJSON_GetObjectItemCaseSensitive(smtp_config, "mailfrom");
    if (mailfrom == NULL || !cJSON_IsString(mailfrom)) {
        perror("Property: mailfrom not found or not string in smtp_config\n");
        result = 1;
        goto free_json;
    }

    
    // Get recipients
    cJSON *service_accounts = cJSON_GetObjectItemCaseSensitive(json, "service-accounts");

    if (service_accounts == NULL || !cJSON_IsArray(service_accounts)) {
        perror("Service accounts not found or invalid format\n");
        result = 1;
        goto free_json;
    }

    int service_accounts_length = cJSON_GetArraySize(service_accounts);
    
    if (service_accounts_length <= 0) {
        perror("Service accounts not found, add one with: dlogger -m\n");
        result = 1;
        goto free_json;    
    }

    const char *reports_path = get_reports_dir();
    if (reports_path == NULL) {
        result = 1;
        goto free_json;
    }
    char *report_dir = malloc(strlen(reports_path) + 1);
    if (report_dir == NULL) {
        result = 1;
        goto free_json;
    }
    strcpy(report_dir, reports_path);

    char **contents = NULL;
    size_t contents_count = 0;
    
    // Get reports
    if (get_reports_and_format_content(file_path, report_dir, &contents, &contents_count) != 0) {
        fprintf(stderr, "Error formatting contents\n");
        free(report_dir);
        return 1;
    }
    free(report_dir);

    if (contents == NULL) {
        fprintf(stderr, "Error: no report contents\n");
        return 1;
    }

    // Build mail configuration
    struct mailcfg mcfg;
    mcfg.frommail = mailfrom->valuestring;
    mcfg.tomail = "";
    mcfg.ccmail = "";
    
    // Build email payload
    char **payload_text = NULL;
    if (payload_text_constructor(&payload_text, mcfg, contents, contents_count) != 0) {
        fprintf(stderr, "Failed to construct email payload\n");
        result = 1;
        goto free_json;
    }

    // Send mail
    CURL *curl;

    CURLcode curl_result = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_result != CURLE_OK) {
        fprintf(stderr, "Curl error, not ok\n");
    }
    
    curl = curl_easy_init();
    struct tm current_time = {0};
    gen_now_date(&current_time);

    if (curl) {
    
        struct curl_slist *recipients = NULL;
        struct upload_status upload_ctx = {0};
        
        // Count payload lines
        size_t payload_lines = 0;
        while (payload_text != NULL && payload_text[payload_lines] != NULL) {
            payload_lines++;
        }
        
        upload_ctx.payload_text = payload_text;
        upload_ctx.payload_lines = payload_lines;

        curl_easy_setopt(curl, CURLOPT_URL, smtp_url);
    
        // Wrap mailfrom in angle brackets as per SMTP RFC
        char formatted_mailfrom[512];
        if (snprintf(formatted_mailfrom, sizeof(formatted_mailfrom), "<%s>", mailfrom->valuestring) > 0) {
            curl_easy_setopt(curl, CURLOPT_MAIL_FROM, formatted_mailfrom);
        } else {
            curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mailfrom->valuestring);
        }
        
        // Set connection timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        
        // SSL/TLS options
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        
        // Authentication
        char password[512] = {0};
        if (decrypt_smtp_password(password, sizeof(password)) == 0) {
            char userpwd[1024] = {0};
            if (snprintf(userpwd, sizeof(userpwd), "%s:%s", user->valuestring, password) >= 0) {
                curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);
            }
        } else {
            fprintf(stderr, "Warning: Could not decrypt SMTP password\n");
        }

        // set recipients
        cJSON *recipient={0};
        cJSON_ArrayForEach(recipient, service_accounts) {
            cJSON *expires = cJSON_GetObjectItemCaseSensitive(recipient, "expires");
            if (expires == NULL || !cJSON_IsString(expires)) {
                continue;
            }
            struct tm expiry = parse_date_str(expires->valuestring);

            if (is_not_expired_tm(current_time, expiry) == 0) {
                continue;
            }

            cJSON *to = cJSON_GetObjectItemCaseSensitive(recipient, "to");
            if (to == NULL || !cJSON_IsString(to)) {
                continue;
            }

            recipients = curl_slist_append(recipients, to->valuestring);
            
        }

        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_cb);
        curl_easy_setopt(curl, CURLOPT_READDATA, (void *)&upload_ctx);
        
        // Allow RCPT TO command to fail for some recipients
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT_ALLOWFAILS, 1L);
        
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s (code: %d)\n", curl_easy_strerror(res), res);
            
            // If curl fails with VRFY error, try Python fallback
            if (res == CURLE_WEIRD_SERVER_REPLY) {
                fprintf(stderr, "Attempting fallback using Python SMTP wrapper...\n");
                
                // Write body to temporary file
                char temp_body_path[256];
                snprintf(temp_body_path, sizeof(temp_body_path), "/tmp/dlogger_mail_%d.txt", (int)getpid());
                
                FILE *body_file = fopen(temp_body_path, "w");
                if (body_file) {
                    for (size_t i = 0; i < payload_lines && payload_text[i] != NULL; i++) {
                        fprintf(body_file, "%s\n", payload_text[i]);
                    }
                    fclose(body_file);
                    
                    // Build recipients array safely
                    char recipients_arg[2048];
                    int offset = snprintf(recipients_arg, sizeof(recipients_arg), "[\"");
                    int first = 1;
                    cJSON *rcpt = NULL;
                    cJSON_ArrayForEach(rcpt, service_accounts) {
                        cJSON *to = cJSON_GetObjectItemCaseSensitive(rcpt, "to");
                        cJSON *expires = cJSON_GetObjectItemCaseSensitive(rcpt, "expires");
                        
                        if (to && cJSON_IsString(to) && expires && cJSON_IsString(expires)) {
                            struct tm expiry = parse_date_str(expires->valuestring);
                            if (is_not_expired_tm(current_time, expiry) == 0) {
                                continue;
                            }
                            
                            if (!first) {
                                offset += snprintf(recipients_arg + offset, sizeof(recipients_arg) - offset, "\",\"");
                            }
                            offset += snprintf(recipients_arg + offset, sizeof(recipients_arg) - offset, "%s", to->valuestring);
                            first = 0;
                            
                            if (offset >= sizeof(recipients_arg) - 10) {
                                fprintf(stderr, "Warning: Recipients list too long, truncating\n");
                                break;
                            }
                        }
                    }
                    snprintf(recipients_arg + offset, sizeof(recipients_arg) - offset, "\"]");
                    
                    // Execute Python fallback - check multiple locations
                    const char* script_locations[] = {
                        "/usr/libexec/dlogger/send_mail.py",
                        "/usr/lib/dlogger/send_mail.py",
                        "/opt/dlogger/bin/send_mail.py",
                        NULL
                    };
                    
                    char script_path[256] = {0};
                    int script_found = 0;
                    for (int i = 0; script_locations[i] != NULL; i++) {
                        if (access(script_locations[i], X_OK) == 0) {
                            strcpy(script_path, script_locations[i]);
                            script_found = 1;
                            break;
                        }
                    }
                    
                    if (!script_found) {
                        fprintf(stderr, "Error: send_mail.py not found in standard locations\n");
                        fprintf(stderr, "Checked: /usr/libexec/dlogger/, /usr/lib/dlogger/, /opt/dlogger/bin/\n");
                        result = 1;
                    } else {
                        char cmd[4096];
                        snprintf(cmd, sizeof(cmd), 
                            "DLOGGER_SMTP_PASSWORD='%s' python3 '%s' '%s' '%s' '%s'",
                            password, 
                            script_path,
                            file_path,
                            recipients_arg,
                            temp_body_path);
                        
                        int ret = system(cmd);
                        if (ret == 0) {
                            fprintf(stderr, "Python SMTP fallback succeeded\n");
                            result = 0;
                        } else {
                            fprintf(stderr, "Python SMTP fallback failed with exit code %d\n", ret);
                            result = 1;
                        }
                    }
                    
                    unlink(temp_body_path);
                } else {
                    result = 1;
                }
            } else {
                result = 1;
            }
        }
        
        curl_easy_cleanup(curl);
        curl_slist_free_all(recipients);
    }

    if (payload_text != NULL) {
        for (size_t i = 0; payload_text[i] != NULL; i++) {
            free(payload_text[i]);
        }
        free(payload_text);
    }

    if (contents != NULL) {
        for (size_t i = 0; i < contents_count; i++) {
            free(contents[i]);
        }
        free(contents);
    }

free_json:
    cJSON_Delete(json);

    return result;
}
