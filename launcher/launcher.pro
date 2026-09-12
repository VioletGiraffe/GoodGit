TEMPLATE = app
TARGET   = gg   # the name typed in a terminal; the gg.dll it loads carries the same name one level up
QMAKE_PROJECT_NAME = launcher   # a VS solution cannot hold two projects named after the same target

include(../version.pri)
include(../global.pri)

RC_ICONS = ../app/res/goodgit.ico   # the exe's icon; the runtime window icon is the SVG in the app's theme.qrc

CONFIG -= qt console
CONFIG += windows   # no console window when started from Explorer or the Run dialog

Release:OUTPUT_DIR=release/
Debug:OUTPUT_DIR=debug/

# DESTDIR is the directory that goes on PATH; OBJECTS_DIR must differ from the app's, both having a main.cpp
DESTDIR     = ../bin/$${OUTPUT_DIR}launcher
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/launcher

SOURCES += main.cpp

LIBS += -lkernel32 -luser32
