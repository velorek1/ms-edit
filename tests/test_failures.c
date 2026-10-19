/* Host fault injection: real filesystem, selected failing calls only. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
static FILE *fail_read_target;
static size_t test_read(void *p,size_t size,size_t n,FILE *f)
{
    if (f==fail_read_target) { errno=EIO;return 0; }
    return fread(p,size,n,f);
}
static int rename_count,fail_rename,fail_restore,fail_write,fail_close,fail_alloc;
static int test_rename(const char *a,const char *b)
{
    rename_count++;
    if (rename_count==fail_rename || (fail_restore && rename_count==3)) {
        errno=EACCES;return -1;
    }
    return rename(a,b);
}
static size_t test_write(const void *p,size_t size,size_t n,FILE *f)
{
    if (fail_write) { if (fail_write==2) fwrite(p,1,1,f);errno=ENOSPC;return 0; }
    return fwrite(p,size,n,f);
}
static int test_close(FILE *f)
{
    int result=fclose(f);
    if (fail_close) { fail_close=0;errno=ENOSPC;return EOF; }
    return result;
}
static void *test_alloc(size_t n) { return fail_alloc?NULL:malloc(n); }
#define rename test_rename
#define fread test_read
#define fwrite test_write
#define fclose test_close
#define malloc test_alloc
#include "../src/buffer.c"
#undef rename
#undef fread
#undef fwrite
#undef fclose
#undef malloc
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static void contents(const char *path,const char *s)
{
    char bytes[32];FILE *f=fopen(path,"rb");size_t n;
    CHECK(f!=NULL);n=fread(bytes,1,sizeof(bytes),f);fclose(f);
    CHECK(n==strlen(s) && !memcmp(bytes,s,n));
}
int main(void)
{
    Buffer b;FILE *f;struct stat st;int mode;
    CHECK(mkdir("build/faults",0700)==0 || errno==EEXIST);
    CHECK(chdir("build/faults")==0);
    for (mode=0;mode<7;mode++) {
        remove("FILE.TXT");remove("ER000.BAK");remove("ER000.TMP");
        f=fopen("FILE.TXT","wb");CHECK(f!=NULL);fputs("old",f);fclose(f);
        buf_init(&b);CHECK(buf_replace(&b,0,0,(ConstBytePtr)"new",3)==0);
        b.binary_width=16;
        strcpy(b.path,"prior.txt");
        rename_count=0;fail_write=mode==0;fail_close=mode==1;
        fail_rename=mode==2?1:(mode==3 || mode==4?2:0);fail_restore=mode==4;
        if (mode==5) CHECK(chmod("FILE.TXT",0444)==0);
        if (mode==6) CHECK(chmod("FILE.TXT",0600)==0);
        CHECK(buf_save(&b,"FILE.TXT")== (mode==6?ED_OK:mode==5?ED_READONLY:mode==4?ED_RECOVERY:ED_IO));
        if (mode<6) CHECK(b.modified && !strcmp(b.path,"prior.txt"));
        if (mode==4) { contents("ER000.BAK","old");contents("ER000.TMP","new"); }
        else contents("FILE.TXT",mode==6?"new":"old");
        if (mode==6) { CHECK(stat("FILE.TXT",&st)==0);CHECK((st.st_mode&0777)==0600); }
        chmod("FILE.TXT",0600);
        buf_free(&b);
    }
    fail_write=fail_close=fail_rename=fail_restore=0;
    buf_init(&b);CHECK(buf_replace(&b,0,0,(ConstBytePtr)"old",3)==0);
    b.modified=0;fail_alloc=1;
    { unsigned char bytes[5000];memset(bytes,'x',sizeof(bytes));
      CHECK(buf_replace(&b,0,3,bytes,sizeof(bytes))==ED_NOMEM); }
    CHECK(buf_len(&b)==3 && buf_get(&b,0)=='o' && !b.modified);
    fail_alloc=0;buf_free(&b);
    /* Disk append and allocation failures cannot change the old piece list. */
    {
        unsigned char bytes[4096];int j;
        memset(bytes,'x',sizeof(bytes));buf_init(&b);
        for (j=0;j<32;j++) CHECK(buf_replace(&b,buf_len(&b),0,bytes,sizeof(bytes))==ED_OK);
        CHECK(b.store!=NULL);b.modified=0;
        fail_write=1;
        CHECK(buf_replace(&b,65534,5,(ConstBytePtr)"new",3)==ED_IO);
        fail_write=0;
        CHECK(buf_len(&b)==131072UL && buf_get(&b,65534)=='x' && !b.modified);
        fail_alloc=1;
        CHECK(buf_replace(&b,65534,5,(ConstBytePtr)"new",3)==ED_NOMEM);
        fail_alloc=0;
        CHECK(buf_len(&b)==131072UL && buf_get(&b,65534)=='x' && !b.modified);
        CHECK(buf_replace(&b,65534,5,(ConstBytePtr)"new",3)==ED_OK);
        CHECK(buf_get(&b,65534)=='n');b.modified=0;
        fail_write=2;
        CHECK(buf_replace(&b,0,10,(ConstBytePtr)"new",3)==ED_IO);
        fail_write=0;
        CHECK(buf_len(&b)==131070UL && buf_get(&b,0)=='x' && !b.modified);
        fail_read_target=b.store->file;b.store->cache_used=0;
        CHECK(buf_save(&b,"FILE.TXT")==ED_IO);
        contents("FILE.TXT","new");
        CHECK(buf_io_error(&b)==ED_IO);
        fail_read_target=NULL;
        CHECK(buf_get(&b,0)=='x' && !buf_io_error(&b));
        buf_free(&b);
    }
    remove("FILE.TXT");remove("ER000.BAK");remove("ER000.TMP");
    CHECK(chdir("../..")==0);CHECK(rmdir("build/faults")==0);
    puts("PASS: write/close/rename/rollback failures, readonly, permissions, allocation atomicity");
    return 0;
}
