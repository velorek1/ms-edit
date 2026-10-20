#define _POSIX_C_SOURCE 200809L
#include "buffer.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

BytePtr ed_alloc(unsigned long n)
{
    if (!n) n=1;
    return (BytePtr)malloc((size_t)n);
}

void ed_free(BytePtr p)
{
    free(p);
}

/* Large documents: append-only backing file, transactional piece lists and
 * one 4 KiB read cache. These are new structures, not recovered MS layouts. */
#define MEMORY_DOCUMENT 98304UL
#define STORE_PAGE 4096U
typedef struct Piece {
    struct Piece *next;
    unsigned long offset,length;
} Piece;
struct Store {
    FILE *file;
    Piece *pieces,*located;
    unsigned long length,located_at,cache_at;
    unsigned cache_used;
    int error;
    char path[ED_PATH];
    unsigned char cache[STORE_PAGE];
};
static void pieces_free(Piece *p)
{
    Piece *next;
    while (p) { next=p->next;free(p);p=next; }
}
static void store_free(struct Store *s)
{
    if (!s) return;
    if (s->file) fclose(s->file);
    if (*s->path) remove(s->path);
    pieces_free(s->pieces);free(s);
}
static int store_new(struct Store **result)
{
    struct Store *s;
    const char *directory;
    char prefix[ED_PATH];
    unsigned attempt;
    size_t n;
    int fd;
    s=(struct Store *)malloc(sizeof(*s));
    if (!s) return ED_NOMEM;
    memset(s,0,sizeof(*s));
    directory=getenv("TEMP");
    if (!directory || !*directory) directory=getenv("TMP");
    if (!directory || !*directory) directory=".";
    n=strlen(directory);
    if (n+14>=ED_PATH) { free(s);return ED_RANGE; }
    strcpy(prefix,directory);
    if (n && prefix[n-1]!='/') {
        prefix[n++]='/';
    }
    prefix[n]=0;
    for (attempt=0;attempt<10000U;attempt++) {
        sprintf(s->path,"%.244sEV%04u.TMP",prefix,attempt);
        fd=open(s->path,O_RDWR|O_CREAT|O_EXCL,0600);
        if (fd>=0) {
            s->file=fdopen(fd,"w+b");
            if (!s->file) { close(fd);remove(s->path);free(s);return ED_IO; }
            *result=s;return ED_OK;
        }
        if (errno!=EEXIST) { free(s);return ED_IO; }
    }
    free(s);return ED_IO;
}
static int piece_add(Piece **head,Piece **tail,unsigned long offset,unsigned long length)
{
    Piece *p;
    if (!length) return ED_OK;
    if (*tail && (*tail)->offset+(*tail)->length==offset) {
        (*tail)->length+=length;return ED_OK;
    }
    p=(Piece *)malloc(sizeof(*p));if (!p) return ED_NOMEM;
    p->offset=offset;p->length=length;p->next=NULL;
    if (*tail) (*tail)->next=p;else *head=p;
    *tail=p;return ED_OK;
}
static int piece_range(struct Store *s,Piece **head,Piece **tail,
                       unsigned long from,unsigned long to)
{
    Piece *p;
    unsigned long at=0,end,lo,hi;
    int e;
    for (p=s->pieces;p && at<to;p=p->next) {
        end=at+p->length;lo=from>at?from:at;hi=to<end?to:end;
        if (lo<hi) {
            e=piece_add(head,tail,p->offset+lo-at,hi-lo);
            if (e) return e;
        }
        at=end;
    }
    return ED_OK;
}
static int store_get(struct Store *s,unsigned long position)
{
    Piece *p;
    unsigned long at,offset,base;
    unsigned char block[STORE_PAGE];
    size_t n;
    if (position>=s->length) return -1;
    if (s->located && position>=s->located_at) { p=s->located;at=s->located_at; }
    else { p=s->pieces;at=0; }
    while (p && position>=at+p->length) { at+=p->length;p=p->next; }
    if (!p) { s->error=ED_IO;return -1; }
    s->located=p;s->located_at=at;offset=p->offset+position-at;
    if (offset<s->cache_at || offset>=s->cache_at+s->cache_used) {
        base=offset-offset%STORE_PAGE;
        clearerr(s->file);
        if (fseek(s->file,(long)base,SEEK_SET)!=0) { s->cache_used=0;s->error=ED_IO;return -1; }
        n=fread(block,1,sizeof(block),s->file);
        if (ferror(s->file) || n<=offset-base) { s->cache_used=0;s->error=ED_IO;return -1; }
        memcpy(s->cache,block,n);s->cache_used=(unsigned)n;s->cache_at=base;s->error=0;
    }
    return s->cache[(unsigned)(offset-s->cache_at)];
}
static int store_replace(struct Store *s,unsigned long at,unsigned long count,
                         ConstBytePtr bytes,unsigned long n)
{
    Piece *head=NULL,*tail=NULL;
    unsigned long done=0;
    unsigned amount;
    long end;
    int e;
    clearerr(s->file);
    if (fseek(s->file,0L,SEEK_END)!=0 || (end=ftell(s->file))<0) return ED_IO;
    /* Keep backing-file offsets within the supported signed-long range. */
    if ((unsigned long)end>2147483647UL-n) return ED_LIMIT;
    e=piece_range(s,&head,&tail,0,at);
    if (!e) e=piece_add(&head,&tail,(unsigned long)end,n);
    if (!e) e=piece_range(s,&head,&tail,at+count,s->length);
    if (e) { pieces_free(head);return e; }
    while (done<n) {
        amount=(unsigned)(n-done>STORE_PAGE?STORE_PAGE:n-done);
        if (fwrite(bytes+done,1,amount,s->file)!=amount) { e=ED_IO;break; }
        done+=amount;
    }
    if (fflush(s->file)!=0) e=ED_IO;
    if (e) { pieces_free(head);clearerr(s->file);return e; }
    /* The old pieces remain valid even if an append failed partway. */
    pieces_free(s->pieces);s->pieces=head;s->length=s->length-count+n;
    s->located=NULL;s->cache_used=0;s->error=0;return ED_OK;
}
static int store_from_buffer(const Buffer *b,struct Store **result)
{
    struct Store *s;
    unsigned char block[STORE_PAGE];
    unsigned long at=0,len=buf_len(b);
    unsigned n,i;
    int e=store_new(&s);
    if (e) return e;
    while (at<len) {
        n=(unsigned)(len-at>STORE_PAGE?STORE_PAGE:len-at);
        for (i=0;i<n;i++) block[i]=(unsigned char)buf_get(b,at+i);
        e=store_replace(s,at,0,(ConstBytePtr)block,n);
        if (e) { store_free(s);return e; }
        at+=n;
    }
    *result=s;return ED_OK;
}
int buf_io_error(const Buffer *b) { return b->store?b->store->error:0; }

