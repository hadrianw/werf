# werf - mouse driven text editor

Werf is mouse driven experimental text editor inspired by Acme, text editor from the Plan 9 from Bell Labs.

![werf-0](https://cloud.githubusercontent.com/assets/154291/16540376/296a9b72-4063-11e6-9a12-79dccc3e365f.png)

## Requirements

libX11, libcairo, libfreetype, libfontconfig

Compile with GNU or BSD make. Uses C11 only for anonymous structs as rest is C99.

## Features and non-features

- Mouse driven
- Simple integration with shell
- UTF-8 only
- Unlimited undo
- Uses proportional font by default
- No syntax highlighting
- No text wrapping yet
- Can't save yet...

## Keybindings

- Default X input method for text entry.
- Enter for new line.
- Same as in WIMP interfaces:
  - Arrows
  - Home/End
  - Page Up/Down
  - Backspace/Delete

Will be removed or changed:

- F2 - undo
- F3 - redo

## Command toolbars

Toolbar is a place for commands that can be applied to selection or the file as a whole. It is divided in two sections. First is for pinned commands and is by default populated by a few built-in commands. Second is for ad-hoc commands. It starts with "..." - a space where new command can be issued. On the right side of "..." is a list of previously issued non-pinned commands. The list is filtered while typing.

There are two such toolbars: selection and top toolbar.

### Selection toolbar

Text can be selected with mouse (left click and drag). After text is selected
and LMB is released selection-toolbar is shown. Toolbar does not overlap the text it splits it. Toolbar appears between lines bordering with selection start or end just under mouse pointer.

Default pinned commands: Cut, Copy, Paste, Delete,
Find, Open, Exec.

### Top toolbar

Top toolbar is for commands related more to the file as a whole. Default pinned commands: Save, Open, Undo, Redo, Paste, Find.

## Commands

Most edit actions are done through commands. Commands can act as simple filters - getting selection content from standard input and replacing it by giving standard output. Except for built-in commands commands are run with ``sh -c``.

- left click - execute command
- double click - edit command and select word under pointer
- triple click - edit command and select whole command line
- middle click - append space and clipboard contents to command and execute it

### Built-in commands

Built-in commands names are capitalized in CamelCase style.

Available built-in commands:

- Save [filename]
- Open [filename]
- Cut
- Copy
- Paste
- Delete
- Undo
- Redo
- Find [regex]

### Command pipes

Commands have more options where to read from or write to a file. They are spawned with additional pipes that are exposed by environmental variables thanks to /dev/fd mechanism.

Available pipes:

- WERF_CONTROL_W - control pipe

### Control pipe

Command can request special behavior from editor through control pipe (WERF_CONTROL_W). Control pipe expects available command name with new line at the end.

Available control commands:

- ReadOnly - disregard changes to selection

It is also available as a simple command wrapper:

    $ cat cmd/ReadOnly
    #!/bin/sh
    echo ReadOnly >> "$werf_control_w"
    exec "$@"

- disregard - selection is read only, disregard writes to selection - rename to ReadOnly
- finish - finish as soon as write off selection is done, probably can be removed, as with jobs toolbar it would not have much use

## TODO

### Now

- top toolbar
  - Save
  - Load
- Find command
  - WERF_AFTER_SELECTION_R - wrap around end?
  - WERF_BEFORE_SELECTION_R - wrap around begin?
  - WERF_HIGHLIGHT_W
     - maybe fold to control pipe? same thing for range?
     - Highlight 12 0 13 0 # highlight whole line 12
- single poll loop (also for command execution)
- command issuing

### Internal

- proper marking of dirty caches
- optimize array grow strategy
- use arrays in buckets for line and lines?
- memory optimization
  - use uint32_t instead of size_t?
  - intrusive array: put nmemb and amemb before payload
- split backend/frontend for ssh tunneling

### Presentation

- cursor blinking
- implement a bit of material design (shadows, but not animation)
- limit direct use of Xlib to only necessary parts?
  - use SDL?
  - draw text to single alpha channel
  - disable subpixel hinting
  - it seems that SDL_Texture does not support single channel pixel formats

### Simpler

- load from stdin and save to stdout
- right click to show selection toolbar
- save undo buffer as mmapped file like vim's swap files
- double click to select word, triple to select line
- extending selection: handles or shift+click
- tab while selected - increase indentation level
- proper tab stops
- keeping indentation level (copy leading white space)
- mark white space at the end of the line
- detect and distinguish indentation style?
  - detect if tabs or spaces are used for indentation
  - mark not matching indentation 

### Later

- Jobs toolbar - shows running and exited commands
  - display standard error and exit code
  - Kill running command
  - Issue subcommands to running command (i.e. Next/Previous for Find)
  - show cursor for running job in text
      - backspace/delete the cursor to kill
- WERF_BEFORE_SELECTION_W
- WERF_AFTER_SELECTION_W
- Command pinning - drag and drop before "..."
- Scrollable toolbars
- multiview and multifile
- completion for:
  - command line
  - editing
- line wrapping
- smooth scrolling
- drag and drop:
  - selection as text
  - files as filenames
- visual scroll bar
  - with outline?
  - external process?
- built-in commands as multicall binary (symlinks to werf)
- shell mode
  - interactive shell
  - partial implementation of VT100 emulation?
- syntax highlighting?
- line numbers?

### After 1.0
- touch events
- elastic tabs?
- undo/redo as external command?
