#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/reporter/reporter.h"

static time_t current_time;

time_t __wrap_time(time_t *output)
{
    if (output != NULL)
        *output = current_time;
    return current_time;
}

int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    current_time = (time_t)strtoll(argv[3], NULL, 10);
    char **files = NULL;
    size_t count = 0;
    int result = read_report_files(argv[1], argv[2], &files, &count);
    if (result != 0) {
        if (files != NULL || count != 0) {
            fprintf(stderr, "Failure left a partial report list\n");
            return 2;
        }
        return result;
    }

    for (size_t i = 0; i < count; i++) {
        printf("FILE:%s\n", files[i]);
        free(files[i]);
    }
    free(files);
    return 0;
}