void buf_init(Buffer *b)
{
    memset(b,0,sizeof(*b));b->line_cache_number=1;
    b->utf8=1;
}
void buf_free(Buffer *b) { ed_free(b->data); store_free(b->store);buf_init(b); }
unsigned long buf_len(const Buffer *b) { return b->store?b->store->length:b->cap-(b->gap_hi-b->gap_lo); }

int buf_get(const Buffer *b, unsigned long p)
{
    if (b->store) return store_get(b->store,p);
    if (p>=buf_len(b)) return -1;
    if (p>=b->gap_lo) p+=b->gap_hi-b->gap_lo;
    return b->data[p];
}

static unsigned buffer_decode(const Buffer *b,unsigned long p,unsigned long *code)
{
    unsigned char bytes[4];unsigned n=0,i;
    if (!b->utf8 || b->binary_width) { *code=(unsigned char)buf_get(b,p);return 1; }
    for (i=0;i<4 && p+i<buf_len(b);i++) bytes[n++]=(unsigned char)buf_get(b,p+i);
    i=utf8_decode(bytes,n,code);
    if (!i) { *code=n?bytes[0]:0;return 1; }
    return i;
}
unsigned long buf_char_next(const Buffer *b,unsigned long p)
{
    unsigned long code;
    if (p>=buf_len(b)) return buf_len(b);
    return p+buffer_decode(b,p,&code);
}
unsigned long buf_char_prev(const Buffer *b,unsigned long p)
{
    unsigned long q;
    if (!p) return 0;
    q=p-1;
    if (b->utf8 && !b->binary_width) while (q && (buf_get(b,q)&192)==128) q--;
    return q;
}
unsigned long buf_codepoint(const Buffer *b,unsigned long p)
{
    unsigned long code;buffer_decode(b,p,&code);return code;
}

