# License inventory

Run `python3 scripts/license_guard.py` and
`python3 -m unittest discover -s scripts -p 'test_license_guard.py'`.
Both run in the license-guard CI job on every PR, without fetching dependencies.

`third_party.json` records each component's name, pinned version, actual license,
source, and whether it ships in the app. CMake dependency entries additionally
specify `dependency`; Python entries specify `python_package`. Bundled JUCE
libraries use `bundled_by` and the pinned JUCE source location. These are the
compiled macOS subset, not approval for every SDK in the JUCE download.
JUCE is used under its commercial EULA; this inventory does not verify seats or
entitlements. A JUCE version/module/configuration change needs a notice review. New linked JUCE
modules are rejected until added to its `modules` list after that review.

Third-party licenses are restricted to MIT, BSD-2-Clause, BSD-3-Clause, BSL-1.0,
Apache-2.0, Zlib, OFL-1.1, Unlicense and JUCE-commercial. No compound expression,
GPL, AGPL, LGPL, ISC or unknown identifier is implicitly approved. HarfBuzz's
Old MIT variant is recorded in the MIT family; its exact upstream terms are
reproduced in THIRD_PARTY_NOTICES.md.

Original assets are explicitly marked `ownership: "Will Adams"` and
`license: "LicenseRef-FOH-proprietary"`, recorded in THIRD_PARTY_NOTICES.md (the repo has no top-level LICENSE file). This is
an ownership exception for original work, not another allowed third-party
license. It cannot approve fetched dependencies or vendored libraries. Do not
mark third-party work as original, or grant an open-source license to original
assets to satisfy the guard.

`files` lists exact asset/source paths (no checksums). Add a reviewed entry for
each new font, image, audio file, or resource; recheck provenance when replacing
one. Original chime and test-signal generators are inventoried as source because
Every non-original component must also appear in THIRD_PARTY_NOTICES.md, and a
top-level LICENSE/COPYING file fails the guard (the repo intentionally has none).
there is no tracked audio file. `vendored` lists exact directory roots for any
future reviewed vendor/submodule; arbitrary new code directories and vendor or
license markers also fail until reviewed. First-party directories are a
fixed list in the guard; new directories must be reviewed even if they contain
only data with an unfamiliar extension; expanding that list requires review. New CMake
FetchContent/ExternalProject declarations require literal source and pins.
Variables in those fields fail closed rather than being evaluated as PR code.

The guard scans Git-tracked and nonignored new files, including all CMake files,
not ignored build/download trees. It checks inventory coverage and policy; it
cannot identify secretly copied code inside an existing first-party source file
or establish copyright ownership. Review provenance alongside every entry.
Preserve full upstream notices and bundle updates when adding shipped components.
