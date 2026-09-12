# The version is edited in app/src/version.h, which the code includes; this lifts it out of the GG_VERSION line
# for qmake, which needs it for the binaries' file-version metadata and the macOS plist.
VERSION_DEFINE = $$cat($$PWD/app/src/version.h, lines)
# LITERAL_HASH, or the # would start a comment and swallow the rest of the line, quoted or not
VERSION_DEFINE = $$find(VERSION_DEFINE, "^$${LITERAL_HASH}define GG_VERSION")
VERSION = $$replace(VERSION_DEFINE, "[^0-9.]", "")
# Without this the makefile has no dependency on the header, so qmake would not re-run when the version changes
QMAKE_INTERNAL_INCLUDED_FILES += $$PWD/app/src/version.h
