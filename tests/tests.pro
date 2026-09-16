# Tests for the app's code, built on their own: not part of GoodGit.pro. They run in a QApplication, so they need a
# display: xvfb-run where there is none.
# Run from anywhere: the fixtures are found through the source tree.

TEMPLATE = app
TARGET = tests
CONFIG += console
CONFIG -= app_bundle
QT = core gui widgets svg   # svg: the theme's tinted glyphs

include(../global.pri)

Release:OUTPUT_DIR=release/
Debug:OUTPUT_DIR=debug/

DESTDIR = bin/$${OUTPUT_DIR}
OBJECTS_DIR = build/$${OUTPUT_DIR}
MOC_DIR = build/$${OUTPUT_DIR}
RCC_DIR = build/$${OUTPUT_DIR}

INCLUDEPATH += ../app/src ../cpputils ../cpp-template-utils ../qtutils ../thin_io/src

DEFINES += FIXTURES_DIR=\\\"$$PWD/fixtures/\\\"

win32: QMAKE_CXXFLAGS += /Fd$${OBJECTS_DIR}   # the compiler's pdb, which would otherwise land beside this file

HEADERS += \
	../app/src/changedfilesmodel.h \
	../app/src/fileicons.h \
	../app/src/filelistdelegate.h \
	../app/src/filelistview.h \
	../app/src/gitparsers.h \
	../app/src/gitprocess.h \
	../app/src/gitrepository.h \
	../app/src/movedblocks.h \
	../app/src/queryround.h \
	../app/src/repository.h \
	../app/src/settings.h \
	../app/src/textdiff.h \
	../app/src/theme.h \
	../app/src/unifieddiff.h \
	../app/src/vcsprocess.h \
	../app/src/vcstypes.h \
	../qtutils/appdialogs/csettingsnotifier.h

SOURCES += \
	../app/src/changedfilesmodel.cpp \
	../app/src/fileicons.cpp \
	../app/src/filelistdelegate.cpp \
	../app/src/filelistview.cpp \
	../app/src/gitparsers.cpp \
	../app/src/gitprocess.cpp \
	../app/src/gitrepository.cpp \
	../app/src/movedblocks.cpp \
	../app/src/repository.cpp \
	../app/src/textdiff.cpp \
	../app/src/theme.cpp \
	../app/src/unifieddiff.cpp \
	../app/src/vcsprocess.cpp \
	fileicons_tests.cpp \
	filelist_tests.cpp \
	gitrepository_tests.cpp \
	main.cpp \
	movedblocks_tests.cpp \
	unifieddiff_tests.cpp \
	vcstypes_tests.cpp

RESOURCES += ../app/res/theme.qrc

mac*{
	LIBS += -framework AppKit -framework UniformTypeIdentifiers
	HEADERS += ../app/src/fileicons_mac.h
	OBJECTIVE_SOURCES += ../app/src/fileicons_mac.mm
}

# The app sources use these library modules: compiled in here, without the whole static libraries
include(../cpputils/assert/assert.pri)
include(../cpputils/debugger/debugger.pri)
include(../qtutils/theme/theme.pri)

# thin_io has no .pri: its sources, as thin_io.pro selects them
SOURCES += ../thin_io/src/filesystem_error.cpp
win*: SOURCES += $$files(../thin_io/src/*_win.cpp)
else: SOURCES += $$files(../thin_io/src/*_linux.cpp)
