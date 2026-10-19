#include "cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"CLI FAIL line %d\n",__LINE__);exit(1); } } while(0)
int main(void)
{
    CliOptions o;
    char *args[]={"editr","A.TXT","-R","-16","B.TXT","-W","-0","C.TXT","-1025","D.TXT","-65536","E.TXT","-B"};
    char *paths[]={"editr","/tmp","/R","--","-notes.txt"};
    char *help[]={"editr","--help"};
    char *suffix[]={"editr","-16junk","X.BIN"};
    cli_parse(&o,13,args);
    CHECK(!o.help && o.count==5 && o.mono);
    CHECK(o.files[0].width==0 && !o.files[0].readonly);
    CHECK(o.files[1].width==16 && o.files[1].readonly);
    CHECK(o.files[2].width==0 && !o.files[2].readonly);
    CHECK(o.files[3].width==1021 && o.files[4].width==1021);
    cli_parse(&o,5,paths);CHECK(!o.help && o.count==3);
    CHECK(!strcmp(o.files[0].path,"/tmp") && !strcmp(o.files[2].path,"-notes.txt"));
    cli_parse(&o,2,help);CHECK(o.help);
    cli_parse(&o,3,suffix);CHECK(o.help);
    puts("PASS: Linux options, absolute paths, per-file flags, width bounds, --");
    return 0;
}
