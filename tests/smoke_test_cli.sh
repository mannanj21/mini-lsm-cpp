#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI="$DIR/../build/mini_lsm_cli"
TEST_DB="/tmp/smoke_test_cli_db"

rm -rf "$TEST_DB"

OUTPUT=$("$CLI" <<EOF
open $TEST_DB
put key1 val1
put key2 val2
get key1
get key2
get key3
scan * *
flush
compact
quit
EOF
)

rm -rf "$TEST_DB"

echo "$OUTPUT"

if ! echo "$OUTPUT" | grep -q "val1"; then
    echo "FAILED: Expected 'val1' in output"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "val2"; then
    echo "FAILED: Expected 'val2' in output"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "(not found)"; then
    echo "FAILED: Expected '(not found)' in output"
    exit 1
fi

echo "Smoke test passed successfully!"
