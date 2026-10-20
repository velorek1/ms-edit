# buffer.c and buffer.h — documents and storage

A `Buffer` owns document bytes and storage, plus path, encoding, read-only and
modified flags. Positions are byte offsets, not character counts. `cursor`,
`anchor`, `top` and `left` also hold the currently loaded view's positions.
`ED_MAX_BYTES` is 16 MiB and `ED_PATH` includes the terminating zero byte.
Functions return `ED_OK` or an error code; `ed_error()` translates errors into
user-facing text.

## Lifetime and byte access

`buf_init()` clears state and assumes UTF-8 initially. `buf_free()` releases both
memory and temporary storage, then reinitializes the object. `ed_alloc()` and
`ed_free()` are allocation wrappers. `buf_len()` and `buf_get()` hide storage
layout; an out-of-range byte read returns -1. `buf_io_error()` distinguishes a
backing-store failure from ordinary end-of-document.

Small documents use a **gap buffer**: allocated memory has unused space between
`gap_lo` and `gap_hi`. Moving the gap to an edit location lets nearby insertions
avoid moving the rest of the document each time. `reserve()` grows capacity;
`move_gap()` relocates bytes. Logical offsets skip the gap.

Above 96 KiB, `store_from_buffer()` moves content into a temporary file. A `Piece`
links a physical offset and length; the list's order describes the logical text.
`store_new()` creates an exclusive file in TEMP, then TMP if TEMP is unset, or
`.` if both are unset. This selection is not a retry through all directories.
`piece_add()` merges adjacent file ranges, and `piece_range()` copies slices of
the logical list. `store_get()` resolves a logical position and uses a 4 KiB
read cache. `pieces_free()` and `store_free()` release the list and file.

`store_replace()` first prepares a replacement list and appends inserted bytes.
It publishes the new list only after a successful write and flush. Failed
appends may leave unused file bytes, but the previous piece list stays valid.
This favors recoverable edits over compact temporary files; no compaction or
crash recovery is provided.

## Editing and line bookkeeping

`buf_replace(b, at, count, bytes, n)` is the central edit: remove `count` bytes at
`at`, then insert `n` bytes. It checks read-only state, ranges and size before
changing the document. UI code should call `replace_active()` so other views
are adjusted too.

`newlines` counts LF bytes. Replacements compute a candidate count from removed
and inserted bytes and publish it only after the edit succeeds. Text has
`newlines + 1` logical lines, including the empty line after a trailing LF.
This makes proportional scrollbar sizing constant-time, without rescanning a
large document on every redraw. Binary rows are calculated from length and row
width instead. A separate position/line-number cache accelerates navigation.

`buf_line_start()`, `buf_line_end()`, `buf_next_line()` and `buf_prev_line()`
implement text lines or fixed-width binary rows. CR immediately before LF is
excluded from text line content. `buf_line_number()` counts from its cached
position. `buf_column()` expands tabs and character widths; `buf_at_column()`
maps display columns back to bytes. These conversions are essential: byte 8 is
not necessarily screen column 8.

`buffer_decode()`, `buf_char_next()`, `buf_char_prev()` and `buf_codepoint()` keep
UTF-8 cursor movement on character boundaries. `buf_find()` searches byte
patterns with case and whole-word options. Its ASCII folding and Unicode word
classification do not implement full Unicode case folding.

## Loading and saving

`buf_load()` builds a temporary Buffer and replaces the old object only on
success. It detects valid UTF-8 in text mode; invalid input uses legacy byte
handling. Loading keeps original bytes, including CRLF.

`buf_save()` streams output via `save_byte()` in 4 KiB blocks. In text mode,
CRLF becomes LF and existing LF remains LF. A non-empty document without a final
LF gets one. Standalone CR bytes remain data; consequently a trailing standalone
CR followed by the appended LF forms a CRLF pair. Empty files stay empty.
Binary mode emits every original byte without conversion or a final newline.
Saving does not rewrite in-memory bytes or shift view offsets.

The destination directory receives an exclusively created temporary file. An
existing destination is renamed to an unused backup before the staged file is
renamed into place. Failures preserve pending edits; failed rollback retains
both recovery files and reports `ED_RECOVERY`. Permission bits are copied, but
this is not a guarantee of all metadata preservation, concurrent-writer safety
or power-loss durability.

Read `test_core.c` for random edits and LF/CRLF rules, `test_storage.c` for the
1 MiB backing-store path, and `test_failures.c` for injected allocation and I/O
failures. Byte-exact file comparisons should use binary mode.
