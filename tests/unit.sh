#!/bin/sh
# Build and run the unit tests only (no end-to-end script).
cd "$(dirname "$0")/.." || exit 1
make build/test_lexer build/test_parser build/test_expand || exit 1
rc=0
for t in lexer parser expand; do
    ./build/test_$t 2>&1 | grep -v -E "^devshell: " || true
    ./build/test_$t >/dev/null 2>&1 || rc=1
done
exit $rc
