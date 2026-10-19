/* MS-EDIT Linux 0.4. Original behavior is documented separately.
 * This is new source, not recovered Microsoft source or a matching decompile.
 */
#include "buffer.h"
#include "platform.h"
#include "cli.h"
#include "files.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_DOCS 9
static Buffer docs[MAX_DOCS];
static int doc_count=1,current=0,insert_mode=1,running=1;
typedef struct {
    int document;
    unsigned long cursor,anchor,top,left,preferred_col;
    int preferred_valid;
} View;
static View views[2];
static int view_count=1,focused=0,split_y=12,pane_height=22;
static unsigned tabstop=8;
static unsigned char normal_attr=0x17,border_attr=0x07,menu_attr=0x70;
static unsigned char hot_attr=0x74,sel_attr=0x70;
static BytePtr clipboard;
static unsigned long clip_len,preferred_col;
static int preferred_valid,search_case,search_word;
static char needle[256],replacement[256];
static int cx,cy;

enum { C_NEW=1,C_OPEN,C_SAVE,C_SAVEAS,C_CLOSE,C_PRINT,C_EXIT,
       C_CUT,C_COPY,C_PASTE,C_CLEAR,C_FIND,C_NEXT,C_REPLACE,
       C_SPLIT,C_SIZE,C_UNSPLIT,C_SETTINGS,C_COLORS,C_HELP,C_ABOUT,
       C_DOC1=100 };
typedef struct { const char *label; int command; } MenuItem;
static const char *titles[]={"&File","&Edit","&Search","&View","&Options","&Help"};
static const int menux[]={3,9,15,23,29,38};
static const MenuItem filemenu[]={
    {"&New",C_NEW},{"&Open...",C_OPEN},{"&Save",C_SAVE},
    {"Save &As...",C_SAVEAS},{"&Close",C_CLOSE},
    {"&Print...",C_PRINT},{"E&xit",C_EXIT}};
static const MenuItem editmenu[]={
    {"Cu&t       Ctrl+X",C_CUT},{"&Copy      Ctrl+C",C_COPY},
    {"&Paste     Ctrl+V",C_PASTE},{"Cl&ear     Del",C_CLEAR}};
static const MenuItem searchmenu[]={
    {"&Find...",C_FIND},{"Repeat &Last Find    F3",C_NEXT},
    {"&Replace...",C_REPLACE}};
static const MenuItem viewmenu[]={
    {"&Split Window  Ctrl+F6",C_SPLIT},{"S&ize Window   Ctrl+F8",C_SIZE},
    {"&Close Window  Ctrl+F4",C_UNSPLIT}};
static const MenuItem optionmenu[]={
    {"&Settings...",C_SETTINGS},{"&Colors...",C_COLORS}};
static const MenuItem helpmenu[]={
    {"&Commands...",C_HELP},{"&About...",C_ABOUT}};
static const MenuItem *menus[]={filemenu,editmenu,searchmenu,viewmenu,optionmenu,helpmenu};
static const int counts[]={7,4,3,3,2,2};
static const int widths[]={20,24,30,36,22,22};

static Buffer *active(void) { return &docs[current]; }
static unsigned long lower(unsigned long a,unsigned long b) { return a<b?a:b; }
static unsigned long upper(unsigned long a,unsigned long b) { return a>b?a:b; }

/* Views are new implementation structures, not recovered original layouts.
 * Buffer's legacy position fields are a working copy for the focused view. */
static void save_view(void)
{
    Buffer *b=active();View *v=&views[focused];
    v->document=current;v->cursor=b->cursor;v->anchor=b->anchor;
    v->top=b->top;v->left=b->left;
    v->preferred_col=preferred_col;v->preferred_valid=preferred_valid;
}
static void load_view(int index)
{
    Buffer *b;View *v;
    focused=index;v=&views[index];current=v->document;b=active();
    b->cursor=v->cursor;b->anchor=v->anchor;b->top=v->top;b->left=v->left;
    preferred_col=v->preferred_col;preferred_valid=v->preferred_valid;
    pane_height=view_count==1?screen_rows-3:(index==0?split_y-2:screen_rows-split_y-2);
}
static void select_doc(int index)
{
    save_view();current=index;
    views[focused].document=index;
    views[focused].cursor=active()->cursor;views[focused].anchor=active()->anchor;
    views[focused].top=active()->top;views[focused].left=active()->left;
    views[focused].preferred_valid=0;load_view(focused);
}
static unsigned long changed_position(unsigned long p,unsigned long at,
                                      unsigned long count,unsigned long n)
{
    if (p<at) return p;
    if (p<=at+count) return at+n;
    return p-count+n;
}
static int replace_active(Buffer *b,unsigned long at,unsigned long count,
                          ConstBytePtr bytes,unsigned long n)
{
    int e,i;
    e=buf_replace(b,at,count,bytes,n);
    if (!e && (count || n)) for (i=0;i<view_count;i++) {
        View *v=&views[i];
        if (i==focused || v->document!=current) continue;
        v->cursor=changed_position(v->cursor,at,count,n);
        v->anchor=changed_position(v->anchor,at,count,n);
        v->top=changed_position(v->top,at,count,n);
        v->preferred_valid=0;
    }
    return e;
}

static void cell(int x,int y,int ch,int attr)
{
    if (x>=0 && x<screen_cols && y>=0 && y<screen_rows) {
        screen[y*screen_cols+x].ch=ch;
        screen[y*screen_cols+x].attr=(unsigned char)attr;
    }
}

static void fill(int x,int y,int w,int h,int ch,int attr)
{
    int i,j;
    for (j=0;j<h;j++) for (i=0;i<w;i++) cell(x+i,y+j,ch,attr);
}

static unsigned string_character(const char *s,unsigned long *code,int *width)
{
    unsigned n=utf8_decode((const unsigned char *)s,(unsigned)strlen(s),code);
    if (n && *code>=128) { *width=utf8_width(*code);*code+=CELL_UNICODE;return n; }
    *code=(unsigned char)*s;*width=1;return 1;
}
static unsigned string_next(const char *s,unsigned p)
{
    unsigned long code;int w;
    return s[p]?p+string_character(s+p,&code,&w):p;
}
static unsigned string_prev(const char *s,unsigned p)
{
    if (!p) return 0;
    p--;
    while (p && ((unsigned char)s[p]&192)==128) p--;
    return p;
}
static unsigned string_columns(const char *s,unsigned from,unsigned to)
{
    unsigned n=0;unsigned long code;int w;
    while (s[from] && from<to) { from+=string_character(s+from,&code,&w);n+=w; }
    return n;
}
static unsigned field_start(const char *s,unsigned pos,unsigned width)
{
    unsigned first=0;
    while (first<pos && string_columns(s,first,pos)>=width) first=string_next(s,first);
    return first;
}

static void text(int x,int y,const char *s,int attr,int max)
{
    int n=0,w,i;unsigned used;unsigned long code;
    while (*s && n<max) {
        used=string_character(s,&code,&w);
        if (n+w>max || x+n+w>screen_cols) break;
        cell(x+n,y,(int)code,attr);
        for (i=1;i<w;i++) cell(x+n+i,y,(int)CELL_CONTINUATION,attr);
        s+=used;n+=w;
    }
}

