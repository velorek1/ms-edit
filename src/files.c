#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
/* Linux directory enumeration. */
#include "files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static int compare(const char *a,const char *b)
{
    int x,y;
    do { x=toupper((unsigned char)*a++);y=toupper((unsigned char)*b++); }
    while (x && x==y);
    return x-y;
}
int fs_absolute(char *out,const char *path)
{
    char *resolved=realpath(path,NULL);
    if (!resolved) return ED_NOTFOUND;
    if (strlen(resolved)>=ED_PATH) { free(resolved);return ED_RANGE; }
    strcpy(out,resolved);free(resolved);return ED_OK;
}
int fs_cwd(char *path) { return getcwd(path,ED_PATH)!=NULL?ED_OK:ED_IO; }
int fs_join(char *out,const char *directory,const char *name)
{
    char work[ED_PATH];
    size_t n=strlen(directory),m=strlen(name);
    int absolute=name[0]=='/';
    if (absolute) { if (m>=ED_PATH) return ED_RANGE;strcpy(out,name);return ED_OK; }
    if (n+m+2>ED_PATH) return ED_RANGE;
    strcpy(work,directory);
    if (n && work[n-1]!='/') {
        work[n++]='/';
    }
    strcpy(work+n,name);strcpy(out,work);return ED_OK;
}
int fs_split(const char *path,char *directory,char *name)
{
    const char *p,*last=NULL;
    size_t n;
    if (strlen(path)>=ED_PATH) return ED_RANGE;
    for (p=path;*p;p++) if (*p=='/') last=p;
    if (last) {
        n=(size_t)(last-path)+1;memcpy(directory,path,n);directory[n]=0;
        strcpy(name,last+1);
    } else { if (fs_cwd(directory)) return ED_IO;strcpy(name,path); }
    return ED_OK;
}
int fs_isdir(const char *path)
{
    struct stat st;
    return stat(path,&st)==0 && (st.st_mode&S_IFMT)==S_IFDIR;
}
/* DOS-style ASCII case folding. *.* includes extensionless names. */
int fs_match(const char *pattern,const char *name)
{
    const char *star=NULL,*retry=NULL;
    if (!strcmp(pattern,"*.*")) pattern="*";
    while (*name) {
        if (*pattern=='?' || toupper((unsigned char)*pattern)==toupper((unsigned char)*name)) {
            pattern++;name++;
        } else if (*pattern=='*') { star=pattern++;retry=name; }
        else if (star) { pattern=star+1;name=++retry; }
        else return 0;
    }
    while (*pattern=='*') pattern++;
    if (!strcmp(pattern,".*")) return 1;
    return !*pattern;
}
void fs_free(FsList *list)
{
    FsEntry *p=list->head,*next;
    while (p) { next=p->next;free(p);p=next; }
    list->head=NULL;list->count=0;
}
const FsEntry *fs_at(const FsList *list,unsigned index)
{
    const FsEntry *p=list->head;
    while (p && index--) p=p->next;
    return p;
}
static int add(FsList *list,const char *name,int directory)
{
    FsEntry *p,**link;
    if (strlen(name)>=ED_NAME || list->count==65535U) return ED_LIMIT;
    p=(FsEntry *)malloc(sizeof(*p));if (!p) return ED_NOMEM;
    strcpy(p->name,name);p->directory=directory;
    link=&list->head;
    while (*link && compare((*link)->name,name)<0) link=&(*link)->next;
    p->next=*link;*link=p;list->count++;return ED_OK;
}
int fs_list(FsList *list,const char *directory,const char *pattern,int folders)
{
    FsList temp;
    int e=0,isdir;
    char path[ED_PATH];
    DIR *dir;
    struct dirent *found;
    temp.head=NULL;temp.count=0;
    if (!fs_isdir(directory)) return ED_NOTFOUND;
    dir=opendir(directory);if (!dir) return ED_IO;
    while ((found=readdir(dir))!=NULL) {
        if (!strcmp(found->d_name,".")) continue;
        if (fs_join(path,directory,found->d_name)) { e=ED_RANGE;break; }
        isdir=fs_isdir(path);
        if (isdir==folders && (isdir || fs_match(pattern,found->d_name)))
            e=add(&temp,found->d_name,isdir);
        if (e) break;
    }
    closedir(dir);
    if (e) { fs_free(&temp);return e; }
    fs_free(list);*list=temp;return ED_OK;
}
