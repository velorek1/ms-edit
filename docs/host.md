# host.c — Linux terminal input and output

A terminal communicates through bytes and escape sequences, not GUI widgets.
This file owns that protocol. It exposes the contract in [platform.h](platform.md)
and uses UTF-8 helpers without knowing the document model.

## Setup and cleanup

`platform_init()` saves `termios` settings, disables line buffering, echo and
several automatic byte translations, sets the character locale and reads the
terminal size. It enables an alternate screen, button-motion mouse reporting,
SGR mouse coordinates and bracketed paste. This lets an editor receive each key
and mouse drag directly without the terminal interpreting ordinary shortcuts.

`platform_done()` restores terminal modes and the original screen; it is also
registered with `atexit()`. `signal_exit()` makes a best-effort restoration for
termination signals, then exits. It cannot restore after SIGKILL or recover
unsaved text. `window_signal()` only sets a flag; `dimensions()` reads dimensions
and invalidates the previous frame outside that signal handler.

## Drawing

`platform_present()` compares `screen` against `previous`. It skips unchanged
rows and emits each changed row with cursor-position and color escape sequences.
This is row-level differential rendering, not a GUI repaint system or individual
cell-only output. `glyph()` converts CP437 values through cp437.h or encodes a
Unicode code point; continuation cells emit no extra glyph.

Scrollbar values 219 and 178 therefore appear as U+2588 `█` and U+2593 `▓` on
the Linux terminal. The choice is made in editor.c; no special scrollbar driver
is needed here. Font appearance depends on the terminal.

## Reading input

`get_byte()` uses `select()` and supports timeouts, interrupted system calls,
a queued lookahead byte and a pending resize. `get_sequence_byte()` handles
resize while completing a multi-byte sequence. `platform_key()` interprets
ASCII, UTF-8, Escape-prefixed Alt keys, CSI/SS3 function and navigation keys,
xterm modifier encodings, SGR/X10 mouse packets and bracketed paste.

A short timeout distinguishes a standalone Escape from the start of a sequence.
`mouse_input()` converts one-based terminal coordinates to zero-based cells and
computes button transitions. Wheel packets become dedicated wheel events.
`platform_ticks()` uses a monotonic clock rather than wall-clock time.

`read_paste()` collects bytes until the closing marker. `paste_append()` grows
storage up to the supported limit; incomplete or failed pastes return an error
without partial document insertion. `platform_beep()` emits BEL. When the window
is below 75×24, ordinary input waits until it is resized so hidden text is not
accidentally edited.

The Python interface suite uses a pseudo-terminal to exercise these protocols
against the real executable. It verifies selected sequences, not every terminal
emulator or every possible interrupted input stream.
