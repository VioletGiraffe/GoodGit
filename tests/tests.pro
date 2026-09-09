# Unit tests for the backend- and UI-free parts of the app, built on their own: not part of GoodGit.pro.
# Run from anywhere: the fixtures are found through the source tree.

TEMPLATE = app
TARGET = tests
CONFIG += console
CONFIG -= app_bundle
QT = core

include(../global.pri)

Release:OUTPUT_DIR=release/
Debug:OUTPUT_DIR=debug/

DESTDIR = bin/$${OUTPUT_DIR}
OBJECTS_DIR = build/$${OUTPUT_DIR}
MOC_DIR = build/$${OUTPUT_DIR}

INCLUDEPATH += ../app/src ../cpputils ../cpp-template-utils

DEFINES += FIXTURES_DIR=\\\"$$PWD/fixtures/\\\"

mac*|linux*|freebsd*: QMAKE_CXXFLAGS_WARN_ON += -Wno-missing-field-initializers
win32: QMAKE_CXXFLAGS += /Fd$${OBJECTS_DIR}   # the compiler's pdb, which would otherwise land beside this file

HEADERS += \
	../app/src/movedblocks.h \
	../app/src/textdiff.h \
	../app/src/unifieddiff.h

SOURCES += \
	../app/src/movedblocks.cpp \
	../app/src/textdiff.cpp \
	../app/src/unifieddiff.cpp \
	main.cpp \
	movedblocks_tests.cpp \
	unifieddiff_tests.cpp

# The app sources assert through cpputils: these compile in what that needs, without the whole static library
include(../cpputils/assert/assert.pri)
include(../cpputils/debugger/debugger.pri)
