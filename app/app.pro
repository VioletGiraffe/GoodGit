###################################################
#            Basic configuration
###################################################

TEMPLATE = app
TARGET   = gg

include(../global.pri)

QT = core gui widgets

Release:OUTPUT_DIR=release/
Debug:OUTPUT_DIR=debug/

DESTDIR  = ../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}

###################################################
#               INCLUDEPATH
###################################################

INCLUDEPATH += \
	../qtutils \
	../cpputils \
	../cpp-template-utils

###################################################
#                 SOURCES
###################################################

HEADERS += \
	src/changedfilesmodel.h \
	src/commitwindow.h \
	src/diffhighlighter.h \
	src/gitparsers.h \
	src/gitprocess.h \
	src/messageedit.h \
	src/repository.h \
	src/settings.h

SOURCES += \
	src/changedfilesmodel.cpp \
	src/commitwindow.cpp \
	src/diffhighlighter.cpp \
	src/gitparsers.cpp \
	src/gitprocess.cpp \
	src/main.cpp \
	src/messageedit.cpp \
	src/repository.cpp \
	src/settings.cpp

###################################################
#                 LIBS
###################################################

LIBS += -L$${DESTDIR} -lqtutils -lcpputils

mac*|linux*|freebsd*{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

###################################################
#    Platform-specific compiler options and libs
###################################################

mac*{
	LIBS += -framework AppKit

	QMAKE_POST_LINK = cp -f -p $${DESTDIR}/*.dylib $${DESTDIR}/$${TARGET}.app/Contents/MacOS/ || true
}