static int reserve(Buffer *b,unsigned long required)
{
    unsigned long cap,len,i;
    BytePtr p;
    if (required>ED_MAX_BYTES) return ED_LIMIT;
    if (required<=b->cap) return ED_OK;
    /* A 4 KiB gap avoids spending 112 KiB of conventional memory on a
     * 150 KiB document. Large initial inserts reserve their actual size. */
    cap=required+4096UL;
    if (cap>ED_MAX_BYTES) cap=ED_MAX_BYTES;
    p=ed_alloc(cap);
    if (!p) return ED_NOMEM;
    len=buf_len(b);
    for (i=0;i<len;i++) p[i]=(unsigned char)buf_get(b,i);
    ed_free(b->data);
    b->data=p; b->cap=cap; b->gap_lo=len; b->gap_hi=cap;
    return ED_OK;
}

static void move_gap(Buffer *b,unsigned long p)
{
    while (b->gap_lo>p) b->data[--b->gap_hi]=b->data[--b->gap_lo];
    while (b->gap_lo<p) b->data[b->gap_lo++]=b->data[b->gap_hi++];
}

/* s must not alias b->data. Reserve before mutation: failed edits are atomic. */
int buf_replace(Buffer *b,unsigned long at,unsigned long count,
                ConstBytePtr s,unsigned long n)
{
    unsigned long len,i,cache_pos,cache_number,newlines;
    struct Store *store;
    int e;
    if (b->readonly) return ED_READONLY;
    len=buf_len(b);
    if (at>len || count>len-at || (n && !s)) return ED_RANGE;
    if (n>ED_MAX_BYTES || len-count>ED_MAX_BYTES-n) return ED_LIMIT;
    if (!count && !n) return ED_OK;
    newlines=b->newlines;
    for (i=0;i<count;i++) if (buf_get(b,at+i)=='\n') newlines--;
    for (i=0;i<n;i++) if (s[i]=='\n') newlines++;
    cache_pos=b->line_cache_pos;cache_number=b->line_cache_number;
    if (cache_number && cache_pos>=at+count) {
        for (i=0;i<count;i++) if (buf_get(b,at+i)=='\n') cache_number--;
        for (i=0;i<n;i++) if (s[i]=='\n') cache_number++;
        cache_pos=cache_pos-count+n;
    } else if (cache_pos>=at) cache_number=0;
    if (buf_io_error(b)) return ED_IO;
    if (b->store) e=store_replace(b->store,at,count,s,n);
    else if (len-count+n>MEMORY_DOCUMENT) {
        e=store_from_buffer(b,&store);
        if (e) return e;
        e=store_replace(store,at,count,s,n);
        if (e) { store_free(store);return e; }
        ed_free(b->data);b->data=NULL;b->cap=b->gap_lo=b->gap_hi=0;b->store=store;
    } else {
        e=reserve(b,len-count+n);
        if (e) return e;
    }
    if (e) return e;
    b->newlines=newlines;
    b->line_cache_pos=cache_pos;b->line_cache_number=cache_number;
    if (b->store) { b->cursor=b->anchor=at+n;b->modified=1;return ED_OK; }
    move_gap(b,at);
    b->gap_hi+=count;
    for (i=0;i<n;i++) b->data[b->gap_lo++]=s[i];
    b->cursor=b->anchor=at+n; b->modified=1;
    return ED_OK;
}

int buf_load(Buffer *b,const char *path,int readonly,unsigned width)
{
    FILE *f;
    Buffer temp;
    struct stat info;
    unsigned char block[4096];
    size_t n;
    int e=ED_OK;
    if (!path || strlen(path)>=ED_PATH) return ED_RANGE;
    buf_init(&temp);
    if (stat(path,&info)==0) {
        if (!S_ISREG(info.st_mode)) return ED_IO;
    }
    f=fopen(path,"rb");
    if (!f) return errno==ENOENT?ED_NOTFOUND:ED_IO;
    /* Allocate known file size once; incremental doubling temporarily needs
     * both the old and new blocks at once. */
    if (fstat(fileno(f),&info)==0 && info.st_size>0) {
        if ((unsigned long)info.st_size>ED_MAX_BYTES) e=ED_LIMIT;
        else if ((unsigned long)info.st_size>MEMORY_DOCUMENT) e=store_new(&temp.store);
        else {
            temp.data=ed_alloc((unsigned long)info.st_size);
            if (!temp.data) e=ED_NOMEM;
            else temp.cap=temp.gap_hi=(unsigned long)info.st_size;
        }
        if (e) { fclose(f);buf_free(&temp);return e; }
    }
    while ((n=fread(block,1,sizeof(block),f))!=0) {
        e=buf_replace(&temp,buf_len(&temp),0,(ConstBytePtr)block,(unsigned long)n);
        if (e) break;
    }
    if (ferror(f)) e=ED_IO;
    if (fclose(f)!=0 && !e) e=ED_IO;
    if (e) { buf_free(&temp); return e; }
    if (!width) {
        unsigned long position=0,code;
        unsigned char bytes[4];unsigned i,used;
        while (position<buf_len(&temp)) {
            for (i=0;i<4 && position+i<buf_len(&temp);i++) bytes[i]=(unsigned char)buf_get(&temp,position+i);
            used=utf8_decode(bytes,i,&code);
            if (!used) { temp.utf8=0;break; }
            position+=used;
        }
    }
    temp.readonly=readonly; temp.binary_width=width;
    temp.modified=0;temp.cursor=temp.anchor=0;temp.line_cache_pos=0;temp.line_cache_number=1;
    strcpy(temp.path,path);
    buf_free(b); *b=temp;
    return ED_OK;
}

