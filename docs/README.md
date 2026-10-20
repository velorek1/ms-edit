# Learning the source

Start here if you can read basic C but have not written a terminal editor.
A document is a sequence of bytes; a view is a cursor and a window onto those
bytes; the terminal is an input/output device. Keeping these ideas separate
makes it possible to test editing without simulating a keyboard.

## Reading order

| Guide | Source and purpose |
|---|---|
| [CLI](cli.md) | `cli.c`, `cli.h`: turn arguments into file requests |
| [Buffer](buffer.md) | `buffer.c`, `buffer.h`: store and edit bytes safely |
| [Files](files.md) | `files.c`, `files.h`: navigate paths and directories |
| [Unicode](utf8.md) | `utf8.c`, `utf8.h`: bytes, characters and display columns |
| [Platform](platform.md) | `platform.h`: shared event and screen definitions |
| [Host](host.md) | `host.c`: read terminal input and present screen cells |
| [Editor](editor.md) | `editor.c`: connect commands, documents and views |
| [CP437](cp437.md) | `cp437.h`: legacy display glyphs |

## Follow one operation

For an Enter key, `platform_key()` returns 13. `main()` passes it to `handle()`,
which reaches `edit_key()`. In text mode, `insert_bytes()` inserts one LF byte
through `replace_active()` and `buf_replace()`. The next `draw()` builds screen
cells, and `platform_present()` sends changed rows to the terminal.

Saving follows a different path: F2 reaches `command(C_SAVE)`, `save_doc()` and
`buf_save()`. Saving streams normalized text to a temporary file before replacing
the destination. The in-memory document is not rewritten by saving.

## Build and experiment

Run `make` from the repository root, then `./editr DEMO.TXT`. Run `make test`
before and after modifying a function. C tests exercise storage and failure
contracts; Python tests drive the real executable through a pseudo-terminal,
which is a pair of file descriptors that behaves like a terminal.

The Makefile links six C translation units into one executable. Header files
share declarations between them; `cp437.h` is an initializer fragment, not an
ordinary interface header. `NOTICE.txt` describes provenance and `LICENSE`
contains the repository's MIT terms. `DEMO.TXT` and `DEMOES.TXT` are sample input.

For advanced reading, trace ownership and failure paths: identify who allocates
a buffer, who frees it, and when a failed operation is allowed to change state.
The guides describe actual implementation choices rather than claiming they
were recovered from Microsoft's program.
