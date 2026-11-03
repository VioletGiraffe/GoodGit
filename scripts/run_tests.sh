#!/bin/sh

# Runs the tests build_tests.sh built. Exit code: the test run's, or 2 when the build is missing.
# An argument of "debug" runs the debug configuration.
# Any further arguments go to the test executable, e.g. a Catch2 test spec.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG=release
if [ "${1:-}" = debug ]; then
	CONFIG=debug
	shift
fi

TESTS="${SCRIPT_DIR}/../tests/bin/${CONFIG}/tests"
if [ ! -x "${TESTS}" ]; then
	echo "The ${CONFIG} tests are not built: run build_tests.sh first." >&2
	exit 2
fi
# The backend tests run these
git --version
hg --version -q
# --warn NoTests: a filter that matches nothing exits 0 otherwise, so a broken one would pass silently
exec "${TESTS}" "$@" --warn NoTests
