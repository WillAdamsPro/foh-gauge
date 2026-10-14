#!/usr/bin/env python3
"""Deterministic golden test-signal generator for FOH Gauge (Part A, test-checklist-build1.md).

Writes 32-bit float WAVs plus manifest.tsv (SHA-256 + expected values) to the given build directory.
Usage: python3 tools/gen_golden.py outdir   (numpy; PCG64 seeds fixed below)
Generated WAVs and manifests are build artifacts, never source files.
Stereo pair files: ch0 = reference, ch1 = mic (mic = ref delayed; + = mic later).
"""
import hashlib, os, struct, sys
import numpy as np

if len(sys.argv) != 2:
    sys.exit("Usage: gen_golden.py outdir (use a build directory)")
OUT = sys.argv[1]
RATES = (44100, 48000, 96000)
rows = []

def rng(seed): return np.random.Generator(np.random.PCG64(seed))

def write_wav(name, chans, fs, kind, check, delay=float("nan"), d2=float("nan"), note=""):
    data = np.stack([np.asarray(c, dtype=np.float32) for c in chans], axis=1)
    n, nch = data.shape
    pcm = data.astype("<f4").tobytes()
    blob = (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE" + b"fmt " +
            struct.pack("<IHHIIHH", 16, 3, nch, fs, fs * nch * 4, nch * 4, 32) + b"data" + struct.pack("<I", len(pcm)) + pcm)
    with open(os.path.join(OUT, name), "wb") as f: f.write(blob)
    c0 = data[:, 0].astype(np.float64)
    rms = 20 * np.log10(np.sqrt(np.mean(c0 ** 2))) if n and np.any(c0) else -999.0
    peak = float(np.max(np.abs(c0))) if n else 0.0
    dc = float(np.mean(c0)) if n else 0.0
    rows.append((name, hashlib.sha256(blob).hexdigest(), fs, n, nch, kind, check, f"{rms:.4f}", f"{peak:.6f}", f"{dc:.6f}", f"{delay:.4f}", f"{d2:.4f}", note))

def white(n, seed, rms_db=-12.0):
    x = rng(seed).standard_normal(n); return x / np.sqrt(np.mean(x ** 2)) * 10 ** (rms_db / 20)

def pink(n, seed, rms_db=-12.0):
    m = 1 << int(np.ceil(np.log2(max(n, 2)))); X = np.fft.rfft(rng(seed).standard_normal(m))
    f = np.arange(len(X), dtype=float); f[0] = 1; X /= np.sqrt(f); X[0] = 0
    x = np.fft.irfft(X, m)[:n]; return x / np.sqrt(np.mean(x ** 2)) * 10 ** (rms_db / 20)

def bandpass(x, fs, lo, hi):
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / fs); fp = np.maximum(f, 1e-9)
    w = 1 / np.sqrt(1 + (f / hi) ** 8) * np.where(f > 0, 1 / np.sqrt(1 + (lo / fp) ** 8), 0)
    return np.fft.irfft(X * w, len(x))

def fdelay(x, d):
    """Band-limited delay by d samples (fractional ok): FFT phase shift on a zero-padded copy."""
    pad = int(np.ceil(abs(d))) + 64; n = len(x); y = np.concatenate([np.zeros(pad), x])
    m = 1 << int(np.ceil(np.log2(len(y) + pad))); Y = np.fft.rfft(y, m)
    Y *= np.exp(-2j * np.pi * np.fft.rfftfreq(m) * d); return np.fft.irfft(Y, m)[pad:pad + n]

def pair(name, fs, ref, d, seed, snr_db=None, verb=False, gain=1.0, refl=(), note=""):
    mic = gain * fdelay(ref, d)
    for (dd, gdb) in refl: mic = mic + gain * 10 ** (gdb / 20) * fdelay(ref, dd)
    if verb:
        t = np.arange(int(0.4 * fs)) / fs; h = rng(seed + 7).standard_normal(len(t)) * np.exp(-6.91 * t / 0.4) * 0.05; h[0] = 1.0
        mic = np.convolve(mic, h)[:len(mic)] * 0.5
    if snr_db is not None:
        mic = mic + white(len(mic), seed + 1, 20 * np.log10(np.sqrt(np.mean(mic ** 2))) - snr_db)
    write_wav(name, [ref, mic], fs, "pair", "delay", d, refl[0][0] if refl else float("nan"), note)

