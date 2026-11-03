#!/bin/sh

# Builds GoodGit.pro, the app and the libraries it links, into bin/<config>. Exit code 1 when the build fails, 2 when the environment is incomplete.
# The Qt kit comes from build_env.sh. An argument of "debug" builds the debug configuration.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG=release
[ "${1:-}" = debug ] && CONFIG=debug

. "${SCRIPT_DIR}/build_env.sh"

cd "${SCRIPT_DIR}/.." || exit 2
"${QMAKE}" -r GoodGit.pro CONFIG+="${CONFIG}" || exit 1
make -j"$(getconf _NPROCESSORS_ONLN)" || exit 1
