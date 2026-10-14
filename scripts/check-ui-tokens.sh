#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# rg returns 1 for a clean scan, 2 for an operational failure.
check() {
    local status=0
    rg -n "$1" app/Source --glob '!FohTheme*.h' || status=$?
    if [[ $status == 0 ]]; then
        echo "UI token check failed: $2" >&2
        return 1
    fi
    [[ $status == 1 ]]
}
check 'Colour ?\(0x|Colours::|Colour::fromHSV' 'use theme colour tokens'
check '(font|numeric)\([0-9]|FontOptions\([0-9]|T::\w+ ?[+-]' 'use named type roles without arithmetic'
