###################################################
#            Basic configuration
###################################################

TEMPLATE = app
TARGET   = gg
VERSION  = 1.0.0 # embedded in the exe's version resource; the installer reads its version from there
DEFINES += GG_VERSION=\\\"$$VERSION\\\"

include(../global.pri)

QT = core gui widgets svg   # the qsvg imageformats plugin renders the QSS glyphs; windeployqt only ships it when Qt Svg is linked
QT += core-private   # src/theme.cpp includes qtutils theme/cthemeiconhandler.h (private QAbstractFileEngineHandler)

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
	src/commitgraph.h \
	src/commitgraphdelegate.h \
	src/commitwindow.h \
	src/consolelogview.h \
	src/diffhighlighter.h \
	src/diffpane.h \
	src/filelistdelegate.h \
	src/gitparsers.h \
	src/gitprocess.h \
	src/gitrepository.h \
	src/hgcommandserver.h \
	src/hgparsers.h \
	src/hgprocess.h \
	src/hgrepository.h \
	src/historymodels.h \
	src/historywindow.h \
	src/messageedit.h \
	src/queryround.h \
	src/repository.h \
	src/repositoryfactory.h \
	src/settings.h \
	src/settingspages.h \
	src/stylesheet.h \
	src/theme.h \
	src/vcsprocess.h \
	src/vcstypes.h

SOURCES += \
	src/changedfilesmodel.cpp \
	src/commitgraph.cpp \
	src/commitgraphdelegate.cpp \
	src/commitwindow.cpp \
	src/consolelogview.cpp \
	src/diffhighlighter.cpp \
	src/diffpane.cpp \
	src/filelistdelegate.cpp \
	src/gitparsers.cpp \
	src/gitprocess.cpp \
	src/gitrepository.cpp \
	src/hgcommandserver.cpp \
	src/hgparsers.cpp \
	src/hgprocess.cpp \
	src/hgrepository.cpp \
	src/historymodels.cpp \
	src/historywindow.cpp \
	src/main.cpp \
	src/messageedit.cpp \
	src/repository.cpp \
	src/repositoryfactory.cpp \
	src/settingspages.cpp \
	src/theme.cpp \
	src/vcsprocess.cpp

RESOURCES += res/theme.qrc

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
