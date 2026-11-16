#ifndef REPORTER_H
#define REPORTER_H

#include <stddef.h>

/*
 * Reads properties.report-properties.day-range as [near, far] UTC days ago.
 * Returns filenames in the inclusive date range. The caller frees the strings
 * and array; a successful empty result has *files == NULL and *count == 0.
 */
int read_report_files(char *config_path, const char *report_dir,
                      char ***files, size_t *count);
/* 
 * Reads content of report files.
 */
int get_content_from_report(char *file_path, char **content);

#endif