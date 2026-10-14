#!/usr/bin/env bash
# Keep public app strings and documentation free of location references.
set -euo pipefail
export LC_ALL=C

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Case-insensitive, whole-word matches; keep the wordlist here for review.
terms=(
    venue venues
    church churches
    chapel chapels
    cathedral cathedrals
    parish parishes
    congregation congregations
    sanctuary sanctuaries
    worship
    ministry ministries
)

paths=(app)
for path in README* resources Resources; do
    if [[ -e "$path" ]]; then
        paths+=("$path")
    fi
done

# app/ includes app/Resources. Skip binary assets; report file and line for text.
if grep -rinIwF -f <(printf '%s\n' "${terms[@]}") "${paths[@]}"; then
    echo "Forbidden location terms found in public app strings or documentation." >&2
    exit 1
else
    status=$?
    if [[ $status -ne 1 ]]; then
        echo "String scan failed (grep exit status $status)." >&2
        exit "$status"
    fi
fi

echo "Public string scan passed."
