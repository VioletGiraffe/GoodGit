CONFIG -= c++17
CONFIG -= c++2a
CONFIG += strict_c++ c++2b

mac* | linux* | freebsd {
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug

	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wextra

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

mac*{
	QMAKE_MACOSX_DEPLOYMENT_TARGET = 13.3
}

linux*{
	exists(/usr/bin/ccache)|exists(/usr/lib/ccache):CONFIG += ccache
}

win*{
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	QMAKE_CXXFLAGS += /MP /wd4251
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus /FS
	QMAKE_CXXFLAGS_WARN_ON = /W4

	Debug:QMAKE_CXXFLAGS += /JMC
	Debug:QMAKE_LFLAGS += /DEBUG:FASTLINK /INCREMENTAL

	Release:QMAKE_CXXFLAGS += /GL /Zi
	Release:QMAKE_LFLAGS += /DEBUG:FULL /OPT:REF /OPT:ICF /TIME /LTCG:INCREMENTAL
}

linux*:Release {
	QMAKE_CXXFLAGS += -flto=auto -ffat-lto-objects
	QMAKE_CFLAGS   += -flto=auto -ffat-lto-objects
	QMAKE_LFLAGS   += -flto=auto
}

mac*:Release {
	QMAKE_CXXFLAGS += -flto=thin
	QMAKE_CFLAGS   += -flto=thin
	QMAKE_LFLAGS   += -flto=thin
}
