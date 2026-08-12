# GoodGit

A git commit GUI that doesn't make you think about the index. Check the files you want, write a message,
commit; one click to push. Whole-file granularity, no staging concept in the UI - if you know TortoiseGit's
commit dialog, you know this.

- The file list is the HEAD-to-working-tree delta; committing exactly the checked files never touches the index
- Embedded diff pane; double-click opens the external difftool
- Submodule-aware: a submodule row opens its own commit window, push pushes referenced submodule commits first
- Detached HEAD is reattached automatically when that cannot move the working tree, refused otherwise
- Everything is `git` CLI subprocesses - your config, hooks and credentials apply as-is

Design and component overview: [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md).

## Building

Qt 6 and a C++23 compiler. Clone with submodules:

```
git clone --recurse-submodules https://github.com/VioletGiraffe/GoodGit.git
```

Then build the top-level `app.pro` with qmake (or open it in Qt Creator). The binary is `bin/<config>/gg`.

## Usage

```
gg [path]
```

Opens the commit window for the repository containing `path` (default: the current directory).
