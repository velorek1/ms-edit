/* Linux terminal input and ANSI rendering. */
#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <time.h>
#include <locale.h>
#include <errno.h>

Cell screen[SCREEN_MAXCOLS*SCREEN_MAXROWS];
static Cell previous[SCREEN_MAXCOLS*SCREEN_MAXROWS];
static int previous_valid;
static volatile sig_atomic_t resize_pending;
int screen_rows=25,screen_cols=80;
MouseEvent mouse_event;
unsigned char *platform_paste_data;
unsigned long platform_paste_length;
unsigned long platform_ticks(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC,&now);
    return (unsigned long)now.tv_sec*1000UL+(unsigned long)now.tv_nsec/1000000UL;
}
static struct termios saved;
static int active;

static void window_signal(int signum) { (void)signum;resize_pending=1; }
static void dimensions(void)
{
    struct winsize ws;
    if (ioctl(1,TIOCGWINSZ,&ws)==0 && ws.ws_row && ws.ws_col) {
        screen_rows=ws.ws_row>SCREEN_MAXROWS?SCREEN_MAXROWS:ws.ws_row;
        screen_cols=ws.ws_col>SCREEN_MAXCOLS?SCREEN_MAXCOLS:ws.ws_col;
    }
    previous_valid=0;resize_pending=0;
}

static void signal_exit(int signum)
{
    static const char reset[]="\033[?2004l\033[?1002l\033[?1006l\033[0m\033[?25h\033[?1049l";
    ssize_t ignored;
    if (active) {
        tcsetattr(0,TCSANOW,&saved);
        ignored=write(1,reset,sizeof(reset)-1);
        (void)ignored;
    }
    _exit(128+signum);
}

int platform_init(void)
{
    struct termios t;
    struct sigaction action;
    if (!isatty(0) || tcgetattr(0,&saved)!=0) return 0;
    setlocale(LC_CTYPE,"");dimensions();
    memset(&action,0,sizeof(action));action.sa_handler=window_signal;
    sigemptyset(&action.sa_mask);sigaction(SIGWINCH,&action,NULL);
    t=saved;
    t.c_lflag &= (tcflag_t)~(ICANON|ECHO|ISIG|IEXTEN);
    t.c_iflag &= (tcflag_t)~(IXON|ICRNL|INLCR|ISTRIP|BRKINT|PARMRK);
    t.c_oflag &= (tcflag_t)~OPOST;
    t.c_cc[VMIN]=1; t.c_cc[VTIME]=0;
    if (tcsetattr(0,TCSANOW,&t)!=0) return 0;
    active=1;
    signal(SIGTERM,signal_exit);signal(SIGHUP,signal_exit);signal(SIGINT,signal_exit);
    fputs("\033[?1049h\033[2J\033[?25l\033[?1002h\033[?1006h\033[?2004h",stdout);
    atexit(platform_done);
    return 1;
}

void platform_done(void)
{
    if (active) {
        tcsetattr(0,TCSANOW,&saved);
        fputs("\033[?2004l\033[?1002l\033[?1006l\033[0m\033[?25h\033[?1049l",stdout); fflush(stdout);
        active=0;
    }
}

static void glyph(unsigned c)
{
    /* CP437 border/control glyphs; other high bytes use generated UTF-8 table. */
    static const char *const high[128]={
#include "cp437.h"
    };
    if (c==CELL_CONTINUATION) return;
    if (c>=CELL_UNICODE) {
        unsigned char bytes[4];unsigned n=utf8_encode(c-CELL_UNICODE,bytes);
        fwrite(bytes,1,n,stdout);
    }
    else if (c>=128 && c<256) fputs(high[c-128],stdout);
    else if (c>=32 && c<127) putchar(c);
    else if (c==24) fputs("\342\206\221",stdout);
    else if (c==25) fputs("\342\206\223",stdout);
    else if (c==26) fputs("\342\206\222",stdout);
    else if (c==27) fputs("\342\206\220",stdout);
    else putchar('.');
}