static void hottext(int x,int y,const char *s,int attr,int hot)
{
    unsigned used;unsigned long code;int width,i,color;
    while (*s && x<screen_cols-1) {
        color=attr;
        if (*s=='&' && s[1]) { s++;color=hot; }
        used=string_character(s,&code,&width);
        if (x+width>screen_cols) break;
        cell(x++,y,(int)code,color);
        for (i=1;i<width;i++) cell(x++,y,(int)CELL_CONTINUATION,color);
        s+=used;
    }
}

static void box(int x,int y,int w,int h,const char *title,int attr)
{
    fill(x,y,w,h,' ',attr);
    fill(x+1,y,w-2,1,196,attr); fill(x+1,y+h-1,w-2,1,196,attr);
    fill(x,y+1,1,h-2,179,attr); fill(x+w-1,y+1,1,h-2,179,attr);
    cell(x,y,218,attr);cell(x+w-1,y,191,attr);
    cell(x,y+h-1,192,attr);cell(x+w-1,y+h-1,217,attr);
    if (title) text(x+(w-(int)strlen(title))/2,y,title,attr,w-4);
}

static void selection(Buffer *b,unsigned long *lo,unsigned long *hi)
{
    *lo=lower(b->cursor,b->anchor); *hi=upper(b->cursor,b->anchor);
}

static void ensure_visible(Buffer *b)
{
    unsigned long line,col,p,next;
    int row,height=pane_height;
    line=buf_line_start(b,b->cursor);
    b->top=buf_line_start(b,lower(b->top,buf_len(b)));
    if (line<b->top) b->top=line;
    p=b->top; row=0;
    while (p<line && row<height) {
        next=buf_next_line(b,p);
        if (next<=p) break;
        p=next; row++;
    }
    if (row>=height) {
        b->top=line;
        for (row=1;row<height;row++) b->top=buf_prev_line(b,b->top);
    }
    col=buf_column(b,b->cursor,tabstop);
    if (col<b->left) b->left=col;
    if (col>=b->left+(unsigned)(screen_cols-2)) b->left=col-(unsigned)(screen_cols-3);
}

static void draw_pane(int first,int last,int is_active)
{
    Buffer *b=active();
    unsigned long p,end,next,col,stop,lo,hi,start,len,code=0;
    int y,x,ch,attr;
    char title[320],display_path[ED_PATH];
    ensure_visible(b);
    strcpy(display_path,*b->path?b->path:"Untitled");
    sprintf(title," %.74s ",display_path);
    fill(0,first,screen_cols,last-first+1,' ',border_attr);
    fill(0,first,screen_cols,1,196,border_attr);
    cell(0,first,218,border_attr);cell(screen_cols-1,first,191,border_attr);
    fill(0,first+1,1,last-first,179,border_attr);
    fill(1,first+1,screen_cols-2,last-first,' ',normal_attr);
    text((screen_cols-(int)strlen(title))/2,first,title,menu_attr,screen_cols-4);
    cell(screen_cols-1,first+1,24,border_attr);cell(screen_cols-1,last,25,border_attr);
    fill(screen_cols-1,first+2,1,last-first-2,219,border_attr);
    selection(b,&lo,&hi); p=b->top; len=buf_len(b); cx=1;cy=first+1;
    for (y=first+1;y<=last;y++) {
        start=p; end=buf_line_end(b,p); col=0;
        while (p<end) {
            ch=buf_get(b,p);stop=col+1;
            if (b->utf8 && !b->binary_width && ch>=128) {
                code=buf_codepoint(b,p);ch=(int)(CELL_UNICODE+code);stop=col+utf8_width(code);
            }
            if (!b->binary_width && ch=='\t') { ch=' '; stop=col+tabstop-col%tabstop; }
            attr=p>=lo && p<hi?sel_attr:normal_attr;
            while (col<stop) {
                if (col>=b->left && col<b->left+(unsigned)(screen_cols-2)) {
                    x=1+(int)(col-b->left);
                    if ((ch==CELL_CONTINUATION && col==b->left) ||
                        (ch>=CELL_UNICODE && ch<CELL_CONTINUATION && stop>b->left+(unsigned)(screen_cols-2))) cell(x,y,' ',attr);
                    else
                    cell(x,y,ch?ch:250,attr);
                }
                col++;
                if (ch>=CELL_UNICODE) ch=(int)CELL_CONTINUATION;
            }
            p=buf_char_next(b,p);
        }
        if (buf_line_start(b,b->cursor)==start) {
            cx=1+(int)(buf_column(b,b->cursor,tabstop)-b->left); cy=y;
        }
        next=buf_next_line(b,start);
        if (next<=start || (next==len && (!len ||
            (b->binary_width && len%b->binary_width!=0) ||
            (!b->binary_width && buf_get(b,len-1)!='\n')))) break;
        p=next;
    }
    (void)is_active;
}

static void draw(void)
{
    Buffer *b;
    char status[160];
    int i,focus=focused,active_x=1,active_y=2;
    if (screen_cols<75 || screen_rows<24) {
        fill(0,0,screen_cols,screen_rows,' ',menu_attr);
        text(0,0,"Resize terminal to at least 75 columns and 24 rows.",menu_attr,screen_cols);
        cx=-1;cy=0;return;
    }
    if (split_y>screen_rows-5) split_y=screen_rows-5;
    if (split_y<4) split_y=4;
    save_view();
    fill(0,0,screen_cols,screen_rows,' ',normal_attr);
    fill(0,0,screen_cols,1,' ',menu_attr);cell(0,0,' ',border_attr);
    for (i=0;i<6;i++) hottext(menux[i],0,titles[i],menu_attr,menu_attr);
    for (i=0;i<view_count;i++) {
        load_view(i);
        draw_pane(i?split_y:1,view_count==1 || i?screen_rows-2:split_y-1,i==focus);
        if (i==focus) { active_x=cx;active_y=cy; }
        save_view();
    }
    load_view(focus);cx=active_x;cy=active_y;b=active();
    fill(0,screen_rows-1,screen_cols,1,' ',menu_attr);
    text(1,screen_rows-1,"F1=Help",menu_attr,10);
    if (b->binary_width)
        sprintf(status,"Line:%lu  Col:%lu  Value:%d",buf_line_number(b,b->cursor),
                buf_column(b,b->cursor,tabstop)+1,buf_get(b,b->cursor));
    else sprintf(status,"Line:%lu    Col:%lu",buf_line_number(b,b->cursor),buf_column(b,b->cursor,tabstop)+1);
    cell(screen_cols-(b->binary_width?36:27),screen_rows-1,179,menu_attr);
    text(screen_cols-(b->binary_width?33:24),screen_rows-1,status,menu_attr,b->binary_width?33:24);
}

/* Shared dialog style: centered title, shadow and horizontal button row. */
static int dialog_ready(void)
{
    draw();
    if (screen_cols>=75 && screen_rows>=24) return 1;
    platform_present(-1,0,1);platform_key();return 0;
}

static void dialog_frame(int x,int y,int width,int height,const char *title)
{
    fill(x+1,y+1,width,height,' ',0);
    fill(x,y,width,height,' ',menu_attr);
    fill(x,y,width,1,' ',0xf0);
    text(x+(width-(int)strlen(title))/2,y,title,0xf0,width-2);
}

