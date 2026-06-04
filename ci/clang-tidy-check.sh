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

mapfile -t files < <(
    printf '%s\n' "${changed_files}" \
        | grep -E '^(src|test)/.*\.cpp$' \
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
    -pthread
