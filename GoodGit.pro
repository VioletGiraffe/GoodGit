TEMPLATE = subdirs

win32:SUBDIRS += launcher   # the gg.exe that goes on PATH and loads gg.dll, see launcher/main.cpp
SUBDIRS += app autoupdater cpputils cpp-template-utils qtutils thin_io

autoupdater.subdir = github-releases-autoupdater

win32:launcher.depends = app   # building the launcher builds the dll it loads, though it links nothing of it
qtutils.depends = cpputils cpp-template-utils
autoupdater.depends = cpp-template-utils
app.depends = autoupdater cpputils cpp-template-utils qtutils thin_io
