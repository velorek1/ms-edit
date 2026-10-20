# Origin of the analysis

This release descends from `msedit-re-0.3.1-linux-20261012.zip`, produced during
Javier and Raúl's earlier work. The historical summary in the README was checked
against that package's `analysis/manifest.json`, `analysis/stub-verification.json`
and `docs/RE_NOTES.md`. These are historical reverse-engineering results, not new
Linux tests or a claim that all original source code has been recovered.

Two reduced historical records, `research-manifest.json` and
`stub-verification.json`, contain data and results. They do not contain the
executable and cannot reproduce decompression without the original reference.
DOS tools and their tests are no longer part of this Linux source distribution.

The initial analysis distinguished byte reconstruction, EXEPACK decompression,
resource extraction and new implementation. The editor is not a complete literal
translation of the executable. Its buffers, views and Linux backend were designed
for this implementation.

The 0.4 cleanup added integrated help and Escape-based exit, and removed DOS-only
options and path conventions. It retained the previous save conventions.
Version 0.5 changes text saving to LF and adds proportional, draggable vertical
scrollbars. Neither release completes Unicode, undo, printing or persistent
settings.
