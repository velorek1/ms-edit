#ifndef EDIT_FILES_H
#define EDIT_FILES_H
#include "buffer.h"
#define ED_NAME ED_PATH
typedef struct FsEntry {
    struct FsEntry *next;
    int directory;
    char name[ED_NAME];
} FsEntry;
typedef struct { FsEntry *head; unsigned count; } FsList;
int fs_absolute(char *out,const char *path);
int fs_cwd(char *path);
int fs_join(char *out,const char *directory,const char *name);
int fs_split(const char *path,char *directory,char *name);
int fs_isdir(const char *path);
int fs_match(const char *pattern,const char *name);
int fs_list(FsList *list,const char *directory,const char *pattern,int folders);
void fs_free(FsList *list);
const FsEntry *fs_at(const FsList *list,unsigned index);
#endif
