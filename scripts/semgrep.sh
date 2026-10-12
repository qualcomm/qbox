#!/usr/bin/env bash

# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause-Clear

set -euo pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

cd "$SCRIPT_DIR/.."

docker run --rm \
  -v "$PWD:/src" \
  -w /src \
  --entrypoint /bin/sh \
  semgrep/semgrep:1.125.0 \
  -c '
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch check-suppressed https://github.com/rmalmain/qcom-reusable-workflows /tmp/reusable-workflows &&
    semgrep scan --sarif --output /tmp/semgrep.sarif --config auto --config /tmp/reusable-workflows/semgrep_rules/rules.yaml &&
    python3 /tmp/reusable-workflows/script/check_sarif_error.py /tmp/semgrep.sarif
  '

