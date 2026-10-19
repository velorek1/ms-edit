#include "files.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    char path[ED_PATH],directory[ED_PATH],name[ED_PATH];
    FsList list;const FsEntry *p;unsigned n=0;
    list.head=NULL;list.count=0;
    assert(fs_match("*.*","NOEXT"));assert(fs_match("*.TXT","a.txt"));
    assert(!fs_match("*.TXT","a.txt.bak"));assert(fs_match("A?.TXT","ab.txt"));
    assert(fs_match("*a*b*","aaXXbY"));assert(!fs_match("A?.TXT","a.txt"));
    assert(fs_join(path,"/tmp/DIR","A.TXT")==0);
    assert(fs_split("/tmp/DIR/A*.TXT",directory,name)==0);
    assert(!strcmp(directory,"/tmp/DIR/"));assert(!strcmp(name,"A*.TXT"));
    assert(fs_join(path,"/tmp","a:b\\c.txt")==0);
    assert(!strcmp(path,"/tmp/a:b\\c.txt"));
    assert(fs_split(path,directory,name)==0);
    assert(!strcmp(directory,"/tmp/"));assert(!strcmp(name,"a:b\\c.txt"));
    assert(fs_cwd(directory)==0);assert(fs_isdir(directory));
    assert(fs_list(&list,"src","*.h",0)==0);
    assert(list.count>=4);
    for (p=list.head;p;p=p->next) { assert(!p->directory);assert(fs_match("*.h",p->name));n++; }
    assert(n==list.count);
    /* A failed refresh leaves the previously usable listing intact. */
    assert(fs_list(&list,"NO_SUCH_DIRECTORY","*.*",0)!=0);assert(n==list.count);
    fs_free(&list);assert(!list.head && !list.count);
    puts("PASS: Wildcard matching, paths, enumeration, refresh failure");return 0;
}
