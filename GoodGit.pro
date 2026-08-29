TEMPLATE = subdirs

SUBDIRS += app autoupdater cpputils cpp-template-utils qtutils thin_io
win32:SUBDIRS += launcher   # the gg.exe that goes on PATH, see launcher/main.cpp

autoupdater.subdir = github-releases-autoupdater

qtutils.depends = cpputils cpp-template-utils
autoupdater.depends = cpp-template-utils
app.depends = autoupdater cpputils cpp-template-utils qtutils thin_io
