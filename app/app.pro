###################################################
#            Basic configuration
###################################################

TEMPLATE = app
TARGET   = GoodGit

QT = core gui widgets

CONFIG += strict_c++ c++latest

mac* | linux* | freebsd {
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

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

win*{
	QMAKE_CXXFLAGS += /MP /wd4251
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus /FS
	QMAKE_CXXFLAGS_WARN_ON = /W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX _SCL_SECURE_NO_WARNINGS

	Debug:QMAKE_LFLAGS += /DEBUG:FASTLINK /INCREMENTAL

 	Release:QMAKE_CXXFLAGS += /GL
	Release:QMAKE_LFLAGS += /DEBUG:FULL /OPT:REF /OPT:ICF /TIME /LTCG:INCREMENTAL
}

mac*{
	LIBS += -framework AppKit

	QMAKE_POST_LINK = cp -f -p $${DESTDIR}/*.dylib $${DESTDIR}/$${TARGET}.app/Contents/MacOS/ || true
}

###################################################
#      Generic stuff for Linux and Mac
###################################################

linux*|mac*|freebsd {
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wextra

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}