static void dialog_button(int x,int y,int width,const char *label,int focused_button)
{
    fill(x,y,width,1,' ',0xf0);
    text(x+(width-(int)strlen(label))/2,y,label,0xf0,width-2);
    if (focused_button) { cell(x,y,16,0xf0);cell(x+width-1,y,17,0xf0); }
}

static int button_hit(int x,int y,int width)
{
    return (mouse_event.pressed&1) && mouse_event.y==y &&
        mouse_event.x>=x && mouse_event.x<x+width;
}

static void dialog_body(int x,int y,const char *s)
{
    char line[65];unsigned n;int row=0;
    while (*s && row<4) {
        n=0;
        while (*s && *s!='\n' && n<64) line[n++]=*s++;
        line[n]=0;text(x,y+row++,line,menu_attr,64);
        if (*s=='\n') s++;
    }
}

static void message(const char *title,const char *s)
{
    int x,y,k;
    for (;;) {
        if (!dialog_ready()) continue;
        x=(screen_cols-70)/2;y=(screen_rows-9)/2;
        dialog_frame(x,y,70,9,title);dialog_body(x+3,y+2,s);
        dialog_button(x+31,y+7,8,"OK",1);
        platform_present(-1,0,1);k=platform_key();
        if (k==K_MOUSE && button_hit(x+31,y+7,8)) k=13;
        if (k==13 || k==27 || k==' ') return;
    }
}

static void error(int e) { if (e) message(" MS-EDIT Linux ",ed_error(e)); }

static int question(const char *title,const char *s)
{
    int x,y,k,focus=0,i;
    static const char *labels[]={"Yes","No","Cancel"};
    for (;;) {
        if (!dialog_ready()) continue;
        x=(screen_cols-70)/2;y=(screen_rows-9)/2;
        dialog_frame(x,y,70,9,title);dialog_body(x+3,y+2,s);
        for (i=0;i<3;i++) dialog_button(x+13+i*16,y+7,12,labels[i],focus==i);
        platform_present(-1,0,1);k=platform_key();
        if (k==K_MOUSE) for (i=0;i<3;i++) {
            if (button_hit(x+13+i*16,y+7,12)) { focus=i;k=13;break; }
        }
        if (k==9 || k==K_RIGHT) focus=(focus+1)%3;
        else if (k==(K_SHIFT|9) || k==K_LEFT) focus=(focus+2)%3;
        else if (k==13 || k==' ') return focus==0?1:(focus==1?0:-1);
        else if (k=='y' || k=='Y' || k==(K_ALT|'y')) return 1;
        else if (k=='n' || k=='N' || k==(K_ALT|'n')) return 0;
        else if (k==27 || k==(K_ALT|'c')) return -1;
    }
}

static int prompt(const char *title,const char *label,char *s,unsigned cap)
{
    char work[ED_PATH];
    unsigned len,pos,first,next;
    int x,y,k,focus=0;
    if (cap>sizeof(work)) cap=sizeof(work);
    strncpy(work,s,cap-1);work[cap-1]=0;
    len=(unsigned)strlen(work);pos=len;
    for (;;) {
        if (!dialog_ready()) continue;
        x=(screen_cols-70)/2;y=(screen_rows-8)/2;
        dialog_frame(x,y,70,8,title);
        text(x+3,y+2,label,menu_attr,63);
        fill(x+3,y+3,63,1,' ',normal_attr);
        first=field_start(work,pos,62);
        text(x+3,y+3,work+first,normal_attr,63);
        dialog_button(x+18,y+5,10,"OK",focus==1);
        dialog_button(x+40,y+5,12,"Cancel",focus==2);
        platform_present(focus==0?x+3+(int)string_columns(work,first,pos):-1,y+3,1);
        k=platform_key();
        if (k==K_MOUSE) {
            if (button_hit(x+18,y+5,10)) { focus=1;k=13; }
            else if (button_hit(x+40,y+5,12)) { focus=2;k=13; }
            else if (button_hit(x+3,y+3,63)) focus=0;
        }
        if (k==9 || k==(K_SHIFT|9)) { focus=(focus+(k==9?1:2))%3;continue; }
        if (focus && (k==K_LEFT || k==K_RIGHT)) { focus=3-focus;continue; }
        if (k==27 || ((k==13 || k==' ') && focus==2)) return 0;
        if (k==13 || (k==' ' && focus==1)) { strcpy(s,work);return 1; }
        if (focus) continue;
        if (k&K_UNICODE) {
            unsigned char encoded[4];unsigned n=utf8_encode(k&0x1fffffL,encoded);
            if (len+n<cap) { memmove(work+pos+n,work+pos,len-pos+1);memcpy(work+pos,encoded,n);pos+=n;len+=n; }
            continue;
        }
        if (k==K_LEFT && pos) pos=string_prev(work,pos);
        else if (k==K_RIGHT && pos<len) pos=string_next(work,pos);
        else if (k==K_HOME) pos=0;
        else if (k==K_END) pos=len;
        else if (k==8 && pos) { next=string_prev(work,pos);memmove(work+next,work+pos,len-pos+1);len-=pos-next;pos=next; }
        else if (k==K_DEL && pos<len) { next=string_next(work,pos);memmove(work+pos,work+next,len-next+1);len-=next-pos; }
        else if (k>=32 && k<=255 && len+1<cap) {
            memmove(work+pos+1,work+pos,len-pos+1);work[pos++]=(char)k;len++;
        }
    }
}

static int file_dialog(char *chosen,int save,int *readonly,unsigned *width);

static int save_doc(int as)
{
    Buffer *b=active();
    char path[ED_PATH];
    FILE *f;
    int e,ro;unsigned width;
    if (b->readonly) { error(ED_READONLY); return 0; }
    strcpy(path,b->path);
    if (as || !*path) {
        ro=b->readonly;width=b->binary_width;
        if (!file_dialog(path,1,&ro,&width)) return 0;
        if (!*path) { error(ED_RANGE);return 0; }
        if (strcmp(path,b->path)) {
            f=fopen(path,"rb");
            if (f) {
                fclose(f);
                if (question(" Save As ","Replace existing file?")!=1) return 0;
            }
        }
    }
    e=buf_save(b,path); error(e); return !e;
}

static int confirm_close(void)
{
    int answer;
    if (!active()->modified) return 1;
    answer=question(" Save file ","This file has not been saved yet. Save it now?");
    if (answer<0) return 0;
    return answer?save_doc(0):1;
}

static void new_doc(void)
{
    if (doc_count==MAX_DOCS) { message(" Edit ","You can load no more than 9 files at a time.");return; }
    save_view();
    current=doc_count++; buf_init(active());save_view();
}

static int open_path(const char *path,int ro,unsigned width,int interactive)
{
    Buffer temp;
    char full[ED_PATH],other[ED_PATH];
    int e,reuse,i;
    if (strchr(path,'*') || strchr(path,'?')) return ED_RANGE;
    if (!fs_absolute(full,path)) for (i=0;i<doc_count;i++) {
        if (!*docs[i].path || fs_absolute(other,docs[i].path)) continue;
        e=strcmp(full,other);
        if (!e) { select_doc(i);return ED_OK; }
    }
    reuse=doc_count==1 && !*docs[0].path && !docs[0].modified && !buf_len(&docs[0]);
    if (doc_count==MAX_DOCS && !reuse) return ED_LIMIT;
    buf_init(&temp);
    e=buf_load(&temp,path,ro,width);
    if (e==ED_NOTFOUND && strlen(path)<ED_PATH) {
        strcpy(temp.path,path);temp.readonly=ro;temp.binary_width=width;e=ED_OK;
    }
    if (e) { buf_free(&temp); if (interactive) error(e);return e; }
    save_view();
    if (reuse) { buf_free(&docs[0]);current=0; }
    else current=doc_count++;
    docs[current]=temp;save_view();
    return ED_OK;
}

