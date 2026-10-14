#!/bin/sh

# Packages the already-built release binary into a distributable AppImage; building it is the caller's job.
# Run from the repository root: ./scripts/create_appimage.sh <Qt install dir>
# Downloads the continuous builds of linuxdeploy and its Qt plugin into build/ unless already there.

set -eu

MYSELF="$(basename "$0")"

if [ $# -ne 1 ]; then
	echo "Usage: ./scripts/${MYSELF} <Qt install dir>" >&2
	exit 1
fi

QT_DIR="$1"

EXECUTABLE="bin/release/gg"
TOOLS="build/appimage-tools"
APPDIR="build/AppDir"

if [ ! -x "${EXECUTABLE}" ]; then
	echo "${MYSELF}: ${EXECUTABLE} not found - build the release configuration first" >&2
	exit 1
fi

# The tools are named by `uname -m`; the release names the arm64 AppImage arm64, not aarch64
MACHINE="$(uname -m)"
case "${MACHINE}" in
	x86_64) ARCH_NAME="x86_64" ;;
	aarch64) ARCH_NAME="arm64" ;;
	*) echo "${MYSELF}: unsupported architecture ${MACHINE}" >&2; exit 1 ;;
esac
APPIMAGE="GoodGit-${ARCH_NAME}.AppImage"

mkdir -p "${TOOLS}"
for TOOL in linuxdeploy linuxdeploy-plugin-qt; do
	TOOL_FILE="${TOOLS}/${TOOL}-${MACHINE}.AppImage"
	if [ ! -x "${TOOL_FILE}" ]; then
		echo "${MYSELF}: downloading ${TOOL}"
		curl -fsSL -o "${TOOL_FILE}" "https://github.com/linuxdeploy/${TOOL}/releases/download/continuous/${TOOL}-${MACHINE}.AppImage"
		chmod +x "${TOOL_FILE}"
	fi
done

# The tools run without FUSE, which containers and CI runners lack
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="${QT_DIR}/bin/qmake"
export LD_LIBRARY_PATH="${QT_DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
# xcb is always deployed; Wayland also brings its shell integration and decoration plugins
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"

LINUXDEPLOY="${TOOLS}/linuxdeploy-${MACHINE}.AppImage"

echo "${MYSELF}: deploying into ${APPDIR}"
rm -rf "${APPDIR}"
"${LINUXDEPLOY}" --appdir "${APPDIR}" --executable "${EXECUTABLE}" \
	--desktop-file app/res/goodgit.desktop --icon-file app/res/goodgit.svg --plugin qt

# The app never uses QNetworkInformation
rm -rf "${APPDIR}/usr/plugins/networkinformation"

echo "${MYSELF}: creating ${APPIMAGE}"
LDAI_OUTPUT="${APPIMAGE}" "${LINUXDEPLOY}" --appdir "${APPDIR}" --output appimage

rm -rf "${APPDIR}"

echo "${MYSELF}: ready for distribution: ${APPIMAGE}"
