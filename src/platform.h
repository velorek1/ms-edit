#ifndef EDIT_PLATFORM_H
#define EDIT_PLATFORM_H

#define K_SHIFT 0x200
#define K_CTRL  0x400
#define K_ALT   0x800
#define K_MASK  0x1ff
#define K_UNICODE 0x1000000L
#define CELL_UNICODE 0x200000L
#define CELL_CONTINUATION 0x400000L
enum { K_LEFT=256,K_RIGHT,K_UP,K_DOWN,K_HOME,K_END,K_PGUP,K_PGDN,
       K_DEL,K_INS,K_F1,K_F2,K_F3,K_F4,K_F5,K_F6,K_F7,K_F8,K_F9,K_F10,K_MOUSE,K_RESIZE,K_WHEEL_UP,K_WHEEL_DOWN,K_PASTE_DATA,K_PASTE_ERROR };
typedef struct { int x,y,buttons,pressed,released; } MouseEvent;
extern MouseEvent mouse_event;
unsigned long platform_ticks(void);
#define SCREEN_COLS 80
typedef struct { unsigned ch;unsigned char attr; } Cell;
#define SCREEN_MAXCOLS 512
#define SCREEN_MAXROWS 256
extern Cell screen[SCREEN_MAXCOLS*SCREEN_MAXROWS];
extern int screen_rows,screen_cols;
extern unsigned char *platform_paste_data;
extern unsigned long platform_paste_length;
int platform_init(void);
void platform_done(void);
void platform_present(int x,int y,int insert);
int platform_key(void);
void platform_beep(void);
#endif
