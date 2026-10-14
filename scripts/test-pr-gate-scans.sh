#!/usr/bin/env bash
# Exercise the PR guards in an isolated repository with synthetic fixtures.
set -euo pipefail

source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/scripts" "$fixture/app" "$fixture/resources" "$fixture/tests/data"
cp "$source_root/scripts/check-strings.sh" "$source_root/scripts/check-cal-files.sh" "$fixture/scripts/"
cp "$source_root/.gitignore" "$fixture/"
cd "$fixture"
git init -q
printf 'Synthetic measurement application.\n' > README.md
printf 'Synthetic calibration fixture.\n' > tests/data/synthetic-mic-cal.csv
git add README.md tests/data/synthetic-mic-cal.csv

expect_rejected() {
    local script=$1 diagnostic=$2 status=0
    bash "$script" > scan.log 2>&1 || status=$?
    if [[ $status -ne 1 ]] || ! grep -Fq "$diagnostic" scan.log; then
        cat scan.log >&2
        echo "Expected $script to reject fixture with: $diagnostic (status=$status)" >&2
        exit 1
    fi
}

bash scripts/check-strings.sh
bash scripts/check-cal-files.sh

# Check every public scan root, including case-insensitive word matching.
for path in app/planted.txt README.md resources/planted.txt; do
    printf 'CHURCH\n' >> "$path"
    expect_rejected scripts/check-strings.sh 'Forbidden location terms found'
    if [[ $path == README.md ]]; then
        printf 'Synthetic measurement application.\n' > "$path"
    else
        rm "$path"
    fi
done
bash scripts/check-strings.sh

# Force-add ignored files just as a PR could; an untracked file must not fail.
for path in foo-cal.csv synthetic.cal synthetic.frd; do
    printf 'Synthetic calibration fixture.\n' > "$path"
    bash scripts/check-cal-files.sh
    git add -f "$path"
    expect_rejected scripts/check-cal-files.sh "Blocked tracked calibration file: $path"
    git rm -q -f "$path"
done
bash scripts/check-cal-files.sh
echo 'PR gate scan regression checks passed.'
