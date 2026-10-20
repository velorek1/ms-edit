#ifndef EDIT_BUFFER_H
#define EDIT_BUFFER_H

/* C89 document storage for Linux. */

#define ED_MAX_BYTES 16777216UL
#define ED_PATH 260
#define ED_OK 0
#define ED_NOMEM 1
#define ED_RANGE 2
#define ED_READONLY 3
#define ED_IO 4
#define ED_NOTFOUND 5
#define ED_LIMIT 6
#define ED_RECOVERY 7

typedef unsigned char *BytePtr;
typedef const unsigned char *ConstBytePtr;
typedef struct {
    BytePtr data;
    struct Store *store;
    unsigned long cap, gap_lo, gap_hi;
    int modified, readonly,utf8;
    unsigned long cursor, anchor, top, left;
    unsigned long line_cache_pos,line_cache_number,newlines;
    unsigned binary_width;
    char path[ED_PATH];
} Buffer;

BytePtr ed_alloc(unsigned long n);
void ed_free(BytePtr p);
void buf_init(Buffer *b);
void buf_free(Buffer *b);
unsigned long buf_len(const Buffer *b);
unsigned long buf_char_next(const Buffer *b,unsigned long position);
unsigned long buf_char_prev(const Buffer *b,unsigned long position);
unsigned long buf_codepoint(const Buffer *b,unsigned long position);
int buf_io_error(const Buffer *b);
int buf_get(const Buffer *b, unsigned long p);
int buf_replace(Buffer *b, unsigned long at, unsigned long count,
                ConstBytePtr s, unsigned long n);
int buf_load(Buffer *b, const char *path, int readonly, unsigned width);
int buf_save(Buffer *b, const char *path);
unsigned long buf_line_start(const Buffer *b, unsigned long p);
unsigned long buf_line_end(const Buffer *b, unsigned long p);
unsigned long buf_next_line(const Buffer *b, unsigned long p);
unsigned long buf_prev_line(const Buffer *b, unsigned long p);
unsigned long buf_line_number(Buffer *b, unsigned long p);
unsigned long buf_column(const Buffer *b, unsigned long p, unsigned tabs);
unsigned long buf_at_column(const Buffer *b, unsigned long start,
                            unsigned long col, unsigned tabs);
int buf_find(const Buffer *b, unsigned long start, ConstBytePtr s,
             unsigned long n, int match_case, int whole_word,
             unsigned long *found);
const char *ed_error(int error);
#endif
