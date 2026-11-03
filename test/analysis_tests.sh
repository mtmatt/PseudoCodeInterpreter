#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
expect_failure() {
    local status=0
    ./pseudo "$@" >"$work_dir/out" 2>"$work_dir/err" || status=$?
    test "$status" -eq 1 || fail "expected exit 1, received $status: $*"
}

./pseudo --help >"$work_dir/out"
rg -q -- '--profile' "$work_dir/out"
expect_failure --analyze "$work_dir/missing.ps"
rg -q 'Cannot open file' "$work_dir/err"
expect_failure --profile example/complexity.ps --sizes 1,
expect_failure --profile example/complexity.ps --sizes -1
expect_failure --profile example/complexity.ps --sizes 9223372036854775808
expect_failure --profile example/complexity.ps --sizes 1 --input 'n;print(1)'
expect_failure --analyze example/complexity.ps --sizes 1
expect_failure --profile example/complexity.ps --max-steps 0

cat >"$work_dir/linear.ps" <<'PS'
Algorithm Linear(n):
    total <- 0
    for i <- 1 to n do
        total <- total + i
    return total
print(Linear(n))
PS
./pseudo --analyze "$work_dir/linear.ps" >"$work_dir/out" 2>"$work_dir/err"
test ! -s "$work_dir/out" || fail 'static analysis executed print'
rg -q 'Linear.*O\(n\)' "$work_dir/err"
./pseudo --profile "$work_dir/linear.ps" --sizes 10,20 >"$work_dir/out" 2>"$work_dir/err"
rg -q '^55$' "$work_dir/out"
rg -q '^210$' "$work_dir/out"
awk -F '\t' '
    $1 == "n=10" { small = $2 }
    $1 == "n=20" { large = $2 }
    END { exit !(small > 0 && large > small && large < 2.2 * small) }
' "$work_dir/err" || fail 'linear profile failed to grow as expected'

printf 'print(count)\n' >"$work_dir/input.ps"
./pseudo --profile "$work_dir/input.ps" --sizes 0,7 --input count >"$work_dir/out" 2>"$work_dir/err"
rg -q '^0$' "$work_dir/out"
rg -q '^7$' "$work_dir/out"

printf 'while true do\n    x <- 1\n' >"$work_dir/infinite.ps"
expect_failure --profile "$work_dir/infinite.ps" --max-steps 20
rg -q 'Profile stopped:' "$work_dir/err"
printf 'print(missing)\n' >"$work_dir/error.ps"
expect_failure --profile "$work_dir/error.ps"
rg -q 'Profile stopped:' "$work_dir/err"
for error_source in 'missing()' 'a <- {missing}' 'a <- {1}; a[2] <- 4'; do
    printf '%s\nprint(42)\n' "$error_source" >"$work_dir/error.ps"
    expect_failure --profile "$work_dir/error.ps"
    rg -q 'Profile stopped:' "$work_dir/err"
    test ! -s "$work_dir/out" || fail 'execution continued after a nested error'
done
for destructor_source in 'missing' 'int("99999999999999999999999999999")'; do
    cat >"$work_dir/destructor.ps" <<PS
Struct Cleanup:
    value
    Algorithm Cleanup constructor(): self.value <- 0
    Algorithm destructor(): $destructor_source
Algorithm work():
    local <- Cleanup()
    return 0
work()
print(99)
PS
    expect_failure --profile "$work_dir/destructor.ps"
    rg -q 'Profile stopped:' "$work_dir/err"
    test ! -s "$work_dir/out" || fail 'execution continued after a destructor error'
done
printf 'if true do 1\n' >"$work_dir/invalid.ps"
expect_failure --analyze "$work_dir/invalid.ps"
rg -q 'Parse error:' "$work_dir/err"

printf 'Algorithm Twice(n): return n * 2\n' >"$work_dir/module.ps"
printf 'import "module.ps"\nprint(Twice(n))\n' >"$work_dir/import.ps"
./pseudo --analyze --profile "$work_dir/import.ps" --sizes 3 >"$work_dir/out" 2>"$work_dir/err"
rg -q '^6$' "$work_dir/out"
rg -q 'Twice.*O\(1\)' "$work_dir/err"

: >"$work_dir/empty.ps"
./pseudo --analyze --profile "$work_dir/empty.ps" >"$work_dir/out" 2>"$work_dir/err"
test ! -s "$work_dir/out" || fail 'empty program produced output'
./pseudo --analyze --profile example/complexity.ps --sizes 4,8,12 >"$work_dir/out" 2>"$work_dir/err"
rg -q '^n=12' "$work_dir/err"
echo 'all analysis CLI tests passed'
