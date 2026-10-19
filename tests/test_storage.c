#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL storage line %d: %s\n",__LINE__,#x);exit(1); } } while (0)
int main(void)
{
    Buffer b,c;FILE *f;unsigned char block[4096];unsigned i;
    unsigned long p;int expected;
    buf_init(&b);buf_init(&c);
    for (i=0;i<sizeof(block);i++) block[i]=(unsigned char)i;
    f=fopen("LARGE.BIN","wb");CHECK(f!=NULL);
    for (i=0;i<256;i++) CHECK(fwrite(block,1,sizeof(block),f)==sizeof(block));
    CHECK(fclose(f)==0);
    CHECK(buf_load(&b,"LARGE.BIN",0,70)==ED_OK);
    CHECK(b.store!=NULL && buf_len(&b)==1048576UL);
    CHECK(buf_replace(&b,65534UL,4,(ConstBytePtr)"CROSS-SEGMENT",13)==ED_OK);
    CHECK(buf_len(&b)==1048585UL);
    for (p=0;p<buf_len(&b);p++) {
        if (p<65534UL) expected=(int)(p&255);
        else if (p<65547UL) expected="CROSS-SEGMENT"[(unsigned)(p-65534UL)];
        else expected=(int)((p-9)&255);
        CHECK(buf_get(&b,p)==expected);
    }
    CHECK(buf_replace(&b,buf_len(&b),0,(ConstBytePtr)"END",3)==ED_OK);
    CHECK(buf_replace(&b,0,5,(ConstBytePtr)"",0)==ED_OK);
    CHECK(buf_save(&b,"LARGE.BIN")==ED_OK);
    CHECK(buf_load(&c,"LARGE.BIN",0,70)==ED_OK);
    CHECK(buf_len(&b)==buf_len(&c));
    for (p=0;p<buf_len(&b);p++) CHECK(buf_get(&b,p)==buf_get(&c,p));
    CHECK(!buf_io_error(&b) && !buf_io_error(&c));
    buf_free(&b);buf_free(&c);CHECK(remove("LARGE.BIN")==0);
    puts("PASS: disk-backed 1 MiB, cross-64KiB replacement, append, delete, save, reload");
    return 0;
}