static void help(void);

/* Selector working state is separate from documents; cancel never loads a file. */
static int file_dialog(char *chosen,int save,int *readonly,unsigned *width)
{
    static char last_directory[ED_PATH];
    FsList files,dirs;
    const FsEntry *entry;
    char directory[ED_PATH],pattern[ED_PATH],name[ED_PATH],path[ED_PATH];
    char part[ED_PATH],newdir[ED_PATH],label[48],number[16];
    unsigned fi=0,di=0,ft=0,dt=0,n,pos,len,first,row,next;
    unsigned long last_click=0,now;int last_x=-1,last_y=-1,double_click;
    int focus=0,select_all=1,ro=*readonly,binary=*width!=0;
    int k,e=0,y=(screen_rows-23)/2,accepted=0,refresh=1,i,offset=save?2:0,origin;
    files.head=dirs.head=NULL;files.count=dirs.count=0;
    if (!*last_directory && fs_cwd(last_directory)) return 0;
    strcpy(directory,last_directory);strcpy(pattern,"*.*");
    strcpy(name,*chosen?chosen:"*.*");
    if (*chosen && !fs_split(chosen,newdir,part) && fs_isdir(newdir)) {
        strcpy(directory,newdir);strcpy(name,part);
    }
    sprintf(number,"%u",*width?*width:70);pos=(unsigned)strlen(name);
    for (;;) {
        if (!dialog_ready()) continue;
        y=(screen_rows-23)/2;origin=(screen_cols-80)/2;
        if (refresh) {
            e=fs_list(&files,directory,pattern,0);
            if (!e) e=fs_list(&dirs,directory,"*.*",1);
            if (e) { error(e);break; }
            fi=di=ft=dt=0;refresh=0;
        }
        dialog_frame(origin+15,y,51,22,save?"Save As":"Open");
        text(origin+17,y+2,"File Name:",menu_attr,10);cell(origin+28,y+2,'[',menu_attr);
        fill(origin+29,y+2,32,1,'.',menu_attr);cell(origin+61,y+2,']',menu_attr);
        first=field_start(name,pos,32);text(origin+29,y+2,name+first,focus==0 && select_all?0x07:menu_attr,32);
        text(origin+17,y+3+(save?1:0),directory,menu_attr,46);
        text(origin+18,y+4+offset,save?"Existing Files:":"Files:",menu_attr,18);text(origin+42,y+4+offset,"Directories:",menu_attr,14);
        box(origin+17,y+5+offset,23,14-offset,NULL,menu_attr);box(origin+41,y+5+offset,21,save?12:10,NULL,menu_attr);
        if (fi<ft) ft=fi;
        if (fi>=ft+12-offset) ft=fi-11+offset;
        if (di<dt) dt=di;
        if (di>=dt+8) dt=di-7;
        for (row=0;row<(unsigned)(12-offset);row++) {
            entry=fs_at(&files,ft+row);if (!entry) break;
            text(origin+19,y+6+offset+(int)row,entry->name,focus==1 && fi==ft+row?0x07:menu_attr,19);
        }
        for (row=0;row<8 && dt+row<dirs.count;row++) {
            n=dt+row;entry=fs_at(&dirs,n);
            strncpy(label,entry->name,47);label[47]=0;
            text(origin+43,y+6+offset+(int)row,label,focus==2 && di==n?0x07:menu_attr,17);
        }
        if (!save) {
        sprintf(label,"[%c] Open Read-only",ro?'X':' ');text(origin+42,y+15,label,focus==3?0x07:menu_attr,22);
        sprintf(label,"[%c] Open Binary",binary?'X':' ');text(origin+42,y+16,label,focus==4?0x07:menu_attr,22);
        sprintf(label,"Line Width: [%-4s]",number);text(origin+42,y+18,label,binary?(focus==5?0x07:menu_attr):0x78,22);
        }
        dialog_button(origin+19,y+20,8,"OK",focus==6 || focus<6);
        dialog_button(origin+34,y+20,10,"Cancel",focus==7);
        dialog_button(origin+52,y+20,8,"Help",focus==8);
        fill(0,screen_rows-1,screen_cols,1,' ',menu_attr);
        text(1,screen_rows-1,"F1=Help  Enter=Execute  Esc=Cancel  Tab=Next Field",menu_attr,70);
        platform_present(focus==0?origin+29+(int)string_columns(name,first,pos):-1,y+2,1);k=platform_key();
        if (k==K_MOUSE) {
            if (!(mouse_event.pressed&1)) continue;
            now=platform_ticks();
            double_click=mouse_event.x==last_x && mouse_event.y==last_y && now-last_click<500UL;
            last_x=mouse_event.x;last_y=mouse_event.y;last_click=now;
            i=mouse_event.x-origin;k=0;
            if (mouse_event.y==y+2 && i>=29 && i<61) {
                focus=0;select_all=0;pos=first;
                while (name[pos] && string_columns(name,first,pos)<(unsigned)(i-29)) pos=string_next(name,pos);
            } else if (i>=18 && i<39 && mouse_event.y>=y+6+offset && mouse_event.y<y+18) {
                row=ft+(unsigned)(mouse_event.y-y-6-offset);
                if (row<files.count) {
                    focus=1;fi=row;entry=fs_at(&files,row);strcpy(name,entry->name);
                    pos=(unsigned)strlen(name);if (double_click) k=13;
                }
            } else if (i>=42 && i<61 && mouse_event.y>=y+6+offset && mouse_event.y<y+(save?18:14)) {
                row=dt+(unsigned)(mouse_event.y-y-6-offset);
                if (row<dirs.count) { focus=2;di=row;if (double_click) k=13; }
            } else if (!save && i>=42 && i<64 && mouse_event.y==y+15) { focus=3;k=' '; }
            else if (!save && i>=42 && i<64 && mouse_event.y==y+16) { focus=4;k=' '; }
            else if (!save && binary && i>=54 && i<62 && mouse_event.y==y+18) focus=5;
            else if (mouse_event.y==y+20) {
                if (i>=19 && i<27) { focus=6;k=13; }
                if (i>=34 && i<44) { focus=7;k=13; }
                if (i>=52 && i<60) { focus=8;k=13; }
            }
            if (!k) continue;
        }
        if (k==27 || (k==13 && focus==7)) break;
        if (k==K_F1 || (k==13 && focus==8)) { help();continue; }
        if (k==9 || k==(K_SHIFT|9)) {
            focus=(focus+(k==9?1:8))%9;
            if (save && focus>=3 && focus<=5) focus=k==9?6:2;
            if (focus==5 && !binary) focus=k==9?6:4;
            select_all=focus==0;continue;
        }
        if (!(k&K_UNICODE) && (k&K_ALT)) {
            switch(tolower(k&255)) {
            case 'n':focus=0;select_all=1;break;case 'f':focus=1;break;
            case 'd':focus=2;break;case 'r':if (!save) { focus=3;ro=!ro; }break;
            case 'b':if (!save) { focus=4;binary=!binary; }break;case 'w':if (!save && binary) focus=5;break;
            }
            continue;
        }
        if ((k==K_LEFT || k==K_RIGHT) && focus>=6) {
            focus=6+(focus-6+(k==K_RIGHT?1:2))%3;continue;
        }
        if (focus==1 && k==K_RIGHT) { focus=2;continue; }
        if (focus==2 && k==K_LEFT) { focus=1;continue; }
        if (k==K_WHEEL_UP || k==K_WHEEL_DOWN) {
            focus=mouse_event.x-origin>=41?2:1;
            k=k==K_WHEEL_UP?K_UP:K_DOWN;
        }
        if (focus==3 && k==' ') { ro=!ro;continue; }
        if (focus==4 && k==' ') { binary=!binary;continue; }
        if (focus==5) {
            n=(unsigned)strlen(number);
            if (k==8 && n) number[n-1]=0;
            if (k>='0' && k<='9' && n<4) { number[n++]=(char)k;number[n]=0; }
            if (k!=13) continue;
        }
        if (focus==1 || focus==2) {
            n=focus==1?files.count:dirs.count;
            row=focus==1?fi:di;
            if (k==K_UP && row) row--;
            if (k==K_DOWN && row+1<n) row++;
            if (k==K_HOME) row=0;
            if (k==K_END && n) row=n-1;
            if (k==K_PGUP) row=row>8?row-8:0;
            if (k==K_PGDN && n) row=(unsigned)lower(row+8,n-1);
            if (focus==1) fi=row;else di=row;
            if (k!=13 && k!=' ') continue;
            if (!n) continue;
            entry=fs_at(focus==1?&files:&dirs,row);
            if (focus==2) {
                e=fs_join(path,directory,entry->name);
                if (!e) e=fs_absolute(newdir,path);
                if (e || !fs_isdir(newdir)) { error(e?e:ED_NOTFOUND);continue; }
                strcpy(directory,newdir);refresh=1;continue;
            }
            strcpy(name,entry->name);pos=(unsigned)strlen(name);k=13;
        }
        if (k==13) {
            if (!*name) continue;
            e=fs_join(path,directory,name);
            if (!e && fs_isdir(path)) {
                e=fs_absolute(newdir,path);
                if (!e) { strcpy(directory,newdir);strcpy(name,pattern);pos=(unsigned)strlen(name);select_all=1;refresh=1;continue; }
            }
            if (!e && (strchr(name,'*') || strchr(name,'?'))) {
                e=fs_split(path,newdir,part);
                if (!e && fs_isdir(newdir)) {
                    strcpy(directory,newdir);strcpy(pattern,part);strcpy(name,part);
                    pos=(unsigned)strlen(name);select_all=1;refresh=1;continue;
                }
                if (!e) e=ED_NOTFOUND;
            }
            if (e) { error(e);continue; }
            n=(unsigned)atoi(number);
            if (binary && (!n || n>1021)) { message(" Invalid Line Width ","Line width must be between 1 and 1021.");continue; }
            strcpy(chosen,path);*readonly=ro;*width=binary?n:0;
            strcpy(last_directory,directory);accepted=1;break;
        }
        if (focus!=0) continue;
        if (k&K_UNICODE) {
            unsigned char encoded[4];unsigned used=utf8_encode(k&0x1fffffL,encoded);
            if (select_all) { name[0]=0;pos=0;select_all=0; }
            len=(unsigned)strlen(name);
            if (len+used<sizeof(name)) {
                memmove(name+pos+used,name+pos,len-pos+1);memcpy(name+pos,encoded,used);pos+=used;
            }
            continue;
        }
        len=(unsigned)strlen(name);
        if (k==K_LEFT) { if (pos) pos=string_prev(name,pos);select_all=0; }
        else if (k==K_RIGHT) { if (pos<len) pos=string_next(name,pos);select_all=0; }
        else if (k==K_HOME) { pos=0;select_all=0; }
        else if (k==K_END) { pos=len;select_all=0; }
        else if (k==8 || k==K_DEL || (k>=32 && k<=255)) {
            if (select_all) { len=pos=0;name[0]=0;select_all=0; }
            if (k==8 && pos) { next=string_prev(name,pos);memmove(name+next,name+pos,len-pos+1);pos=next; }
            else if (k==K_DEL && pos<len) { next=string_next(name,pos);memmove(name+pos,name+next,len-next+1); }
            else if (k>=32 && k<=255 && len+1<sizeof(name)) {
                memmove(name+pos+1,name+pos,len-pos+1);name[pos++]=(char)k;
            }
        }
    }
    fs_free(&files);fs_free(&dirs);return accepted;
}

