# cli.c and cli.h — command-line arguments

`cli_parse()` converts `argv` into a `CliOptions` structure. It does not open
files or start the terminal. `FileSpec` stores a path pointer, a read-only flag
and a binary row width. These pointers borrow strings from `argv`; they must not
be freed separately. `CLI_MAX_FILES` is 32, while the editor itself allows nine
open documents.

The parser walks arguments from left to right. Local `ro` and `width` variables
are copied into each subsequent file specification. Thus `-R one -W two` opens
`one` read-only and `two` writable. `-B` sets a global monochrome flag. Numeric
options select binary width, clamped to 1021; `-0` restores text mode. Numeric
accumulation stops growing after the supported bound to avoid integer overflow.

`--` ends option recognition, so `-- -notes.txt` names a file. Only a leading
hyphen starts an option; `/tmp/notes.txt` is an ordinary Linux path. `-h` and
`--help`, unknown option letters and malformed numeric options request help.
The existing letter-option parser examines the first letter after `-`; it does
not implement a general long-option or bundled-option grammar.

`main()` in editor.c uses the parsed data and calls `open_spec()` to expand file
filters. Separating parsing from file access keeps tests deterministic: see
`tests/test_cli.c` for flag scope, absolute paths, bounds and `--`.
