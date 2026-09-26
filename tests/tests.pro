# The unit tests, built on their own: not part of GoodGit.pro.
# The static libraries the tests link are built first, then the tests themselves (unittests.pro).

TEMPLATE = subdirs

SUBDIRS += thin_io unittests

thin_io.file = ../thin_io/thin_io.pro
unittests.file = unittests.pro
unittests.depends = thin_io
