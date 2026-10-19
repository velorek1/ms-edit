#ifndef EDIT_CLI_H
#define EDIT_CLI_H
#define CLI_MAX_FILES 32
typedef struct { const char *path; int readonly; unsigned width; } FileSpec;
typedef struct {
    int mono,help,count;
    FileSpec files[CLI_MAX_FILES];
} CliOptions;
/* Linux flags; -- ends options so filenames may start with a dash. */
void cli_parse(CliOptions *options,int argc,char **argv);
#endif