os.makedirs(OUT, exist_ok=True)
for fs in RATES:
    tag, N = f"{fs}", fs
    t = np.arange(N) / fs
    write_wav(f"pink_-12dBrms_s1_{tag}.wav", [pink(N, 1)], fs, "noise", "level", note="S9/G1 pink -12 dBFS RMS seed 1")
    write_wav(f"white_-12dBrms_s2_{tag}.wav", [white(N, 2)], fs, "noise", "level", note="white -12 dBFS RMS seed 2")
    write_wav(f"sine_1k_0dBFS_{tag}.wav", [np.sin(2 * np.pi * 1000 * t)], fs, "sine", "level", note="S10 1 kHz 0 dBFS peak")
    write_wav(f"sine_100_-20dBFS_{tag}.wav", [0.1 * np.sin(2 * np.pi * 100 * t)], fs, "sine", "level", note="100 Hz -20 dBFS peak")
    ts = np.arange(2 * fs) / fs; L = 2.0 / np.log(1000)
    write_wav(f"logsweep_20-20k_2s_-6dBFS_{tag}.wav", [0.5 * np.sin(2 * np.pi * 20 * L * (np.exp(ts / L) - 1))], fs, "sweep", "level", note="G2 log sweep 20-20k 2 s")
    imp = np.zeros(N); imp[100] = 1.0
    write_wav(f"impulse_at100_{tag}.wav", [imp], fs, "impulse", "level", note="unit impulse at sample 100")
    write_wav(f"silence_1s_{tag}.wav", [np.zeros(N)], fs, "silence", "silent", note="digital silence")
    ref = pink(N, 10); d48 = 4.80e-3 * fs
    pair(f"pair_int230_{tag}.wav", fs, ref, 230, 11, note="S5 integer 230 smp")
    pair(f"pair_frac4.80ms_{tag}.wav", fs, ref, d48, 12, note="S4/D-INS fractional 4.80 ms")
    pair(f"pair_frac4.80ms_snr10_{tag}.wav", fs, ref, d48, 13, snr_db=10, note="4.80 ms + white noise SNR 10 dB")
    pair(f"pair_frac4.80ms_snr-10_{tag}.wav", fs, ref, d48, 14, snr_db=-10, note="S8/D-CF SNR -10 dB")
    pair(f"pair_frac4.80ms_verb_{tag}.wav", fs, ref, d48, 15, verb=True, note="4.80 ms + RT60 0.4 s reverb tail")
    pair(f"pair_inv230_{tag}.wav", fs, ref, 230, 16, gain=-1.0, note="S6 polarity inverted")
    pair(f"pair_refl2ms_-3dB_+8ms_{tag}.wav", fs, ref, 2e-3 * fs, 17, refl=((10e-3 * fs, -3.0),), note="S16 direct 2 ms, refl -3 dB at 10 ms")
    pair(f"pair_twospike3ms_4.5ms_-0.5dB_{tag}.wav", fs, ref, 3e-3 * fs, 18, refl=((4.5e-3 * fs, -0.5),), note="D-2Sa two-spike")
    write_wav(f"pair_swapped4.80ms_{tag}.wav", [fdelay(ref, d48), ref], fs, "pair", "delay_neg", -d48, note="D-SGN ref later than mic: expect -4.80 ms")
    pair(f"pair_long200ms_{tag}.wav", fs, pink(2 * fs, 20), 0.2 * fs, 21, note="S13 long 200 ms, 2 s window")
    sub = bandpass(pink(3 * fs, 30), fs, 30, 120); sub *= 10 ** (-12 / 20) / np.sqrt(np.mean(sub ** 2))
    pair(f"pair_sub_bp30-120_12.5ms_{tag}.wav", fs, sub, 12.5e-3 * fs, 31, note="S15/D-SUB 3 s window")
    write_wav(f"clip_sine1k_+6dB_{tag}.wav", [np.clip(2.0 * np.sin(2 * np.pi * 1000 * t), -1, 1)], fs, "clip", "level", note="1 kHz +6 dB hard-clipped at 1.0")
    write_wav(f"dc_0.5_{tag}.wav", [np.full(N, 0.5)], fs, "dc", "level", note="constant DC 0.5")
    write_wav(f"dc_offset_pink_{tag}.wav", [pink(N, 40) + 0.25], fs, "dc", "level", note="pink -12 dBFS + DC 0.25")
for n in (0, 1, 63, 64, 65, 4096):
    r = pink(max(n, 1), 50)[:n]
    write_wav(f"edge_len{n}_48000.wav", [r, r], 48000, "edge", "edge", 0.0 if n >= 64 else float("nan"), note=f"edge length {n}, identical pair")
with open(os.path.join(OUT, "manifest.tsv"), "w") as f:
    f.write("# FOH Gauge golden signals, float32 WAV. Levels on ch0. delay/delay2 in samples (+ = mic later). check: level|delay|delay_neg|silent|edge\n")
    f.write("file\tsha256\tfs\tframes\tchannels\tkind\tcheck\trms_dbfs\tpeak\tdc\tdelay_smp\tdelay2_smp\tnote\n")
    for r in rows: f.write("\t".join(str(v) for v in r) + "\n")
print(len(rows), "files ->", os.path.abspath(OUT))
