#include "cli.h"
#include <string.h>
#include <ctype.h>
void cli_parse(CliOptions *o,int argc,char **argv)
{
    int i,c,ro=0,options=1;
    unsigned width=0;
    const char *p;
    unsigned long n;
    memset(o,0,sizeof(*o));
    for (i=1;i<argc;i++) {
        p=argv[i];
        if (options && !strcmp(p,"--")) { options=0;continue; }
        if (options && (!strcmp(p,"--help") || !strcmp(p,"-h"))) { o->help=1;return; }
        if (options && *p=='-' && p[1]) {
            p++;
            if (*p>='0' && *p<='9') {
                n=0;
                while (*p>='0' && *p<='9') {
                    if (n<=1021UL) n=n*10UL+(unsigned)(*p-'0');
                    p++;
                }
                if (*p) { o->help=1;return; }
                width=(unsigned)(n>1021UL?1021UL:n);
                continue;
            }
            c=toupper((unsigned char)*p);
            switch(c) {
            case 'B':o->mono=1;break;
            case 'R':ro=1;break;
            case 'W':ro=0;break;
            default:o->help=1;return;
            }
        } else if (o->count<CLI_MAX_FILES) {
            o->files[o->count].path=argv[i];
            o->files[o->count].readonly=ro;
            o->files[o->count].width=width;
            o->count++;
        }
    }
}