static void open_dialog(void)
{
    char path[ED_PATH];
    int ro=0;unsigned width=0;
    path[0]=0;
    if (file_dialog(path,0,&ro,&width)) open_path(path,ro,width,1);
}

static int open_spec(const char *spec,int ro,unsigned width)
{
    FsList list;
    const FsEntry *p;
    char directory[ED_PATH],pattern[ED_PATH],path[ED_PATH];
    int e;
    if (!strchr(spec,'*') && !strchr(spec,'?')) return open_path(spec,ro,width,0);
    list.head=NULL;list.count=0;
    e=fs_split(spec,directory,pattern);
    if (!e) e=fs_list(&list,directory,pattern,0);
    if (!e && !list.count) e=ED_NOTFOUND;
    for (p=list.head;p && !e;p=p->next) {
        e=fs_join(path,directory,p->name);
        if (!e) e=open_path(path,ro,width,0);
    }
    fs_free(&list);return e;
}

static void close_doc(void)
{
    int i;
    if (!confirm_close()) return;
    buf_free(active());
    for (i=current;i<doc_count-1;i++) docs[i]=docs[i+1];
    doc_count--;buf_init(&docs[doc_count]);
    if (!doc_count) doc_count=1;
    for (i=0;i<view_count;i++) {
        if (views[i].document==current) {
            memset(&views[i],0,sizeof(views[i]));
            views[i].document=current<doc_count?current:doc_count-1;
        } else if (views[i].document>current) views[i].document--;
    }
    load_view(focused);
}

static void copy_region(int cut)
{
    Buffer *b=active();
    BytePtr p;
    unsigned long lo,hi,i;
    int e;
    selection(b,&lo,&hi);
    if (lo==hi) { message(" Edit ","You must select a region first."); return; }
    if (cut && b->readonly) { error(ED_READONLY);return; }
    p=ed_alloc(hi-lo);
    if (!p) { error(ED_NOMEM);return; }
    for (i=lo;i<hi;i++) p[i-lo]=(unsigned char)buf_get(b,i);
    if (buf_io_error(b)) { ed_free(p);error(ED_IO);return; }
    ed_free(clipboard);clipboard=p;clip_len=hi-lo;
    if (cut) { e=replace_active(b,lo,hi-lo,NULL,0);error(e); }
}

static void insert_bytes(ConstBytePtr s,unsigned long n)
{
    Buffer *b=active();
    unsigned long lo,hi;
    selection(b,&lo,&hi);
    error(replace_active(b,lo,hi-lo,s,n)); preferred_valid=0;
}

