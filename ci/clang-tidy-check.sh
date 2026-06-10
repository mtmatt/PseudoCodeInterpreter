#!/usr/bin/env bash
set -euo pipefail

base_sha="${BASE_SHA:-}"
head_sha="${HEAD_SHA:-HEAD}"

clang-tidy --verify-config >/dev/null

if [[ "$#" -gt 0 ]]; then
    changed_files="$(printf '%s\n' "$@")"
else
    if [[ -n "${base_sha}" ]] && git cat-file -e "${base_sha}^{commit}" 2>/dev/null; then
        changed_files="$(git diff --name-only --diff-filter=ACMRTUXB "${base_sha}" "${head_sha}")"
    else
        changed_files="$(git diff --name-only --diff-filter=ACMRTUXB HEAD)"
    fi
fi

# src/compiler.cpp and src/pseudoc.cpp need LLVM headers; skip them when no
# LLVM development install is available.
llvm_config="$(command -v llvm-config || echo /opt/homebrew/opt/llvm/bin/llvm-config)"
llvm_flags=()
llvm_filter='cat'
if "${llvm_config}" --includedir >/dev/null 2>&1; then
    llvm_flags=("-I$(${llvm_config} --includedir)")
else
    llvm_filter="grep -Ev '^src/(compiler|pseudoc)\\.cpp$'"
fi

mapfile -t files < <(
    printf '%s\n' "${changed_files}" \
        | grep -E '^(src|test)/.*\.cpp$' \
        | eval "${llvm_filter}" \
        || true
)

if [[ "${#files[@]}" -eq 0 ]]; then
    echo "No C++ translation units changed."
    exit 0
fi

clang-tidy --warnings-as-errors='*' "${files[@]}" -- \
    -std=c++17 \
    -Isrc \
    -Igoogletest/googletest/include \
    "${llvm_flags[@]}" \
    -pthread
