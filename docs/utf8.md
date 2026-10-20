# utf8.c and utf8.h — bytes, characters and columns

UTF-8 stores a Unicode code point in one to four bytes. Terminal columns are a
third quantity: some characters occupy two columns. This module performs the
conversions; it does not provide full Unicode text editing.

`utf8_decode()` validates continuation bytes, rejects overlong encodings,
surrogates and values above U+10FFFF, and returns the consumed length. Zero means
invalid or incomplete input. `utf8_encode()` performs the reverse operation and
returns zero for invalid code points. Callers supply space for up to four bytes.

`utf8_word()` treats underscore and locale-classified alphanumeric characters as
word characters. `utf8_width()` asks `wcwidth()` for terminal width, but maps
zero or negative widths to one. That fallback makes unsupported characters
visible as cells, while limiting combining-mark accuracy. `host.c` initializes
the character locale from the environment.

`utf8_to_cp437()` searches a 128-entry code-point table for a legacy byte, returns
ASCII unchanged, and returns -1 when no mapping exists. This lets the editor
insert representable Spanish characters into a legacy file without silently
changing its entire encoding.

For example, `ñ` occupies two UTF-8 bytes but normally one display cell. Cursor
movement in buffer.c must step across the two bytes together; rendering in
editor.c must advance only one column. Emoji sequences, grapheme clusters and
full case-insensitive Unicode searching remain unsupported or partial. Interface
tests cover Spanish UTF-8, legacy CP437 insertion and character navigation.
