# files.c and files.h — paths and directory lists

The file selector needs directory entries without knowing POSIX details.
`FsEntry` is one linked-list node containing a name and directory flag;
`FsList` owns the head and count. `fs_free()` releases that ownership.

`fs_cwd()` retrieves the working directory. `fs_join()` combines directory and
name, while an absolute name replaces the directory. `fs_split()` separates the
last slash; without one, it uses the current directory. All reject overlong
paths instead of truncating them. Backslashes and colons are ordinary Linux
filename characters. `fs_absolute()` calls `realpath()`, so its input must exist;
it is not a general canonicalizer for a new save destination. `fs_isdir()` uses
`stat()` and follows symlinks.

`fs_match()` implements `*` and `?`, with ASCII-style case-insensitive comparison
and historical DOS conventions: `*.*` also matches extensionless names. A saved
star/retry position implements backtracking without recursive calls. Filesystem
names themselves remain case-sensitive on ordinary Linux filesystems; matching
is a user-interface policy.

`fs_list()` opens a directory, filters files or folders, and builds a temporary
list. Internal `add()` inserts names in order using `compare()`; `fs_at()` walks
to an indexed entry. Only successful construction replaces the caller's list,
so a reported refresh failure keeps the previous list available. The linked
list keeps allocation simple, but indexed lookup and sorted insertion are not
constant-time. See `tests/test_files.c` for filters, path semantics and refresh
failure behavior.
