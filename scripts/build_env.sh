# Sourced by build_tests.sh and build_app.sh to prepare a qmake build: sets QMAKE, or exits the caller with 2.
# Requires SCRIPT_DIR, the directory holding this script: a sourced script cannot find its own path portably.
# The Qt kit is resolved in order: QT_ROOT_DIR as already set (what jurplel/install-qt-action exports on CI);
# local-env.sh beside this script, git-ignored, where a developer sets it for their machine; a qmake6 or qmake
# already on PATH (a distribution's Qt).

[ -z "${QT_ROOT_DIR:-}" ] && [ -f "${SCRIPT_DIR}/local-env.sh" ] && . "${SCRIPT_DIR}/local-env.sh"

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
	echo "QT_ROOT_DIR is not set and no qmake is on PATH. Set QT_ROOT_DIR to the Qt kit directory, the one holding bin/qmake, or set it in a git-ignored local-env.sh beside this script." >&2
	exit 2
fi

# A kept .qmake.stash pins the toolchain probed when it was written, so every build probes the current one instead.
# One is written per subproject build directory, and qmake also searches upward, so the sweep covers the repository.
find "${SCRIPT_DIR}/.." -name .qmake.stash -delete
