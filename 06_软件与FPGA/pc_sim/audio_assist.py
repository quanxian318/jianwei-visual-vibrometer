#!/usr/bin/env python3
"""Offline 10–100 Hz acoustic vibration aid (not a displacement meter).

Use: python audio_assist.py motor.wav --visual-hz 10.844
Dependencies: numpy. Input: PCM 16-bit, mono/stereo WAV, >= 8 kHz.
"""
import argparse
import json
import wave
from pathlib import Path

import numpy as np


def read_wav(path):
    with wave.open(str(path), 'rb') as w:
        channels, sr, width, count = w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes()
        if w.getcomptype() != 'NONE' or width != 2 or channels < 1:
            raise ValueError('Expected uncompressed 16-bit PCM WAV (mono or stereo)')
        if sr < 8000:
            raise ValueError('Sample rate must be >= 8 kHz')
        pcm = np.frombuffer(w.readframes(count), dtype='<i2')
    if pcm.size != count * channels:
        raise ValueError('Truncated WAV data')
    return sr, pcm.reshape((-1, channels)).astype(np.float64).mean(axis=1) / 32768.0


def spectral_peaks(signal, rate, low=10.0, high=100.0, min_snr_db=8.0, max_peaks=5):
    """Strongest separate peaks. prominence_db compares with median band-bin power.

    This is not an estimate of absolute acoustic SNR or statistical confidence.
    """
    n = len(signal)
    if n < 16 or rate <= 2 * high:
        raise ValueError('Insufficient samples/sample rate')
    x = np.asarray(signal, dtype=np.float64)
    x = x - x.mean()
    if np.sqrt(np.mean(x*x)) < 1.e-7:
        return []
    spectrum = np.abs(np.fft.rfft(x * np.hanning(n))) ** 2
    hz = np.fft.rfftfreq(n, 1.0 / rate)
    mask = (hz >= low) & (hz <= high)
    idxs = np.flatnonzero(mask)
    if len(idxs) < 5:
        raise ValueError('Recording too short for the selected band')
    band = spectrum[idxs]
    median = float(np.median(band))
    if median <= 1.e-22:
        median = 1.e-22
    candidates = sorted(idxs, key=lambda i: spectrum[i], reverse=True)
    results = []
    spacing_hz = max(1.0, 2 * rate / n)
    for i in candidates:
        power = float(spectrum[i])
        prominence = 10.0 * np.log10(max(power, 1.e-30) / median)
        if prominence < min_snr_db:
            break
        if any(abs(hz[i] - r['hz']) < spacing_hz for r in results):
            continue
        delta = 0.0
        if 0 < i < len(spectrum)-1:
            # Parabolic interpolation on log-power around the DFT bin.
            a, b, c = np.log(np.maximum(spectrum[i-1:i+2], 1.e-30))
            divisor = a - 2*b + c
            if abs(divisor) > 1.e-12:
                delta = float(np.clip(0.5 * (a-c) / divisor, -0.5, 0.5))
        results.append({'hz': round(float((i+delta)*rate/n), 3),
                        'prominence_db': round(float(prominence), 1)})
        if len(results) >= max_peaks:
            break
    return results


def audio_envelope(samples, sr, carrier_low=200.0, carrier_high=3000.0,
                   envelope_rate=1000):
    """Band-limited analytic-signal amplitude; mean-pool before decimating.

    For offline finite clips, the FFT bandpass is non-causal and periodic at
    the edges; discard edge transients / validate on longer steady clips.
    """
    if sr % envelope_rate:
        raise ValueError('Input sample rate must be divisible by 1000 Hz')
    high = min(carrier_high, sr / 2 - 100.0)
    if high <= carrier_low:
        raise ValueError('Sample rate is too low for the acoustic carrier band')
    n = len(samples)
    spectrum = np.fft.fft(samples)
    hz = np.fft.fftfreq(n, 1.0 / sr)
    select = (hz >= carrier_low) & (hz <= high)
    analytic = np.fft.ifft(spectrum * (2.0 * select))
    envelope = np.abs(analytic)
    step = sr // envelope_rate
    envelope = envelope[:(len(envelope)//step)*step].reshape(-1,step).mean(axis=1)
    return envelope


def analyze(samples, sr, visual_hz=None, low=10.0, high=100.0):
    """Analyze direct low-frequency sound and high-frequency sound AM.

    The two peaks are different evidence, neither measures displacement.
    """
    if low < 1 or high > 100 or low >= high:
        raise ValueError('Band must be within 1–100 Hz (default 10–100)')
    if len(samples)/sr < 2.0:
        raise ValueError('At least 2 s of steady audio are required')
    rms = float(np.sqrt(np.mean(np.asarray(samples)**2)))
    silent = rms < 1.e-5
    direct = [] if silent else spectral_peaks(samples, sr, low, high)
    env = audio_envelope(np.asarray(samples), sr)
    modulation = float(np.std(env) / max(np.mean(env), 1.e-12))
    # Pure steady tones and nearly silent audio are not valid AM evidence.
    envelope_peaks = [] if silent or modulation < 0.01 else spectral_peaks(env, 1000, low, high)
    best = envelope_peaks[0]['hz'] if envelope_peaks else None
    agreement = None
    if visual_hz is not None and best is not None:
        tol = max(0.5, 0.02 * visual_hz)
        agreement = abs(best - visual_hz) <= tol
    return {
        'band_hz': [low, high], 'duration_s': round(len(samples)/sr, 3),
        'sample_rate_hz': sr, 'audio_rms_fs': round(rms, 6),
        'envelope_modulation_ratio': round(modulation, 4),
        'envelope_peaks': envelope_peaks, 'direct_acoustic_peaks': direct,
        'visual_frequency_hz': visual_hz, 'best_envelope_hz': best,
        'visual_envelope_agree': agreement,
        'note': 'Acoustic corroboration only; no mechanical amplitude or fault classification.'
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('wav', type=Path, help='16-bit PCM WAV (extract audio from MP4 with ffmpeg)')
    p.add_argument('--visual-hz', type=float)
    args = p.parse_args()
    sr, data = read_wav(args.wav)
    print(json.dumps(analyze(data, sr, args.visual_hz), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
