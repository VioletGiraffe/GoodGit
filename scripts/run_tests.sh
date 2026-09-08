#!/bin/sh

# Builds tests/tests.pro and runs it. Exit code: the test run's, or 2 when the environment is incomplete.
# The Qt kit is resolved in order: QT_ROOT_DIR as already set (what jurplel/install-qt-action exports on CI);
# local-env.sh beside this script, git-ignored, where a developer sets it for their machine; the default
# installation location ~/Qt/<version>/{macos,gcc_64}; a qmake6 or qmake already on PATH (a distribution's Qt).
# An argument of "debug" builds and runs the debug configuration.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG=release
[ "${1:-}" = debug ] && CONFIG=debug

[ -z "${QT_ROOT_DIR:-}" ] && [ -f "${SCRIPT_DIR}/local-env.sh" ] && . "${SCRIPT_DIR}/local-env.sh"
if [ -z "${QT_ROOT_DIR:-}" ]; then
	for kit in "${HOME}"/Qt/6.*/macos "${HOME}"/Qt/6.*/gcc_64; do
		[ -x "${kit}/bin/qmake" ] && QT_ROOT_DIR="${kit}"
	done
fi

if [ -n "${QT_ROOT_DIR:-}" ]; then
	QMAKE="${QT_ROOT_DIR}/bin/qmake"
	if [ ! -x "${QMAKE}" ]; then
		echo "No qmake under \"${QT_ROOT_DIR}/bin\"." >&2
		exit 2
	fi
elif command -v qmake6 >/dev/null 2>&1; then
	QMAKE=qmake6
elif command -v qmake >/dev/null 2>&1; then
	QMAKE=qmake
else
	echo "QT_ROOT_DIR is not set, no kit was found under ~/Qt and no qmake is on PATH. Set QT_ROOT_DIR to the Qt kit directory, the one holding bin/qmake." >&2
	exit 2
fi

cd "${SCRIPT_DIR}/../tests" || exit 2
"${QMAKE}" tests.pro CONFIG+="${CONFIG}" || exit 1
make -j"$(getconf _NPROCESSORS_ONLN)" || exit 1
exec "bin/${CONFIG}/tests"
