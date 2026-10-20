# Validation of version 0.5

Date: 20 October 2026. Environment: Linux, GCC 13.3.0, Python 3.

## Reproduction

```sh
make clean
make
make test
```

Compilation uses `-std=c89 -pedantic -Wall -Wextra -Werror -O2`.
All five C test executables and 31 Python interface tests pass.
The current run is recorded in [validation.txt](validation.txt).

- Core: 8,000 random edits compared against an independent byte model, including
  the maintained LF count; search, tabs, CRLF navigation, binary files and the
  64 KiB boundary.
- Text saving: empty input, missing final LF, LF, CRLF, mixed endings, standalone
  CR, and a 110,000-byte document with CRLF crossing the 4 KiB boundary. The same
  large document is also saved in binary mode and compared byte for byte.
- Storage: a 1 MiB document with replacement, insertion, deletion, save and
  reload; content is compared exactly in binary mode.
- Failures: write, close, rename, rollback, permissions and allocation.
- CLI: per-file options, absolute paths, `--` and binary-width bounds.
- Files: filters, directory listing, failed refresh and Linux paths in which
  colon and backslash are filename characters rather than DOS separators.
- Real interface under a pseudo-terminal: editing, save, clipboard, search,
  Spanish UTF-8, read-only files, mouse, shared views and resizing. Help works
  from F1, menus and the file selector. Escape exits with save/cancel/discard
  choices, or returns to the previous context when used inside help.
- Five additional scrollbar cases cover drag endpoints, motion outside the
  scrollbar column, release, track clicks, arrow clicks, empty/short files,
  binary rows, split panes, resizing and a minimum-height split with no track.
  Tests insert a marker after scrolling and verify its exact saved location.
  Initial output is checked for the Unicode block and shaded-track glyphs.

The help test waits for the body as well as the title to avoid racing the
editor process while its output is still arriving.

## Memory and visual inspection

The core test also passes AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cc -std=c89 -pedantic -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc \
  tests/test_core.c src/buffer.c src/utf8.c -o build/test-core-asan
ASAN_OPTIONS=detect_leaks=0 ./build/test-core-asan
```

The result is in [sanitizers.txt](sanitizers.txt). Leak detection was disabled;
this run makes no claim about LeakSanitizer or complete absence of memory leaks.

[scrollbar.png](scrollbar.png) renders captured ANSI output from the actual 0.5
executable before and after dragging: the viewport moves from line 1 to line 40
and the four-cell thumb moves down its shaded track. The capture was inspected
visually, and thumb cells were checked programmatically. It is a reconstruction
of terminal cells, not a screenshot of a particular terminal emulator.

`help.png` is a historical 0.4 capture at 75×24 and still shows the former CRLF
policy. The current help text says LF. Existing tests cover help at minimum size
and recentering/mouse closure at 100×30. Image rendering is not a runtime or test
dependency of the editor.

## Scope

The suite does not cover every Linux terminal, keyboard sequence, filesystem or
Unicode interaction. Scrollbar size uses logical lines and integer rounding;
large jumps still walk intervening lines. The tests do not prove equivalence
with the historical Microsoft program. Original binary materials and their
unpacking tests are neither distributed nor run by `make test`.
