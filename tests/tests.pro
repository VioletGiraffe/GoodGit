# The unit tests, built on their own: not part of GoodGit.pro.
# The static libraries the tests link are built first, then the tests themselves (unittests.pro).

TEMPLATE = subdirs

SUBDIRS += thin_io unittests

# .subdir, not .file: with .file, qmake -r writes Makefile.thin_io while the subdirs Makefile runs Makefile
thin_io.subdir = ../thin_io
unittests.file = unittests.pro
unittests.depends = thin_io
