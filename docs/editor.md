# editor.c — commands, documents and views

This is the application's coordinator. It owns up to nine `Buffer` documents,
two `View` records, selection/clipboard state, menus and the main event loop.
Start at `main()`: parse arguments, open requested files, initialize the terminal,
then repeat `draw()`, `platform_present()` and `handle(platform_key())`.

## Documents versus views

A document owns bytes. A view holds a document number, cursor, selection anchor,
scroll offsets and preferred column for vertical movement. Two views can display
the same document. `active()` finds the current Buffer; `save_view()` copies its
working positions to the focused View and `load_view()` restores another View.
`select_doc()` changes which document the focused view displays.

`replace_active()` calls the storage edit and uses `changed_position()` to shift
positions in other views sharing that document. Without this step, inserting
text before another view's cursor would leave that cursor pointing at the wrong
content. The active edit position is updated by the Buffer operation itself.

## Constructing the screen

`cell()` clips individual writes; `fill()`, `text()`, `hottext()` and `box()` build
regions, labels and borders. The `string_character`, `string_next`, `string_prev`,
`string_columns` and `field_start` helpers let dialog text navigate UTF-8 and fit
visible columns. `selection()` orders the cursor and anchor into a range.

`ensure_visible()` scrolls enough to keep the cursor visible. `draw_pane()` walks
logical lines, expands tabs, marks selected bytes and creates wide-character
continuation cells. `draw()` renders each view and the status bar, restoring the
original focus afterward. A too-small terminal gets a resize message.

`scrollbar()` computes a proportional thumb from total logical lines, visible
height and top line. It uses the maintained LF count, or fixed-width binary rows,
so sizing does not scan the whole document each frame. A short document fills
the track; a long one gets at least one thumb cell. Very small split panes can
have arrows but no track. Integer rounding makes movement discrete.

`mouse_main()` distinguishes text selection, split-divider dragging and scrollbar
dragging. Clicking the thumb remembers where inside it the grab occurred, which
avoids a jump on press. Movement is clamped to the track even outside the column;
release stops dragging. Track clicks center the thumb near that position, and
arrows move one logical line. `scroll_to()` moves to the requested line and
places cursor and anchor at its start, preserving the earlier click behavior
and preventing `ensure_visible()` from undoing the scroll. Long jumps walk lines;
there is no random-access line index.

## Dialogs and file operations

`dialog_ready()`, `dialog_frame()`, `dialog_button()`, `button_hit()` and
`dialog_body()` share geometry, style and mouse hit testing. `message()` displays
information, `error()` translates error codes, `question()` asks for a choice,
and `prompt()` edits a text field. `help()` shows the built-in command reference.

`file_dialog()` combines filename fields, directory/file lists, filters and
buttons. `open_dialog()` handles a chosen file; `open_spec()` expands startup
patterns; `open_path()` loads or selects a document. `new_doc()` and `close_doc()`
manage document slots. `save_doc()` handles Save/Save As and `confirm_close()`
asks whether to save pending changes. Cancel leaves the editor open.

## Editing and dispatch

`command()` implements menu actions; `disabled()` marks unavailable actions and
`menu()` performs menu navigation. `handle()` dispatches events and shortcuts.
`move_cursor()` handles navigation, using `isword_at()` for word movement.
`edit_key()` handles insertion, deletion and Enter. Enter inserts LF in text mode;
CRLF loaded from older files remains navigable as a line ending.

`copy_region()` fills the internal clipboard and optionally removes selected
bytes. `insert_bytes()` replaces selection with supplied bytes. `find_options()`,
`find_text()` and `replace_text()` gather search choices and invoke buffer-level
matching. `usage()` is the noninteractive command-line help.

To follow a change safely, identify its command path, the storage mutation and
all view updates. Test real saved bytes as well as UI feedback. `test_ui.py`
covers menus, clipboard, selection, shared views, mouse dragging, resizing,
search, help and exit confirmation. Undo, printing, full Unicode and persistent
settings are outside this implementation.
