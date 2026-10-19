# ZzLogg User Guide

This guide covers the current Qt 6 application. It can be read offline in the built-in documentation window.

<a name="contents"></a>

## Contents

- [Getting started](#getting-started)
- [Exploring log files](#exploring-log-files)
  - [Search syntax and marks](#search-syntax)
  - [Opening files](#opening-files)
  - [Encodings](#encodings)
  - [Predefined filters](#predefined-filters)
  - [Highlighters](#highlighters)
  - [Color labels](#color-labels)
  - [Changing log files](#changing-log-files)
  - [Scratchpad](#scratchpad)
- [Settings](#settings)
  - [General](#general)
  - [View](#view)
  - [File](#file)
  - [Storage](#storage)
  - [Advanced options](#advanced-options)
- [Keyboard commands](#keyboard-commands)
- [Mouse navigation](#mouse-navigation)
- [Command line options](#command-line-options)

<a name="getting-started"></a>

## Getting started

Start ZzLogg from the command line, the desktop application menu, or a file association. You can pass one or more file paths on the command line. With no file argument, the application attempts to restore the previous session according to the session settings.

On first use, choose where ZzLogg stores its settings, sessions, and application logs: the user data directory, the program's `data` directory, or a custom absolute path. You can review this choice later under Settings → Storage.

The main window has three working areas:

| Area | Purpose |
|---|---|
| Main view at the top | Browse the original log and its surrounding context. |
| Search input between the views | Enter text or a regular expression and run a search. |
| Filtered view at the bottom | Read matching and marked lines in their original file order. |

A search lists matching lines in the filtered view and marks them with red circles in both views. Select a result to locate it in the original log. Use the right-hand overview to see where matches occur throughout the file.

<a name="exploring-log-files"></a>

## Exploring log files

<a name="search-syntax"></a>

### Search syntax and marks

Regular expressions extract related events without losing their original order. Choose regular-expression mode for patterns, or fixed-string mode to search for literal text.

For example, to compare connection opening and closing events:

```text
Entering (Open|Close)Connection
```

The parentheses group the alternatives and `|` means “either”. An opening event without a corresponding closing event is easier to spot in the results. To include information about the connection type:

```text
Entering (Open|Close)Connection|Created a .* connection
```

Here `.*` matches any sequence of characters on a single line. The second alternative still requires `Created a` followed later by a space and `connection`.

#### Logical combinations

Enable logical search with the button beside the search input. Enclose every pattern in double quotes. Use the following operators to combine patterns:

| Operator | Meaning | Example |
|---|---|---|
| `and` | Both patterns must match the line. | `"x" and "y"` |
| `or` | Either pattern may match the line. | `"x" or "y"` |
| `&` | AND with left-to-right short-circuit evaluation. | `"x" & "y"` |
| <code>&#124;</code> | OR with left-to-right short-circuit evaluation. | <code>"x" &#124; "y"</code> |
| `not` | Negates the expression in parentheses. | `not("x")` |

Search history supplies autocomplete suggestions. Edit or clear it from the search input's context menu, and set its maximum size in General settings. Autocomplete respects the search's case-sensitivity option.

#### Match overview and marks

The overview on the right shows matches as red strokes and manually marked lines as blue strokes. Click a line's circular marker in the left margin, or select the line and press `m`, to toggle its mark. To mark several lines, select them and use `m` or the context menu.

By default, the filtered view includes both matches and marks. Switch its mode to show only marks or only matches when needed. Press `Ctrl+L` to jump to a particular line.

#### Regular-expression engines

ZzLogg uses Hyperscan for fast regular-expression searches. Some constructs, including lookahead, are unsupported by that engine. When Hyperscan cannot compile a pattern, ZzLogg falls back to Qt's PCRE-based regular-expression engine; those searches can be substantially slower. For detailed syntax limits, see the optional online [Hyperscan pattern reference](https://intel.github.io/hyperscan/dev-reference/compilation.html#pattern-support).

<a name="opening-files"></a>

### Opening files

Open logs through File → Open or the toolbar, drag files from a file manager, provide a remote URL, pass file paths on the command line, or use Recent Files and Favorites. Where an installed package registers a `.log` file association, double-clicking a log in the file manager can also open it in ZzLogg.

#### Archives and compressed files

Supported archives include `zip`, `7z`, and `tar`. ZzLogg extracts an archive into a temporary directory, then presents a file dialog so you can choose files from it.

Single compressed files in `gzip`, `bzip2`, `xz`, and `lzma` formats are decompressed into a temporary directory and opened. The format is detected from the file contents or extension. Extraction and confirmation preferences are under Settings → File.

#### Remote URLs

ZzLogg downloads a remote file into a temporary directory and opens the downloaded copy.

#### Recent files

The File menu provides quick access to up to five recently opened files.

#### Favorites

Use Favorites → Add to Favorites or the toolbar to bookmark an open file. Favorites are useful for logs you revisit too infrequently to keep them in the recent-file list.

#### Clipboard

Paste text from the clipboard into ZzLogg to save it to a temporary file and explore it as a log.

#### Switching between open files

Use View → Opened Files, or press `Ctrl+Shift+O` to open the file-switching dialog.

<a name="encodings"></a>

### Encodings

ZzLogg attempts to detect a file's encoding. If the result is wrong, select the correct encoding from the Encoding menu. File settings also let you choose a default encoding instead of automatic detection.

<a name="predefined-filters"></a>

### Predefined filters

Save frequently used patterns as predefined filters through the Tools menu. Each filter has a display name, a pattern, and a choice between regular-expression and plain-text matching.

Use the drop-down beside the search input to add one or more predefined patterns to a search. You can also save the current pattern as a predefined filter from the search input's context menu.

<a name="highlighters"></a>

### Highlighters

Highlighters color log text to make errors or particular event types easier to recognize. Rules are organized into sets, selected through the context menu or Tools → Highlighters. You can apply multiple sets to an open file.

A set can contain any number of rules. Each rule specifies a regular expression or a plain-text pattern and foreground/background colors. Logical search combinations are not supported in highlighter rules.

Apply a rule to the entire matching line or just the matching text. When highlighting matching text, a regular expression with capture groups highlights the captured portions. Color variation gives different strings matching the same pattern slightly different colors.

Rule order and set order affect the result: rules are evaluated from bottom to top, and subsequent matching rules override the applicable colors. Arrange higher-priority rules accordingly.

Export highlighter settings with a `.conf` extension to import them on another machine. Each set has a unique ID; import adds only sets whose IDs are not already present.

<a name="color-labels"></a>

### Color labels

Color labels create quick highlighting rules from selected text. Nine labels are available by default. Assign a label through the context menu or `Ctrl+Shift+1` through `Ctrl+Shift+9`. Multiple strings can share one label. `Ctrl+D` applies the next label to the selection.

To remove a label from selected text, select it and choose None in the context menu. `Ctrl+Shift+0` clears all color labels. Customize the colors in the Color Labels tab of the highlighter settings dialog.

<a name="changing-log-files"></a>

### Browsing changing log files

ZzLogg can display and search a log while another program writes to it. The displayed log updates as it grows. Enable Auto-refresh to update search results automatically as well.

Press `f` to follow the end of the file, like `tail -f`. If new lines are appended, matching lines can be added to the results; if the file is overwritten, the previous results are cleared.

To distinguish appends from overwrites, ZzLogg normally recalculates the hash of the indexed portion. This is more reliable, but can be slow for large files or network shares. Fast modification detection checks only the beginning and end, reducing work but potentially missing changes in the middle. Choose the strategy under Settings → File.

Following requires file-change monitoring. If both native monitoring and polling are disabled, following is disabled too.

<a name="scratchpad"></a>

### Scratchpad

Use the Scratchpad to inspect Base64 text or format XML/JSON from logs. Send selected text to the current scratchpad tab through the context menu or `Ctrl+Z`. To replace the tab's contents, use the corresponding context-menu action or `Ctrl+Shift+Z`.

Inside the Scratchpad, `Ctrl+N` opens a new tab. These shortcuts depend on which window has focus.

<a name="settings"></a>

## Settings

<a name="general"></a>

### General

#### Search options

| Option | Effect |
|---|---|
| Extended regular expressions | Interpret patterns using regular-expression syntax. |
| Fixed strings | Match text literally; punctuation has no special meaning. |
| Incremental QuickFind | Restart QuickFind as the pattern changes. |
| Highlight matched text | Highlight matching portions in the main and filtered views. |
| Color variation | Give different matching strings slightly different highlight colors. |
| Search history size | Limit the number of patterns saved for autocomplete. |
| Search on add or replace pattern | Immediately search when the context menu updates the pattern. |

Search-type settings apply to filtering and QuickFind.

#### Session options

- Load last session restores previously open files, view configuration, marks, and follow mode.
- Follow file on load enables follow mode for newly opened files.
- Minimize to tray keeps the application in the system tray when closing the main window. Exit through the tray menu or File → Exit. This option is unavailable on macOS.
- Enable multiple windows allows File → New Window. Closing windows individually leaves the last closed window as the saved session; File → Exit saves all windows for restoration.

#### Language

Choose Simplified Chinese, Traditional Chinese, or English. Applying the language setting updates the interface immediately without restarting.

#### Version checking

Current builds have no update-manifest URL configured. The version checker therefore does not contact a release service or display automatic update notifications.

<a name="view"></a>

### View

#### Font

Choose a clear monospaced font, such as DejaVu Sans Mono. Force font antialiasing if the automatic settings produce poor text rendering. In either log view, use `Ctrl+Mouse wheel` to adjust the font size.

#### Theme and widget style

Choose Light or Dark for the interface theme. The theme controls the appearance of application surfaces and icons. A separate Qt widget-style selector controls how widgets are drawn; available styles depend on the platform. Some widget-style changes require restarting, as indicated by the application.

#### High DPI

The current application uses Qt 6 only. Qt normally handles display scaling automatically. If scaling looks wrong, review the available High DPI options, especially when using a fractional scale factor.

#### Line numbers and other display options

The unified line-number option controls line numbers in both the main and filtered views.

You can hide ANSI terminal color escape sequences in both views to make logs easier to read. This can slow regular-expression searches.

<a name="file"></a>

### File

#### File-change monitoring

Native monitoring uses operating-system notifications to reload changed files. For network shares or directories mounted through SFTP where notifications may be unreliable, enable polling to check for changes periodically.

Fast modification detection hashes only the beginning and end of the file. It improves speed, especially for large or remote files, but may miss changes in the middle. Disable it when detecting those changes is essential.

Scrolling beyond the end of a file can enable follow mode. Disable that behavior if you prefer to turn following on explicitly.

#### Encoding

Use automatic detection or specify an encoding for all newly opened files.

#### Archives

Enable archive extraction to detect and unpack supported archives and compressed files into a temporary directory. By default, extraction asks for confirmation. Enable extraction without confirmation to skip that prompt.

#### File download

By default, HTTPS downloads require valid certificate verification. The option to ignore SSL errors can support development servers using self-signed certificates, but disables that verification.

<a name="storage"></a>

### Storage

Settings, sessions, and application logs share a data directory. The startup chooser and Settings → Storage provide the same three choices:

| Location | Use |
|---|---|
| User data directory | Store data for the current operating-system account. |
| Program directory (`data`) | Keep data beside the program when that location is writable. |
| Custom directory | Use a writable directory specified by an absolute path. |

The page previews the data path and provides an Open Directory button. Changing location schedules a migration that takes effect after restarting; choose Restart Now or Restart Later when prompted.

The command-line option <code>--data-dir &lt;path&gt;</code> overrides the data directory for that process. When it controls the current directory, storage selection in the interface is managed by the command line.

<a name="advanced-options"></a>

### Advanced options

- Parallel search uses multiple CPU cores for regular-expression matching. It does not apply to QuickFind.
- Search is optimized by default for UTF-8 and single-byte encodings. If most files use other multibyte encodings, the non-Latin encoding optimization may improve performance.
- The search-results cache keeps matching line numbers in memory so repeated searches can reuse results instead of scanning the file again.
- Diagnostic logging has a configurable verbosity. Levels 4 or 5 are usually enough for troubleshooting. Application logs are stored in the selected data directory's `logs` subdirectory. Logging may slow searches.
- The current application has no crash-reporting feature.

<a name="keyboard-commands"></a>

## Keyboard commands

Log navigation follows conventions from `vi` and `less`. These are default bindings; customize actions in Settings → Shortcuts. Some bindings depend on the active view or window.

| Keys | Action |
|---|---|
| Up / Down; `j` / `k` | Move the selection down/up with `j`/`k`, or in the arrow's direction. A numeric prefix with `j`/`k` moves that many lines. |
| Left / Right; `h` / `l` | Scroll horizontally. |
| `Ctrl+Up` / `Ctrl+Down` | Scroll vertically. |
| `^` / `$` | Go to the beginning/end of the selected line. |
| `Ctrl+Home` | Jump to the start of the file. |
| `Shift+G` / `Ctrl+End` | Jump to the end of the file. |
| `Ctrl+L` | Open the jump-to-line dialog. |
| `'` / `"` | Start QuickFind forward/backward. |
| `n` / `Shift+N` | Repeat QuickFind forward/backward. |
| `*` / `.` | Find the next occurrence of selected text. |
| `/` / `,` | Find the previous occurrence of selected text. |
| `f` | Toggle following the end of the file. |
| `m` | Toggle marks on selected lines. |
| `[` / `]` | Jump to the previous/next mark. |
| `+` / `-` | Decrease/increase the filtered view's size. |
| `v` | Cycle filtered mode: marks and matches → marks → matches. |
| `F5` | Reload the current file. |
| `Ctrl+S` | Focus the search input. |
| `Ctrl+Shift+O` | Open the file-switching dialog. |
| `Ctrl+Shift+1`–`Ctrl+Shift+9` | Apply a color label to selected text. |
| `Ctrl+D` | Apply the next color label. |
| `Ctrl+Shift+0` | Clear all color labels. |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Send selection to the Scratchpad / replace its contents. |
| `Ctrl+N` in Scratchpad | Open a new scratchpad tab. |

The older manual's `[number]g`, `G`, and `Alt+G` are no longer current default bindings. Use `Ctrl+L` to jump to a line and `Ctrl+Home` to reach the start of the file; review or customize your bindings in Shortcut settings.

<a name="mouse-navigation"></a>

## Mouse navigation

| Gesture | Action |
|---|---|
| `Alt+Mouse wheel` | Scroll horizontally. |
| `Shift+Mouse wheel` | Scroll faster. |
| `Ctrl+Mouse wheel` | Change the log font size. |

<a name="command-line-options"></a>

## Command line options

Pass one or more file paths after the options. Quote paths containing spaces. The executable name below is the Windows example; use the installed executable name on your platform.

```text
ZzLogg.exe "C:\Logs\server.log"
ZzLogg.exe --follow "C:\Logs\server.log"
ZzLogg.exe --data-dir "D:\ZzLoggData" --new-session "C:\Logs\server.log"
ZzLogg.exe --log --debug 2
```

| Option | Effect |
|---|---|
| `-h, --help` | Print help and exit. |
| `-v, --version` | Print version information. |
| `-m, --multi` | Allow multiple application instances; use with `-s` to load the saved session. |
| `-s, --load-session` | Load the previous session; default when no file is supplied. |
| `-n, --new-session` | Do not load the previous session; default when a file is supplied. |
| `-l, --log` | Write application diagnostic output to a log file. |
| `-f, --follow` | Follow initially opened files. |
| <code>-d, --debug &lt;debug_level&gt;</code> | Increase diagnostic verbosity by a numeric amount, for example `-d 2`. Repeated letters such as `-dddd` are not the current syntax. |
| <code>--data-dir &lt;path&gt;</code> | Use an absolute data directory for this process. |
| <code>--window-width &lt;width&gt;</code> | Set the new window's width. |
| <code>--window-height &lt;height&gt;</code> | Set the new window's height. |

The console-mode parser additionally supports <code>-e, --pattern &lt;pattern&gt;</code> for its search pattern; the graphical application's parser does not expose that option. Use `--help` on the executable you are running to check its available options.

[Back to contents](#contents)
