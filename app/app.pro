###################################################
#            Basic configuration
###################################################

TEMPLATE = app
TARGET   = gg
macx:QMAKE_APPLICATION_BUNDLE_NAME = GoodGit   # the .app bundle is what the user sees on macOS; the binary inside it stays gg
macx:QMAKE_INFO_PLIST = res/Info.plist   # qmake generates a plist with no CFBundleName and no version keys
# The version is edited in src/version.h, which the code includes; this lifts it out of the GG_VERSION line
# for qmake, which needs it for the exe's file-version metadata and the macOS plist.
VERSION_DEFINE = $$cat($$PWD/src/version.h, lines)
# LITERAL_HASH, or the # would start a comment and swallow the rest of the line, quoted or not
VERSION_DEFINE = $$find(VERSION_DEFINE, "^$${LITERAL_HASH}define GG_VERSION")
VERSION = $$replace(VERSION_DEFINE, "[^0-9.]", "")
# Without this the makefile has no dependency on the header, so qmake would not re-run when the version changes
QMAKE_INTERNAL_INCLUDED_FILES += $$PWD/src/version.h

include(../global.pri)

QT = core gui widgets svg   # the qsvg imageformats plugin renders the QSS glyphs; windeployqt only ships it when Qt Svg is linked
QT += core-private   # src/theme.cpp includes qtutils theme/cthemeiconhandler.h (private QAbstractFileEngineHandler)
QT += network   # the autoupdater library queries the GitHub releases API

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
	../cpp-template-utils \
	../github-releases-autoupdater/src \
	../thin_io/src

###################################################
#                 SOURCES
###################################################

HEADERS += \
	src/changedfilesmodel.h \
	src/commitgraph.h \
	src/commitgraphdelegate.h \
	src/commitwindow.h \
	src/consolelogview.h \
	src/diffpane.h \
	src/difftextview.h \
	src/externalapps.h \
	src/filelistdelegate.h \
	src/filelistview.h \
	src/fileviewerwindow.h \
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
	src/textdiff.h \
	src/theme.h \
	src/unifieddiff.h \
	src/updatecheck.h \
	src/vcsprocess.h \
	src/vcstypes.h \
	src/version.h \
	src/welcomewindow.h

SOURCES += \
	src/changedfilesmodel.cpp \
	src/commitgraph.cpp \
	src/commitgraphdelegate.cpp \
	src/commitwindow.cpp \
	src/consolelogview.cpp \
	src/diffpane.cpp \
	src/difftextview.cpp \
	src/externalapps.cpp \
	src/filelistdelegate.cpp \
	src/filelistview.cpp \
	src/fileviewerwindow.cpp \
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
	src/textdiff.cpp \
	src/theme.cpp \
	src/unifieddiff.cpp \
	src/updatecheck.cpp \
	src/vcsprocess.cpp \
	src/welcomewindow.cpp

RESOURCES += res/theme.qrc

win32:RC_ICONS = res/goodgit.ico   # the exe's icon; the runtime window icon is the SVG in theme.qrc
macx:ICON = res/goodgit-mac.icns   # built from res/goodgit-mac.svg (Apple icon grid variant), see that file

###################################################
#                 LIBS
###################################################

LIBS += -L$${DESTDIR} -lautoupdater -lqtutils -lcpputils -lthin_io

mac*|linux*|freebsd*{
	PRE_TARGETDEPS += $${DESTDIR}/libautoupdater.a $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a $${DESTDIR}/libthin_io.a

	QMAKE_CXXFLAGS_WARN_ON += -Wno-missing-field-initializers
}

###################################################
#    Platform-specific sources, compiler options and libs
###################################################

mac*{
	LIBS += -framework AppKit

	HEADERS += src/commandlinetool_mac.h
	OBJECTIVE_SOURCES += src/commandlinetool_mac.mm
}