static int find_options(void)
{
    int q;
    q=question(" Search ","Match case?");if (q<0) return 0;search_case=q;
    q=question(" Search ","Match whole word only?");if (q<0) return 0;search_word=q;
    return 1;
}

static void find_text(int again)
{
    Buffer *b=active();
    unsigned long p,start;
    int e;
    if (!again || !*needle) {
        if (!prompt(" Find ","Find What:",needle,sizeof(needle)) || !*needle || !find_options()) return;
        start=b->cursor;
    } else start=upper(b->cursor,b->anchor);
    e=buf_find(b,start,(ConstBytePtr)needle,(unsigned long)strlen(needle),search_case,search_word,&p);
    if (buf_io_error(b)) { error(ED_IO);return; }
    if (e) message(" Find ","No more matches found.");
    else { b->anchor=p;b->cursor=p+strlen(needle);preferred_valid=0; }
}

static void replace_text(void)
{
    Buffer *b=active();
    unsigned long p,start,n,r,count=0;
    int all,q,e;
    char msg[80];
    if (b->readonly) { error(ED_READONLY);return; }
    if (!prompt(" Replace ","Find What:",needle,sizeof(needle)) || !*needle) return;
    if (!prompt(" Replace ","Replace With:",replacement,sizeof(replacement))) return;
    if (!find_options()) return;
    all=question(" Replace ","Replace all in document? (No = confirm from cursor)");
    if (all<0) return;
    n=(unsigned long)strlen(needle);r=(unsigned long)strlen(replacement);
    start=all?0:lower(b->cursor,b->anchor);
    while (buf_find(b,start,(ConstBytePtr)needle,n,search_case,search_word,&p)==ED_OK) {
        b->anchor=p;b->cursor=p+n;
        q=all?1:question(" Replace ","Replace this occurrence?");
        if (q<0) break;
        if (q) {
            e=replace_active(b,p,n,(ConstBytePtr)replacement,r);
            if (e) { error(e);return; }
            count++;start=p+r;
        } else start=p+n;
    }
    sprintf(msg,"Change complete. %lu replacements.",count);
    message(" Replace ",msg);preferred_valid=0;
}

static void help(void)
{
    static const char *lines[]={
        "F1  Help                 F10 / Alt+F  File menu",
        "Esc / Alt+X  Exit (asks before discarding edits)",
        "F2  Save     File menu: New, Open, Save As, Close",
        "Arrows, Home/End, PgUp/PgDn  Move cursor",
        "Ctrl+Left/Right  Word     Ctrl+Home/End  Document",
        "Shift + movement  Select text; mouse drag also selects",
        "Ctrl+C / Ctrl+X / Ctrl+V  Copy / Cut / Paste",
        "Insert  Toggle overwrite; Backspace / Delete  Remove",
        "Ctrl+Q,F  Find    F3  Find next    Ctrl+Q,A  Replace",
        "Alt+1..9  Choose document; F6  Next document/view",
        "Ctrl+F6  Split; Ctrl+F8  Resize; Ctrl+F4  Unsplit",
        "Options: tab stops and colors (current session only)",
        "Open: wildcard filter, directories, read-only, binary",
        "Dialogs: Tab/Shift+Tab, arrows, Enter; Esc cancels",
        "Text saves use CRLF. Binary mode preserves bytes.",
        "No undo/redo. Clipboard is internal to this editor."
    };
    int x,y,k,i;
    for (;;) {
        if (!dialog_ready()) continue;
        x=(screen_cols-70)/2;y=(screen_rows-21)/2;
        dialog_frame(x,y,70,21,"Help - MS-EDIT Linux");
        for (i=0;i<16;i++) text(x+3,y+2+i,lines[i],menu_attr,64);
        dialog_button(x+31,y+19,8,"OK",1);
        platform_present(-1,0,1);k=platform_key();
        if (k==K_MOUSE && button_hit(x+31,y+19,8)) k=13;
        if (k==13 || k==27 || k==' ') return;
    }
}

static void command(int c)
{
    Buffer *b=active();
    unsigned long lo,hi;
    char value[16];
    int i,q,old_focus;View old_views[2];
    if (c>=C_DOC1 && c<C_DOC1+doc_count) { select_doc(c-C_DOC1);return; }
    switch(c) {
    case C_NEW:new_doc();break;
    case C_OPEN:open_dialog();break;
    case C_SAVE:save_doc(0);break;
    case C_SAVEAS:save_doc(1);break;
    case C_CLOSE:close_doc();break;
    case C_EXIT:
        save_view();memcpy(old_views,views,sizeof(views));old_focus=focused;
        for (i=0;i<doc_count;i++) {
            select_doc(i);
            if (!confirm_close()) {
                memcpy(views,old_views,sizeof(views));load_view(old_focus);return;
            }
        }
        running=0;break;
    case C_CUT:copy_region(1);break;
    case C_COPY:copy_region(0);break;
    case C_PASTE:if (clip_len) insert_bytes(clipboard,clip_len);break;
    case C_CLEAR:
        selection(b,&lo,&hi);
        if (hi>lo) error(replace_active(b,lo,hi-lo,NULL,0));
        else message(" Edit ","You must select a region first.");
        break;
    case C_FIND:find_text(0);break;
    case C_NEXT:find_text(1);break;
    case C_REPLACE:replace_text();break;
    case C_SETTINGS:
        sprintf(value,"%u",tabstop);
        if (prompt(" Settings ","Tab Stops (1-60):",value,sizeof(value))) {
            q=atoi(value);
            if (q>=1 && q<=60) tabstop=(unsigned)q;
            else message(" Invalid Tab Spacing ","The tab distance must be between 1 and 60 characters.");
        } break;
    case C_COLORS:
        sprintf(value,"%02X",normal_attr);
        if (prompt(" Colors ","Normal text: two hex digits, background then foreground:",value,sizeof(value))) {
            unsigned v;
            if (strlen(value)==2 && isxdigit((unsigned char)value[0]) && isxdigit((unsigned char)value[1]) && sscanf(value,"%x",&v)==1 && v<128)
                normal_attr=(unsigned char)v;
            else message(" Colors ","Use a background from 0-7 and a foreground from 0-F.");
        } break;
    case C_SPLIT:
        if (view_count==1) {
            save_view();views[1]=views[0];view_count=2;
            split_y=screen_rows/2;load_view(1);
        }
        break;
    case C_UNSPLIT:
        if (view_count==2) {
            save_view();views[0]=views[focused];view_count=1;load_view(0);
        }
        break;
    case C_SIZE:
        if (view_count==2) {
            save_view();memcpy(old_views,views,sizeof(views));old_focus=focused;
            i=split_y;
            for (;;) {
                draw();fill(0,screen_rows-1,screen_cols,1,' ',menu_attr);
                text(1,screen_rows-1,"Use arrow keys to resize window - Enter=Accept Esc=Cancel",menu_attr,78);
                platform_present(-1,0,1);q=platform_key();
                if (q==13) break;
                if (q==27) { split_y=i;memcpy(views,old_views,sizeof(views));load_view(old_focus);break; }
                if (q==K_UP && split_y>4) split_y--;
                if (q==K_DOWN && split_y<screen_rows-5) split_y++;
            }
            load_view(focused);
        }
        break;
    case C_HELP:help();break;
    case C_ABOUT:
        message(" About ","MS-EDIT Linux 0.4 - by Kai (OpenAI LLM)");break;
    }
    preferred_valid=0;
}

