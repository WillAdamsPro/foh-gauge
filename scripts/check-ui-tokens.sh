#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Both scanners return 1 for a clean scan, 2 for an operational failure.
check() {
    local status=0
    if command -v rg >/dev/null 2>&1; then
        rg -n "$1" app/Source --glob '!FohTheme*.h' || status=$?
    else
        # Hosted runners do not bundle ripgrep. Keep the same ERE scan with grep.
        grep -REn --exclude='FohTheme*.h' "$1" app/Source || status=$?
    fi
    if [[ $status == 0 ]]; then
        echo "UI token check failed: $2" >&2
        return 1
    fi
    [[ $status == 1 ]]
}
check 'Colour ?\(0x|Colours::|Colour::fromHSV' 'use theme colour tokens'
check '(font|numeric)\([0-9]|FontOptions\([0-9]|T::[[:alnum:]_]+ ?[+-]' 'use named type roles without arithmetic'
