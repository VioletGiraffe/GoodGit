# Git, made good

* Smooth, no-bullshit commit workflow: no staging, no "detached HEAD", first class submodule support.
* Supports Mercurial (hg) too.
* No login required in the app, uses your environment git/hg as configured.


Design and component overview: [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md).

## Requirements

macOS: 13.3 or newer\
Windows: 10 1809+ / 11 (any build)\
\
Git: 2.25 or newer (January 2020)\
Mercurial: no known floor

## Building

Qt 6.8 or newer, and a C++23 compiler. Clone with submodules:

```
git clone --recurse-submodules https://github.com/VioletGiraffe/GoodGit.git
```

Then build the top-level `GoodGit.pro` with qmake (or open it in Qt Creator). The binary is `bin/<config>/gg`, and `bin/<config>/GoodGit.app` on macOS.

## Usage

```
gg [path]
```

Opens the commit window for the repository containing `path` (default: the current directory).