void platform_present(int x,int y,int insert)
{
    static const int colors[8]={0,4,2,6,1,5,3,7};
    int row,col,attr=-1,a,dirty,changed=0;
    unsigned long index;
    (void)insert;
    for (row=0;row<screen_rows;row++) {
        dirty=!previous_valid;
        for (col=0;col<screen_cols && !dirty;col++) {
            index=(unsigned long)row*screen_cols+col;
            dirty=screen[index].ch!=previous[index].ch || screen[index].attr!=previous[index].attr;
        }
        if (!dirty) continue;
        if (!changed) { fputs("\033[?25l",stdout);changed=1; }
        printf("\033[%d;1H",row+1);
        for (col=0;col<screen_cols;col++) {
            index=(unsigned long)row*screen_cols+col;a=screen[index].attr;
            if (a!=attr) {
                printf("\033[%d;%dm",(a&8?90:30)+colors[a&7],(a&128?100:40)+colors[(a>>4)&7]);attr=a;
            }
            glyph(screen[index].ch);previous[index]=screen[index];
        }
    }
    previous_valid=1;
    if (x>=0 && x<screen_cols && y>=0 && y<screen_rows) printf("\033[%d;%dH\033[?25h",y+1,x+1);
    else fputs("\033[?25l",stdout);
    fflush(stdout);
}

static int queued_byte=-1;
static int get_byte(int timeout)
{
    fd_set fds;struct timeval tv;unsigned char c;int result;
    if (queued_byte>=0) { result=queued_byte;queued_byte=-1;return result; }
    for (;;) {
        if (resize_pending) return -2;
        FD_ZERO(&fds);FD_SET(0,&fds);tv.tv_sec=timeout<0?0:timeout/1000;tv.tv_usec=timeout<0?0:(timeout%1000)*1000;
        result=select(1,&fds,NULL,NULL,timeout<0?NULL:&tv);
        if (result<0 && errno==EINTR) continue;
        if (result<=0) return -1;
        result=(int)read(0,&c,1);
        if (result<0 && errno==EINTR) continue;
        return result==1?c:-1;
    }
}

static int get_sequence_byte(int timeout)
{
    int c;
    do { c=get_byte(timeout);if (c==-2) dimensions(); } while (c==-2);
    return c;
}

static int paste_append(unsigned char byte,unsigned long *capacity)
{
    unsigned char *grown;
    unsigned long next;
    if (platform_paste_length==*capacity) {
        next=*capacity?*capacity*2:4096;
        if (next>16777216UL) return 0;
        grown=(unsigned char *)realloc(platform_paste_data,(size_t)next);
        if (!grown) return 0;
        platform_paste_data=grown;*capacity=next;
    }
    platform_paste_data[platform_paste_length++]=byte;return 1;
}
static int read_paste(void)
{
    static const unsigned char end[]="\033[201~";
    unsigned matched=0,i;unsigned long capacity=0;int c,failed=0;
    free(platform_paste_data);platform_paste_data=NULL;platform_paste_length=0;
    for (;;) {
        c=get_byte(1000);
        if (c==-2) { dimensions();continue; }
        if (c<0) { failed=1;break; }
        if (c==end[matched]) { if (++matched==6) break;continue; }
        for (i=0;i<matched;i++) if (!failed && !paste_append(end[i],&capacity)) failed=1;
        matched=0;
        if (c==end[0]) matched=1;
        else if (!failed && !paste_append((unsigned char)c,&capacity)) failed=1;
    }
    if (failed) { free(platform_paste_data);platform_paste_data=NULL;platform_paste_length=0;return K_PASTE_ERROR; }
    return K_PASTE_DATA;
}

static int mouse_input(int button,int x,int y,int release)
{
    int mask,buttons=mouse_event.buttons;
    mouse_event.x=x-1;mouse_event.y=y-1;
    if (button&64) return (button&1)?K_WHEEL_DOWN:K_WHEEL_UP;
    mask=(button&3)==0?1:((button&3)==2?2:4);
    if ((button&3)==3) buttons=0;
    else if (release) buttons&=~mask;
    else buttons|=mask;
    mouse_event.pressed=buttons&~mouse_event.buttons;
    mouse_event.released=mouse_event.buttons&~buttons;
    mouse_event.buttons=buttons;return K_MOUSE;
}