/* Buffered emission permits text LF conversion without another document copy. */
static int save_byte(FILE *f,unsigned char *block,unsigned *used,int ch)
{
    block[(*used)++]=(unsigned char)ch;
    if (*used==4096U) {
        if (fwrite(block,1,*used,f)!=*used) return ED_IO;
        *used=0;
    }
    return ED_OK;
}

/* Stage in the same directory; keep a backup until the rename succeeds.
 * Never overwrite an existing .BAK. Refuse instead.
 * Power-loss durability/fsync and metadata preservation are not implemented.
 */
int buf_save(Buffer *b,const char *path)
{
    char tmp[ED_PATH],bak[ED_PATH],dir[ED_PATH],saved[ED_PATH];
    const char *slash,*q;
    FILE *f;
    struct stat info,probe;
    int fd;
    unsigned long i,len;
    unsigned char block[4096];
    unsigned j,k;
    int exists=0,e=ED_OK;
    size_t prefix;
    if (b->readonly) return ED_READONLY;
    if (!path || !*path || strlen(path)>=ED_PATH) return ED_RANGE;
    strcpy(saved,path);
    if (lstat(path,&info)==0) {
        if (!S_ISREG(info.st_mode)) return ED_IO;
        if (!(info.st_mode&0222)) return ED_READONLY;
        if (access(path,W_OK)!=0) return ED_IO;
        exists=1;
    } else if (errno!=ENOENT) return ED_IO;
    slash=NULL;
    for (q=path;*q;q++) if (*q=='/') slash=q;
    prefix=slash?(size_t)(slash-path+1):0;
    if (prefix+13>=ED_PATH) return ED_RANGE;
    memcpy(dir,path,prefix); dir[prefix]=0;
    for (k=0;k<1000;k++) {
        sprintf(tmp,"%.246sER%03u.TMP",dir,k);
        sprintf(bak,"%.246sER%03u.BAK",dir,k);
        if (!strcmp(tmp,path) || !strcmp(bak,path)) continue;
        if (stat(bak,&probe)==0) continue;
        if (errno!=ENOENT) return ED_IO;
        fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL,0666);
        if (fd>=0) break;
        if (errno!=EEXIST) return ED_IO;
    }
    if (k==1000) return ED_IO;
    f=fdopen(fd,"wb");
    if (!f) { close(fd); remove(tmp); return ED_IO; }
    len=buf_len(b);
    j=0;
    for (i=0;i<len && !e;i++) {
        int ch=buf_get(b,i);
        if (ch<0 || buf_io_error(b)) { e=ED_IO;break; }
        /* Strip only CR belonging to CRLF; preserve standalone CR bytes. */
        if (!b->binary_width && ch=='\r' && i+1<len && buf_get(b,i+1)=='\n')
            continue;
        if (!e) e=save_byte(f,block,&j,ch);
    }
    if (!e && !b->binary_width && len && buf_get(b,len-1)!='\n') {
        if (!e) e=save_byte(f,block,&j,'\n');
    }
    if (buf_io_error(b)) e=ED_IO;
    if (!e && j && fwrite(block,1,j,f)!=j) e=ED_IO;
    if (fclose(f)!=0) e=ED_IO;
    if (e) { remove(tmp); return e; }
    if (exists && chmod(tmp,info.st_mode&0777)!=0) {
        remove(tmp); return ED_IO;
    }
    if (exists && rename(path,bak)!=0) { remove(tmp); return ED_IO; }
    if (rename(tmp,path)!=0) {
        /* Retain BOTH versions if rollback fails: never discard staged edits. */
        if (exists && rename(bak,path)!=0) return ED_RECOVERY;
        remove(tmp); return ED_IO;
    }
    if (exists) remove(bak);
    strcpy(b->path,saved); b->modified=0;
    return ED_OK;
}

