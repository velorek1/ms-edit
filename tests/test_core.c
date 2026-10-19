#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)

static void check_bytes(const Buffer *b,const unsigned char *s,unsigned long n)
{
    unsigned long i;
    CHECK(buf_len(b)==n);
    for (i=0;i<n;i++) CHECK(buf_get(b,i)==s[i]);
}

static void basic(void)
{
    Buffer b;
    unsigned long found;
    buf_init(&b);
    CHECK(buf_get(&b,0)==-1);
    CHECK(buf_replace(&b,0,0,(ConstBytePtr)"alpha\r\nbeta\n",12)==0);
    CHECK(buf_line_number(&b,12)==3);
    CHECK(buf_line_end(&b,0)==5);
    CHECK(buf_next_line(&b,0)==7);
    CHECK(buf_prev_line(&b,7)==0);
    CHECK(buf_line_start(&b,9)==7);
    CHECK(buf_find(&b,0,(ConstBytePtr)"BETA",4,0,1,&found)==0 && found==7);
    CHECK(buf_find(&b,0,(ConstBytePtr)"BETA",4,1,1,&found)==ED_NOTFOUND);
    CHECK(buf_find(&b,0,(ConstBytePtr)"eta",3,0,1,&found)==ED_NOTFOUND);
    CHECK(buf_replace(&b,5,2,(ConstBytePtr)"-",1)==0);
    check_bytes(&b,(const unsigned char *)"alpha-beta\n",11);
    b.readonly=1;
    CHECK(buf_replace(&b,0,1,NULL,0)==ED_READONLY);
    check_bytes(&b,(const unsigned char *)"alpha-beta\n",11);
    b.readonly=0;
    CHECK(buf_replace(&b,12,0,NULL,0)==ED_RANGE);
    CHECK(buf_replace(&b,0,12,NULL,0)==ED_RANGE);
    CHECK(buf_replace(&b,0,1,NULL,1)==ED_RANGE);
    CHECK(buf_replace(&b,0,0,(ConstBytePtr)"x",ED_MAX_BYTES+1)==ED_LIMIT);
    check_bytes(&b,(const unsigned char *)"alpha-beta\n",11);
    buf_free(&b);
}

static void tabs_binary(void)
{
    Buffer b;
    buf_init(&b);
    CHECK(buf_replace(&b,0,0,(ConstBytePtr)"a\tb\r\n",6)==0);
    CHECK(buf_column(&b,2,8)==8);
    CHECK(buf_at_column(&b,0,8,8)==2);
    CHECK(buf_at_column(&b,0,7,8)==1);
    CHECK(buf_at_column(&b,0,20,8)==3);
    b.binary_width=2;
    CHECK(buf_line_start(&b,3)==2);
    CHECK(buf_line_end(&b,3)==4);
    CHECK(buf_next_line(&b,4)==6);
    CHECK(buf_line_number(&b,5)==3);
    CHECK(buf_column(&b,1,8)==1);
    buf_free(&b);
}

static void boundary_and_io(void)
{
    Buffer b,c;
    BytePtr bytes;
    unsigned long i,n=150000UL;
    FILE *f;
    buf_init(&b);buf_init(&c);
    bytes=ed_alloc(n);CHECK(bytes!=NULL);
    for (i=0;i<n;i++) bytes[i]=(unsigned char)(i*17UL);
    CHECK(buf_replace(&b,0,0,bytes,n)==0);
    CHECK(buf_replace(&b,65534UL,4,(ConstBytePtr)"CROSS-SEGMENT",13)==0);
    CHECK(buf_len(&b)==n+9);
    for (i=0;i<65534UL;i++) CHECK(buf_get(&b,i)==bytes[i]);
    for (i=0;i<13;i++) CHECK(buf_get(&b,65534UL+i)=="CROSS-SEGMENT"[i]);
    CHECK(buf_get(&b,65547UL)==bytes[65538UL]);
    b.binary_width=32; /* byte-exact roundtrip is a binary-mode contract */
    CHECK(buf_save(&b,"build/ROUND.BIN")==0);
    CHECK(!b.modified);
    CHECK(buf_load(&c,"build/ROUND.BIN",0,32)==0);
    CHECK(c.binary_width==32 && buf_len(&c)==buf_len(&b));
    for (i=0;i<buf_len(&b);i++) CHECK(buf_get(&b,i)==buf_get(&c,i));
    CHECK(buf_replace(&b,0,buf_len(&b),(ConstBytePtr)"\0\x1a\r\n\xff",5)==0);
    CHECK(buf_save(&b,b.path)==0);
    CHECK(buf_load(&c,b.path,1,0)==0);
    check_bytes(&c,(const unsigned char *)"\0\x1a\r\n\xff",5);
    CHECK(buf_save(&c,c.path)==ED_READONLY);
    CHECK(buf_load(&c,"build/NOFILE.XXX",0,0)==ED_NOTFOUND);
    check_bytes(&c,(const unsigned char *)"\0\x1a\r\n\xff",5);
    /* No changes to buffer state when staging a save fails. */
    b.modified=1;
    CHECK(buf_save(&b,"build/nonexistent-dir/X.TXT")==ED_IO);
    CHECK(b.modified);
    f=fopen("build/ROUND.BIN","rb");CHECK(f!=NULL);CHECK(fgetc(f)==0);fclose(f);
    remove("build/ROUND.BIN");ed_free(bytes);buf_free(&b);buf_free(&c);
}

static void text_save_rules(void)
{
    static const char *input[]={"","abc","a\nb\n","a\rb\r","a\tb","a\r\nb\r\n"};
    static const char *expected[]={"","abc\r\n","a\r\nb\r\n","a\rb\r\n","a\tb\r\n","a\r\nb\r\n"};
    Buffer b,c;unsigned i;
    for (i=0;i<sizeof(input)/sizeof(input[0]);i++) {
        buf_init(&b);buf_init(&c);
        CHECK(buf_replace(&b,0,0,(ConstBytePtr)input[i],(unsigned long)strlen(input[i]))==ED_OK);
        CHECK(buf_save(&b,"build/TEXT.TXT")==ED_OK);
        CHECK(buf_load(&c,"build/TEXT.TXT",0,0)==ED_OK);
        check_bytes(&c,(const unsigned char *)expected[i],(unsigned long)strlen(expected[i]));
        buf_free(&b);buf_free(&c);
    }
    remove("build/TEXT.TXT");
}

static void randomized(void)
{
    Buffer b;
    unsigned char model[20000],add[80];
    unsigned long n=0,p,del,count,i;
    unsigned step;
    buf_init(&b);srand(2026);
    for (step=0;step<8000;step++) {
        p=(unsigned long)rand()%(n+1);
        del=(unsigned long)rand()%41UL;
        if (del>n-p) del=n-p;
        count=(unsigned long)rand()%80UL;
        if (n-del+count>sizeof(model)) count=0;
        for (i=0;i<count;i++) add[i]=(unsigned char)rand();
        CHECK(buf_replace(&b,p,del,(ConstBytePtr)add,count)==0);
        memmove(model+p+count,model+p+del,(size_t)(n-p-del));
        memcpy(model+p,add,(size_t)count);n=n-del+count;
        check_bytes(&b,model,n);
    }
    buf_free(&b);
}

int main(void)
{
    basic();tabs_binary();boundary_and_io();text_save_rules();randomized();
    printf("PASS: buffer, search, CRLF, tabs, 64 KiB boundary, binary I/O, 8000 random edits (%u checks)\n",checks);
    return 0;
}