static int disabled(int c)
{ return c==C_PRINT || (c==C_SPLIT && view_count==2) || ((c==C_SIZE || c==C_UNSPLIT) && view_count==1); }

static void menu(int which)
{
    int sel=0,k,count,x,w,i,c,attr;
    char line[ED_PATH+24];
    const char *label,*hot;
    for (;;) {
        count=counts[which]+(which==3?doc_count:0);x=menux[which];w=widths[which];
        if (sel>=count) sel=count-1;
        draw();hottext(x,0,titles[which],sel_attr,hot_attr);
        box(x,1,w,count+2,NULL,menu_attr);
        for (i=0;i<count;i++) {
            if (i<counts[which]) { label=menus[which][i].label;c=menus[which][i].command; }
            else {
                c=C_DOC1+i-counts[which];
                sprintf(line,"&%d %.20s",i-counts[which]+1,*docs[c-C_DOC1].path?docs[c-C_DOC1].path:"Untitled");label=line;
            }
            attr=disabled(c)?0x78:menu_attr;
            if (i==sel) { attr=disabled(c)?0x18:0x1f;fill(x+1,i+2,w-2,1,' ',attr); }
            hottext(x+2,i+2,label,attr,disabled(c)?attr:((i==sel)?0x1e:hot_attr));
        }
        platform_present(-1,0,1);k=platform_key();
        if (k==K_MOUSE) {
            if (mouse_event.y==0 && (mouse_event.buttons&1)) {
                for (i=5;i>=0;i--) if (mouse_event.x>=menux[i]-1) { which=i;sel=0;break; }
                continue;
            }
            if (mouse_event.x>=x && mouse_event.x<x+w && mouse_event.y>=2 && mouse_event.y<count+2) {
                sel=mouse_event.y-2;
                if ((mouse_event.pressed|mouse_event.released)&1) k=13;
            } else if (mouse_event.pressed&1) return;
        }
        if (k==27 || k==K_F10) return;
        if (k==K_LEFT) { which=(which+5)%6;sel=0;continue; }
        if (k==K_RIGHT) { which=(which+1)%6;sel=0;continue; }
        if (k==K_UP) sel=(sel+count-1)%count;
        if (k==K_DOWN) sel=(sel+1)%count;
        if (k>=32 && k<127) {
            for (i=0;i<count;i++) {
                if (i<counts[which]) {
                    hot=strchr(menus[which][i].label,'&');
                    if (!hot || tolower((unsigned char)hot[1])!=tolower(k)) continue;
                } else if (k!='1'+i-counts[which]) continue;
                sel=i;k=13;break;
            }
        }
        if (k==13) {
            c=sel<counts[which]?menus[which][sel].command:C_DOC1+sel-counts[which];
            if (disabled(c)) { platform_beep();continue; }
            command(c);return;
        }
    }
}

static int isword_at(Buffer *b,unsigned long p)
{
    int c=buf_get(b,p);
    if (c<0) return 0;
    if (b->utf8 && !b->binary_width) return utf8_word(buf_codepoint(b,p));
    return isalnum((unsigned char)c) || c=='_';
}

static void move_cursor(int key)
{
    Buffer *b=active();
    unsigned long p=b->cursor,len=buf_len(b),line,col;
    int k=key&K_MASK,ctrl=key&K_CTRL,n=1,i,vertical=0;
    if (k==K_UP || k==K_DOWN || k==K_PGUP || k==K_PGDN) vertical=1;
    if (vertical) {
        if (!preferred_valid) { preferred_col=buf_column(b,p,tabstop);preferred_valid=1; }
        col=preferred_col;line=buf_line_start(b,p);
        if (k==K_PGUP || k==K_PGDN) n=pane_height;
        for (i=0;i<n;i++) {
            if (k==K_UP || k==K_PGUP) line=buf_prev_line(b,line);
            else line=buf_next_line(b,line);
        }
        p=buf_at_column(b,line,col,tabstop);
    } else {
        preferred_valid=0;
        switch(k) {
        case K_HOME:p=ctrl?0:buf_line_start(b,p);break;
        case K_END:p=ctrl?len:buf_line_end(b,p);break;
        case K_LEFT:
            if (ctrl) { while (p && !isword_at(b,buf_char_prev(b,p))) p=buf_char_prev(b,p);while (p && isword_at(b,buf_char_prev(b,p))) p=buf_char_prev(b,p); }
            else if (p) { p=buf_char_prev(b,p);if (!b->binary_width && p && buf_get(b,p)=='\n' && buf_get(b,p-1)=='\r') p--; }
            break;
        case K_RIGHT:
            if (ctrl) { while (p<len && isword_at(b,p)) p=buf_char_next(b,p);while (p<len && !isword_at(b,p)) p=buf_char_next(b,p); }
            else if (p<len) { if (!b->binary_width && buf_get(b,p)=='\r' && buf_get(b,p+1)=='\n') p++;p=buf_char_next(b,p); }
            break;
        }
    }
    b->cursor=p;if (!(key&K_SHIFT)) b->anchor=p;
}

static void edit_key(int k)
{
    Buffer *b=active();
    unsigned long lo,hi,end;
    unsigned char ch;
    int e;
    if (k&K_UNICODE) {
        unsigned char bytes[4];unsigned n;int legacy;
        if (b->utf8 && !b->binary_width) n=utf8_encode((unsigned long)(k&0x1fffffL),bytes);
        else {
            legacy=utf8_to_cp437((unsigned long)(k&0x1fffffL));
            if (legacy<0) { message(" Encoding ","This character cannot be represented in CP437.");return; }
            bytes[0]=(unsigned char)legacy;n=1;
        }
        selection(b,&lo,&hi);
        if (!insert_mode && lo==hi && lo<buf_line_end(b,lo)) hi=buf_char_next(b,hi);
        error(replace_active(b,lo,hi-lo,bytes,n));preferred_valid=0;return;
    }
    if ((k&K_MASK)>=K_LEFT && (k&K_MASK)<=K_PGDN) { move_cursor(k);return; }
    if (k==(K_SHIFT|K_INS)) { command(C_PASTE);return; }
    if (k==(K_CTRL|K_INS)) { command(C_COPY);return; }
    if (k==(K_SHIFT|K_DEL)) { command(C_CUT);return; }
    if (k==K_INS) { insert_mode=!insert_mode;return; }
    selection(b,&lo,&hi);
    if (k==8 || k==K_DEL) {
        if (lo==hi) {
            if (k==8 && lo) {
                lo=buf_char_prev(b,lo);
                if (!b->binary_width && lo && buf_get(b,lo)=='\n' && buf_get(b,lo-1)=='\r') lo--;
            } else if (k==K_DEL && hi<buf_len(b)) {
                if (!b->binary_width && buf_get(b,hi)=='\r' && buf_get(b,hi+1)=='\n') hi++;
                hi=buf_char_next(b,hi);
            }
        }
        e=replace_active(b,lo,hi-lo,NULL,0);error(e);
    } else if (k==13 && !b->binary_width) insert_bytes((ConstBytePtr)"\r\n",2);
    else if ((k>=32 && k<=255) || k==9 || k==14 || k==15 || (b->binary_width && k==13)) {
        ch=(unsigned char)k;
        end=buf_line_end(b,lo);
        if (!insert_mode && lo==hi && lo<end) hi=buf_char_next(b,hi);
        e=replace_active(b,lo,hi-lo,(ConstBytePtr)&ch,1);error(e);
    }
    preferred_valid=0;
}