unsigned long buf_line_start(const Buffer *b,unsigned long p)
{
    if (p>buf_len(b)) p=buf_len(b);
    if (b->binary_width) return p-p%b->binary_width;
    while (p && buf_get(b,p-1)!='\n') p--;
    return p;
}

unsigned long buf_line_end(const Buffer *b,unsigned long p)
{
    unsigned long len=buf_len(b),q;
    if (b->binary_width) {
        q=buf_line_start(b,p)+b->binary_width;
        return q<len?q:len;
    }
    while (p<len && buf_get(b,p)!='\n') p++;
    if (p && buf_get(b,p-1)=='\r' && buf_get(b,p)=='\n') p--;
    return p;
}

unsigned long buf_next_line(const Buffer *b,unsigned long p)
{
    unsigned long len=buf_len(b);
    if (b->binary_width) {
        p=buf_line_start(b,p)+b->binary_width;
        return p<len?p:len;
    }
    while (p<len && buf_get(b,p)!='\n') p++;
    return p<len?p+1:len;
}

unsigned long buf_prev_line(const Buffer *b,unsigned long p)
{
    p=buf_line_start(b,p);
    return p?buf_line_start(b,p-1):0;
}

unsigned long buf_line_number(Buffer *b,unsigned long p)
{
    unsigned long i,n;
    if (b->binary_width) return p/b->binary_width+1;
    if (p>buf_len(b)) p=buf_len(b);
    i=b->line_cache_number?b->line_cache_pos:0;
    n=b->line_cache_number?b->line_cache_number:1;
    while (i<p) if (buf_get(b,i++)=='\n') n++;
    while (i>p) if (buf_get(b,--i)=='\n') n--;
    b->line_cache_pos=p;b->line_cache_number=n;return n;
}

unsigned long buf_column(const Buffer *b,unsigned long p,unsigned tabs)
{
    unsigned long i,col=0;
    if (!tabs) tabs=8;
    if (b->binary_width) return p%b->binary_width;
    for (i=buf_line_start(b,p);i<p;i=buf_char_next(b,i)) {
        if (buf_get(b,i)=='\t') col+=tabs-col%tabs;
        else col+=b->utf8 && !b->binary_width?utf8_width(buf_codepoint(b,i)):1;
    }
    return col;
}

unsigned long buf_at_column(const Buffer *b,unsigned long start,
                            unsigned long col,unsigned tabs)
{
    unsigned long end=buf_line_end(b,start),c=0,next;
    if (!tabs) tabs=8;
    while (start<end && c<col) {
        next=c+(b->utf8 && !b->binary_width?utf8_width(buf_codepoint(b,start)):1);
        if (!b->binary_width && buf_get(b,start)=='\t') next=c+tabs-c%tabs;
        if (next>col) break;
        c=next;start=buf_char_next(b,start);
    }
    return start;
}

static int fold(int c) { return c>='a' && c<='z'?c-'a'+'A':c; }
static int word(int c)
{ return c=='_' || (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9'); }

static int word_at(const Buffer *b,unsigned long p)
{
    if (p>=buf_len(b)) return 0;
    return b->utf8 && !b->binary_width?utf8_word(buf_codepoint(b,p)):word(buf_get(b,p));
}

int buf_find(const Buffer *b,unsigned long start,ConstBytePtr s,
             unsigned long n,int match_case,int whole_word,unsigned long *found)
{
    unsigned long len=buf_len(b),p,i;
    int x,y;
    if (!n || n>len || start>len-n) return ED_NOTFOUND;
    for (p=start;p<=len-n;p++) {
        if (whole_word && ((p && word_at(b,buf_char_prev(b,p))) || word_at(b,p+n))) continue;
        for (i=0;i<n;i++) {
            x=buf_get(b,p+i); y=s[i];
            if (!match_case) { x=fold(x); y=fold(y); }
            if (x!=y) break;
        }
        if (i==n) { *found=p; return ED_OK; }
    }
    return ED_NOTFOUND;
}

const char *ed_error(int e)
{
    switch(e) {
    case ED_OK:return "OK";
    case ED_NOMEM:return "Out of memory!";
    case ED_RANGE:return "Invalid range or filename.";
    case ED_READONLY:return "You cannot modify a read-only file.";
    case ED_NOTFOUND:return "Edit was unable to find a match or file.";
    case ED_RECOVERY:return "Recovery required: keep ERnnn.BAK and ERnnn.TMP in the target folder.";
    case ED_LIMIT:return "This prototype supports at most 256 KiB per file.";
    default:return "File operation failed. Check path, permissions and space.";
    }
}
