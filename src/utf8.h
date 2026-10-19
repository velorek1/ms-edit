#ifndef EDIT_UTF8_H
#define EDIT_UTF8_H
unsigned utf8_encode(unsigned long code,unsigned char *out);
unsigned utf8_decode(const unsigned char *bytes,unsigned available,unsigned long *code);
int utf8_to_cp437(unsigned long code);
int utf8_word(unsigned long code);
int utf8_width(unsigned long code);
#endif
