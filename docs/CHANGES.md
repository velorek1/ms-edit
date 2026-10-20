# Version 0.5 changes

Prepared on 20 October 2026 from the published repository
https://github.com/velorek1/ms-edit at commit `7220ff10c43d5c83983761aa5a405773e176d2d7`.

- Enter inserts LF in text mode; text saving converts CRLF to LF. Binary saving
  remains byte-exact. Existing final-newline behavior is retained.
- Vertical scrollbars display a proportional solid thumb over a shaded track,
  support dragging with a stable grab offset, and clamp movement at endpoints.
  Track/arrow clicking, split panes and binary rows are covered by tests.
- Buffer edits maintain an LF count for efficient scrollbar sizing.
- All Markdown documentation is in English. The source guide explains all C
  files and headers, their functions, data flow, design choices and limitations.
- README licensing text now agrees with the existing published MIT LICENSE;
  the license itself is unchanged.

Validation: strict C89 build; five C executables; 31 pseudo-terminal interface
cases; core AddressSanitizer/UndefinedBehaviorSanitizer with leak detection off.
See [TESTING.md](TESTING.md) and the included logs for scope and limitations.

This package contains source and documentation. Build with `make` and run
`./editr`. It does not include the original Microsoft binary or a compiled
Linux executable. No changes have been pushed to the public repository.
