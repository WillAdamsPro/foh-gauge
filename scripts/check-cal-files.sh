#!/usr/bin/env bash
# Check the index, including files force-added despite .gitignore.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

failed=0
while IFS= read -r -d '' path; do
  case "$path" in
    tests/data/synthetic-*) continue ;;
    *.cal|*.frd|*-cal.csv)
      printf 'Blocked tracked calibration file: %s\n' "$path" >&2
      failed=1
      ;;
  esac
done < <(git ls-files -z)

exit "$failed"