static void mouse_main(void)
{
    static int drag=0;
    Buffer *b;
    unsigned long p,col;
    int x=mouse_event.x,y=mouse_event.y,i,first,last,row;
    if (mouse_event.released&1) { drag=0;return; }
    if (mouse_event.pressed&1) {
        if (y==0) {
            for (i=5;i>=0;i--) if (x>=menux[i]-1) { menu(i);return; }
            return;
        }
        if (view_count==2 && y==split_y) { drag=2;return; }
        i=view_count==2 && y>=split_y?1:0;
        if (i!=focused) { save_view();load_view(i); }
        drag=1;
    }
    if (!(mouse_event.buttons&1)) return;
    if (drag==2) {
        if (y>=4 && y<=screen_rows-5) split_y=y;
        load_view(focused);return;
    }
    if (drag!=1) return;
    b=active();first=focused?split_y:1;
    last=view_count==1 || focused?screen_rows-2:split_y-1;
    if (x==screen_cols-1) {
        if (!(mouse_event.pressed&1)) return;
        if (y<=first+1) b->top=buf_prev_line(b,b->top);
        else if (y>=last) b->top=buf_next_line(b,b->top);
        else b->top=buf_line_start(b,buf_len(b)*(unsigned long)(y-first-1)/(unsigned long)(last-first));
        b->cursor=b->anchor=b->top;preferred_valid=0;drag=0;return;
    }
    if (y<=first) { b->top=buf_prev_line(b,b->top);y=first+1; }
    if (y>last) { b->top=buf_next_line(b,b->top);y=last; }
    if (x<1) x=1;
    if (x>screen_cols-2) x=screen_cols-2;
    p=b->top;
    for (row=first+1;row<y;row++) p=buf_next_line(b,p);
    col=b->left+(unsigned)(x-1);p=buf_at_column(b,p,col,tabstop);
    b->cursor=p;
    if (mouse_event.pressed&1) b->anchor=p;
    preferred_valid=0;
}

static void handle(int k)
{
    Buffer *b=active();
    int i;
    if (k&K_UNICODE) { edit_key(k);return; }
    if (k==K_RESIZE) { load_view(focused);return; }
    if (k&K_ALT) {
        i=k&K_MASK;
        if (i>='1' && i<'1'+doc_count) { select_doc(i-'1');return; }
        switch(i<256?tolower(i):i) {
        case 'f':menu(0);return;case 'e':menu(1);return;case 's':menu(2);return;
        case 'v':menu(3);return;case 'o':menu(4);return;case 'h':menu(5);return;
        case 'x':command(C_EXIT);return;
        }
    }
    switch(k) {
    case K_PASTE_DATA:
        if (platform_paste_length) {
            unsigned long p=0,out=0,code;unsigned used;int legacy,valid=1;
            if (!b->utf8 && !b->binary_width) {
                while (p<platform_paste_length) {
                    used=utf8_decode(platform_paste_data+p,(unsigned)lower(4,platform_paste_length-p),&code);
                    legacy=used?utf8_to_cp437(code):-1;
                    if (legacy<0) { valid=0;break; }
                    platform_paste_data[out++]=(unsigned char)legacy;p+=used;
                }
                if (valid) platform_paste_length=out;
            }
            if (valid) insert_bytes(platform_paste_data,platform_paste_length);
            else message(" Encoding ","Paste contains characters not available in CP437.");
        }
        free(platform_paste_data);platform_paste_data=NULL;platform_paste_length=0;break;
    case K_PASTE_ERROR:message(" Paste ","Paste was incomplete or exceeded available memory. Document unchanged.");break;
    case K_MOUSE:mouse_main();break;
    case K_WHEEL_UP:case K_WHEEL_DOWN:
        for (i=0;i<3;i++) move_cursor(k==K_WHEEL_UP?K_UP:K_DOWN);
        break;
    case 27:command(C_EXIT);break;
    case K_F10:menu(0);break;
    case K_F1:command(C_HELP);break;
    case K_F2:command(C_SAVE);break;
    case 19:move_cursor(K_LEFT);break;
    case 4:move_cursor(K_RIGHT);break;
    case 5:move_cursor(K_UP);break;
    case 6:move_cursor(K_CTRL|K_RIGHT);break;
    case 18:move_cursor(K_PGUP);break;
    case 1:move_cursor(K_CTRL|K_LEFT);break;
    case K_F3:command(C_NEXT);break;
    case K_F6:
        if (view_count==2) { save_view();load_view(1-focused); }
        else select_doc((current+1)%doc_count);
        break;
    case K_CTRL|K_F6:command(C_SPLIT);break;
    case K_CTRL|K_F8:command(C_SIZE);break;
    case K_CTRL|K_F4:command(C_UNSPLIT);break;
    case 8:edit_key(8);break;
    case 3:command(C_COPY);break;case 24:command(C_CUT);break;
    case 22:command(C_PASTE);break;
    case 17:
        i=platform_key();if (i>=0 && i<256) i=tolower(i);
        if (i=='f' || i==6) command(C_FIND);
        else if (i=='a' || i==1) command(C_REPLACE);
        else if (i=='y' || i==25) error(replace_active(b,b->cursor,buf_line_end(b,b->cursor)-b->cursor,NULL,0));
        break;
    default:edit_key(k);break;
    }
}

static void usage(void)
{
    puts("MS-EDIT Linux 0.4 - by Kai (OpenAI LLM)");
    puts("Usage: ./editr [-B] [-R|-W] [-<width>] [--] [file ...]");
    puts("-B monochrome; -R read-only; -W writable (for following files).");
    puts("-<width> binary view 1..1021; -0 text. Up to 9 files, 16 MiB each.");
    puts("-h, --help show this summary. F1 opens built-in help in the editor.");
    puts("Esc or Alt+X exits, asking about unsaved changes.");
    puts("Text saves use CRLF; binary mode preserves bytes.");
    puts("Large documents use TEMP/TMP disk backing.");
}

int main(int argc,char **argv)
{
    int i,e;
    CliOptions options;
    buf_init(&docs[0]);
    cli_parse(&options,argc,argv);
    if (options.help) { usage();return 0; }
    for (i=0;i<options.count;i++) {
        e=open_spec(options.files[i].path,options.files[i].readonly,options.files[i].width);
        if (e) { fprintf(stderr,"%s: %s\n",options.files[i].path,ed_error(e));for (i=0;i<doc_count;i++) buf_free(&docs[i]);return 1; }
    }
    if (options.mono) { normal_attr=7;border_attr=7;menu_attr=0x70;hot_attr=0x70;sel_attr=0x70; }
    if (!platform_init()) { fputs("An interactive terminal is required.\n",stderr);for (i=0;i<doc_count;i++) buf_free(&docs[i]);return 1; }
    while (running) { draw();platform_present(cx,cy,insert_mode);handle(platform_key()); }
    platform_done();
    for (i=0;i<doc_count;i++) buf_free(&docs[i]);
    ed_free(clipboard);
    return 0;
}
