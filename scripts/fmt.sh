#!/usr/bin/env bash

# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause-Clear

set -euo pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

EXTENSIONS=(
    '*.c'
    '*.cc'
    '*.h'
)

cd "$SCRIPT_DIR/.."

(
    git diff -z --diff-filter=AM --name-only main... -- "${EXTENSIONS[@]}"  # files changed between main and HEAD
    git ls-files -z --modified -- "${EXTENSIONS[@]}"                        # tracked files with some differences with the working tree
    git diff -z --cached --diff-filter=A --name-only -- "${EXTENSIONS[@]}"  # files staged and added
) |
    sort -zu |
    xargs -0 -r clang-format -i -style=file
