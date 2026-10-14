# Synthetic golden signals

The generator salvaged from `juno/golden-signals` creates 69 float32 WAVs at
44.1, 48 and 96 kHz in `<build>/tests/golden`, plus a manifest of SHA-256 hashes,
headers, channel-0 levels and expected delays. Stereo pairs use channel 0 as
reference and channel 1 as mic; positive delay means mic later.

Install the build-time dependency with `python3 -m pip install -r tools/requirements-golden.txt`
(or in a virtual environment), then use the normal CMake build and ctest commands.
CMake requires Python with NumPy when tests are enabled; select a virtual environment
with `-DPython3_EXECUTABLE=/path/to/venv/bin/python3` if needed.
`ctest --test-dir build -R golden_tests --output-on-failure` runs just this harness.

The build regenerates missing outputs or outputs older than the generator.
The harness checks all 69 headers/hashes and levels, silence, short-window rejection,
integer/fractional/negative delays, noise, reverb, polarity, reflection levels,
two similar arrivals, long delays and LF sub pairs against the core API.
Hashes check artifact integrity against the generated manifest, not a cross-platform
bitwise baseline. Fixed PCG64 seeds make generation repeatable within the same
Python/NumPy environment. Never commit generated WAVs or manifests.
