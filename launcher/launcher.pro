TEMPLATE = app
TARGET   = gg   # the name typed in a terminal; the application it starts carries the same name one level up
QMAKE_PROJECT_NAME = launcher   # a VS solution cannot hold two projects named after the same target

include(../global.pri)

CONFIG -= qt console
CONFIG += windows   # no console window when started from Explorer or the Run dialog

Release:OUTPUT_DIR=release/
Debug:OUTPUT_DIR=debug/

# Both paths must differ from the app's: the target name is the same, and so is the source file name
DESTDIR     = ../bin/$${OUTPUT_DIR}launcher
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/launcher

SOURCES += main.cpp

LIBS += -lkernel32 -luser32