int platform_key(void)
{
    char seq[32];
    int c,n=0,k=0,num=0,mod=1,mx,my,button,other;
    unsigned char bytes[4];unsigned count,want;unsigned long code;
    /* Do not consume queued text while a too-small terminal hides the editor. */
    while ((screen_cols<75 || screen_rows<24) && !resize_pending) {
        struct timeval wait;wait.tv_sec=0;wait.tv_usec=50000;
        select(0,NULL,NULL,NULL,&wait);
    }
    c=get_byte(-1);
    if (c==-2) { dimensions();return K_RESIZE; }
    if (c>=194 && c<=244) {
        bytes[0]=(unsigned char)c;want=c<224?2:(c<240?3:4);
        for (count=1;count<want;count++) {
            n=get_sequence_byte(100);
            if (n==-2) { dimensions();return K_RESIZE; }
            if (n<128 || n>191) { if (n>=0) queued_byte=n;break; }
            bytes[count]=(unsigned char)n;
        }
        if (utf8_decode(bytes,count,&code)) return (int)(K_UNICODE|code);
        return c;
    }
    if (c<0) return K_ALT|'x';
    if (c==127) return 8;
    if (c!=27) return c;
    c=get_sequence_byte(40);
    if (c==-2) { dimensions();return K_RESIZE; }
    if (c<0) return 27;
    if (c!='[' && c!='O') return K_ALT|c;
    do {
        c=get_sequence_byte(40);
        if (c==-2) { dimensions();return K_RESIZE; }
        if (c<0 || n>=30) return 0;
        seq[n++]=(char)c;
    } while (!((c>='A' && c<='Z') || (c>='a' && c<='z') || c=='~'));
    seq[n]=0;
    if (seq[0]=='<' && (c=='M' || c=='m') && sscanf(seq+1,"%d;%d;%d",&button,&mx,&my)==3)
        return mouse_input(button,mx,my,c=='m');
    if (n==1 && c=='M') {
        button=get_sequence_byte(40);mx=get_sequence_byte(40);my=get_sequence_byte(40);
        if (button>=32 && mx>=33 && my>=33) return mouse_input(button-32,mx-32,my-32,0);
        return 0;
    }
    if (seq[0]=='[' && n==2 && c>='A' && c<='E') return K_F1+c-'A';
    if (seq[0]>='0' && seq[0]<='9') sscanf(seq,"%d;%d",&num,&mod);
    if (c=='u' || (c=='~' && num==27 && sscanf(seq,"%d;%d;%d",&num,&mod,&other)==3)) {
        if (c=='~') num=other;
        if (num<0 || num>0x10ffff) return 0;
        if (num>=128) return (int)(K_UNICODE|num);
        if ((mod-1)&4) return num&31;
        return ((mod-1)&2)?K_ALT|num:num;
    }
    if (c=='~' && num==200) return read_paste();
    switch(c) {
    case 'Z':return K_SHIFT|9;
    case 'A':k=K_UP;break; case 'B':k=K_DOWN;break;
    case 'C':k=K_RIGHT;break; case 'D':k=K_LEFT;break;
    case 'H':k=K_HOME;break; case 'F':k=K_END;break;
    case 'P':k=K_F1;break; case 'Q':k=K_F2;break;
    case 'R':k=K_F3;break; case 'S':k=K_F4;break;
    case '~':switch(num) {
        case 1:case 7:k=K_HOME;break; case 4:case 8:k=K_END;break;
        case 2:k=K_INS;break; case 3:k=K_DEL;break;
        case 5:k=K_PGUP;break; case 6:k=K_PGDN;break;
        case 11:k=K_F1;break; case 12:k=K_F2;break; case 13:k=K_F3;break;
        case 14:k=K_F4;break; case 15:k=K_F5;break; case 17:k=K_F6;break;
        case 18:k=K_F7;break; case 19:k=K_F8;break;
        case 20:k=K_F9;break; case 21:k=K_F10;break;
        } break;
    }
    if ((mod-1)&1) k|=K_SHIFT;
    if ((mod-1)&2) k|=K_ALT;
    if ((mod-1)&4) k|=K_CTRL;
    return k;
}

void platform_beep(void) { putchar(7); fflush(stdout); }
