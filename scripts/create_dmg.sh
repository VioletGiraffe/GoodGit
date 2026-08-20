#!/bin/sh

# Packages the already-built release bundle into a distributable DMG; building it is the caller's job.
# Run from the repository root: ./scripts/create_dmg.sh <Qt install dir>

set -eu

MYSELF="$(basename "$0")"

if [ $# -ne 1 ]; then
	echo "Usage: ./scripts/${MYSELF} <Qt install dir>" >&2
	exit 1
fi

QT_DIR="$1"

APP="GoodGit"
BUNDLE="bin/release/${APP}.app"
DMG="${APP}.dmg"
STAGE="build/dmg-staging"

if [ ! -d "${BUNDLE}" ]; then
	echo "${MYSELF}: ${BUNDLE} not found - build the release configuration first" >&2
	exit 1
fi

echo "${MYSELF}: deploying Qt frameworks into ${BUNDLE}"
"${QT_DIR}/bin/macdeployqt" "${BUNDLE}"

echo "${MYSELF}: creating ${DMG}"

rm -rf "${STAGE}"
mkdir -p "${STAGE}"
cp -R "${BUNDLE}" "${STAGE}/"
ln -s /Applications "${STAGE}/"

# -srcfolder populates via a private nobrowse mount - no volume appears under /Volumes for Spotlight to grab and pin.
hdiutil create "${DMG}" -ov -volname "${APP}" -fs "HFS+" -format UDZO -srcfolder "${STAGE}"

rm -rf "${STAGE}"

echo "${MYSELF}: ready for distribution: ${DMG}"
