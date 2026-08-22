###################################################
#            Basic configuration
###################################################

TEMPLATE = app
TARGET   = gg
macx:QMAKE_APPLICATION_BUNDLE_NAME = GoodGit   # the .app bundle is what the user sees on macOS; the binary inside it stays gg
macx:QMAKE_INFO_PLIST = res/Info.plist   # qmake generates a plist with no CFBundleName and no version keys
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
	src/recentrepositories.h \
	src/recentrepositoriespanel.h \
	src/repository.h \
	src/repositoryfactory.h \
	src/repositorywindows.h \
	src/settings.h \
	src/settingspages.h \
	src/stylesheet.h \
	src/theme.h \
	src/vcsprocess.h \
	src/vcstypes.h \
	src/welcomewindow.h

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
	src/recentrepositories.cpp \
	src/recentrepositoriespanel.cpp \
	src/repository.cpp \
	src/repositoryfactory.cpp \
	src/repositorywindows.cpp \
	src/settingspages.cpp \
	src/theme.cpp \
	src/vcsprocess.cpp \
	src/welcomewindow.cpp

RESOURCES += res/theme.qrc

win32:RC_ICONS = res/goodgit.ico   # the exe's icon; the runtime window icon is the SVG in theme.qrc
macx:ICON = res/goodgit-mac.icns   # built from res/goodgit-mac.svg (Apple icon grid variant), see that file

###################################################
#                 LIBS
###################################################

LIBS += -L$${DESTDIR} -lqtutils -lcpputils

mac*|linux*|freebsd*{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

###################################################
#    Platform-specific sources, compiler options and libs
###################################################

mac*{
	LIBS += -framework AppKit

	HEADERS += src/commandlinetool_mac.h
	OBJECTIVE_SOURCES += src/commandlinetool_mac.mm
}
