#!/usr/bin/env bash
set -euo pipefail

base_sha="${BASE_SHA:-}"
head_sha="${HEAD_SHA:-HEAD}"

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
        | grep -E '\.(c|cc|cpp|h)$' \
        | grep -Ev '^(googletest/|editors/zed/grammars/pseudocode/src/parser\.c$)' \
        || true
)

if [[ "${#files[@]}" -eq 0 ]]; then
    echo "No C/C++ files changed."
    exit 0
fi

clang-format --dry-run --Werror "${files[@]}"
