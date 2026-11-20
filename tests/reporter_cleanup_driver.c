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
    if (argc != 3)
        return 2;

    current_time = (time_t)strtoll(argv[2], NULL, 10);
    return rem_older_than_conf_range(argv[1]);
}
