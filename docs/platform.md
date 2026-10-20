# platform.h — the terminal contract

This header connects editor.c to host.c. The editor works in cells and logical
key events; host.c translates those concepts into Linux terminal operations.
There is one Linux implementation, with no runtime backend selection.

`Cell` contains a character value and DOS-style color attribute. `screen` is a
fixed-capacity array; the current row stride is `screen_cols`. Dimensions are
bounded by `SCREEN_MAXCOLS` (512) and `SCREEN_MAXROWS` (256). Ordinary character
values may name CP437 glyphs. `CELL_UNICODE + codepoint` represents Unicode;
`CELL_CONTINUATION` marks the second cell of a wide character, which must not
be printed twice.

ASCII key values and the `K_LEFT`… event enumeration share an integer. Modifier
bits `K_SHIFT`, `K_CTRL` and `K_ALT` can be combined with a key; `K_MASK` extracts
its ordinary base. `K_UNICODE` identifies a code-point event. Mouse coordinates
are zero-based. `MouseEvent.buttons` is the held state, while `pressed` and
`released` describe transitions; checking only held buttons cannot distinguish
a click from dragging.

`platform_init()` and `platform_done()` manage terminal lifetime.
`platform_present()` flushes the prepared screen and cursor. `platform_key()`
reads the next event; `platform_ticks()` supplies monotonic milliseconds for
click timing; `platform_beep()` emits the terminal bell.

Bracketed paste arrives as `K_PASTE_DATA` with `platform_paste_data` and length.
The editor consumes and frees that allocation. `K_PASTE_ERROR` indicates that
no complete paste should be inserted. Keeping paste separate from ordinary
keys prevents pasted Escape sequences from being executed as editor commands.
