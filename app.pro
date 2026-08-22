TEMPLATE = subdirs

SUBDIRS += app cpputils cpp-template-utils qtutils
win32:SUBDIRS += launcher   # the gg.exe that goes on PATH, see launcher/main.cpp

qtutils.depends = cpputils cpp-template-utils
app.depends = cpputils cpp-template-utils qtutils
