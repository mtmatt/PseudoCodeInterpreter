#!/usr/bin/env bash
# Differential tests: compiled binaries must produce the same stdout as the
# interpreter. Run from the repository root after `make` and `make compiler`.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PSEUDO="$ROOT/pseudo"
PSEUDOC="$ROOT/pseudoc"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

if [[ ! -x "$PSEUDO" ]]; then
    echo "missing $PSEUDO (run make first)" >&2
    exit 1
fi
if [[ ! -x "$PSEUDOC" ]]; then
    echo "missing $PSEUDOC (run make compiler first)" >&2
    exit 1
fi

FILES=(
    "$ROOT"/test/compiler/*.ps
    "$ROOT/test/test_fib.ps"
    "$ROOT/test/test_repeat.ps"
    "$ROOT/test/test_array_methods.ps"
    "$ROOT/test/test_string_index.ps"
    "$ROOT/test/test_struct.ps"
)

failures=0
for src in "${FILES[@]}"; do
    name="$(basename "$src" .ps)"
    expected="$WORKDIR/$name.expected"
    actual="$WORKDIR/$name.actual"
    bin="$WORKDIR/$name.bin"

    "$PSEUDO" "$src" > "$expected" 2>&1
    if ! "$PSEUDOC" "$src" -o "$bin" > "$WORKDIR/$name.compile" 2>&1; then
        echo "FAIL $name: compilation failed"
        sed 's/^/    /' "$WORKDIR/$name.compile"
        failures=$((failures + 1))
        continue
    fi
    "$bin" > "$actual" 2>&1
    if ! diff -u "$expected" "$actual" > "$WORKDIR/$name.diff"; then
        echo "FAIL $name: output mismatch"
        sed 's/^/    /' "$WORKDIR/$name.diff"
        failures=$((failures + 1))
    else
        echo "PASS $name"
    fi
done

if [[ $failures -ne 0 ]]; then
    echo "$failures test(s) failed"
    exit 1
fi
echo "all compiler tests passed"
