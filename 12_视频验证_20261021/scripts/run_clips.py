"""Run under WSL. Uses existing ffmpeg, ffprobe and built C++ reference tools."""
import json
import math
import pathlib
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = pathlib.Path('/mnt/d/codex/2026-10-13/pcb-12v-5a-5-5-2/work/code_review_20261021/host_build')
JOBS = [
    ('a3332b91761880775aa172de0af3bad7', 'video1', ['tip_lower:818:488:64', 'tip_upper:828:178:64',
                                                 'flat:518:328:64', 'support:136:513:64']),
    ('cfb2f104a3598cc4c02675bb049f8b5b', 'video2', ['tip_upper:825:130:64', 'tip_lower:870:300:64',
                                                 'flat:560:260:64', 'support:360:518:64']),
]

for stem, name, rois in JOBS:
    source = ROOT / '原始视频' / (stem + '.mp4')
    out = ROOT / name
    out.mkdir(exist_ok=True)
    meta = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
        'stream=width,height,avg_frame_rate:frame=best_effort_timestamp_time', '-of', 'json', str(source)]))
    stream = meta['streams'][0]
    width, height = stream['width'], stream['height']
    times = [float(f['best_effort_timestamp_time']) for f in meta['frames']]
    fps = (len(times) - 1) / (times[-1] - times[0])
    pts = out / 'pts_seconds.txt'
    pts.write_text(''.join(f'{t:.9f}\n' for t in times))
    levels = 0
    while (width + (1 << levels) - 1) // (1 << levels) > 200:
        levels += 1
    low, high, gain = .5, 10., 20
    coef_h = round(65536 * (1 - math.exp(-2 * math.pi * high / fps)))
    coef_l = round(65536 * (1 - math.exp(-2 * math.pi * low / fps)))
    vis = out / 'decoded.vis'
    with vis.open('wb') as f:
        f.write(struct.pack('<4sHHHBBHHH', b'VIS1', width, height, len(times), levels, 0, coef_h, coef_l, gain))
        f.flush()  # child encoder writes through the same file descriptor
        subprocess.run(['ffmpeg', '-v', 'error', '-i', str(source), '-map', '0:v:0', '-vsync', '0',
                        '-pix_fmt', 'gray', '-f', 'rawvideo', '-'], stdout=f, check=True)
    expected = 18 + width * height * len(times)
    if vis.stat().st_size != expected:
        raise RuntimeError(f'frame/PTS mismatch: {name}')
    with vis.open('rb') as f:
        subprocess.run([str(BUILD / 'measure_clip'), str(pts), str(out), '8', *rois], stdin=f, check=True)
    subprocess.run([str(BUILD / 'vision_ref'), str(vis), str(out / 'vision.vout')], check=True)
    # Keep a short exact-input excerpt for independent model verification.
    with vis.open('rb') as f, (out / 'model_excerpt.vis').open('wb') as g:
        h = bytearray(f.read(18));struct.pack_into('<H', h, 8, 4);g.write(h)
        g.write(f.read(4 * width * height))
    info = dict(source=source.name, width=width, height=height, frames=len(times), fps=fps,
                duration_s=times[-1]-times[0], levels=levels, band_hz=[low, high], gain=gain,
                timestamps='container presentation timestamps; exposure synchronization unverified',
                units='native pixels; no physical calibration', backend='production C++ tracker/analyzeCapture/vision::core')
    (out / 'run.json').write_text(json.dumps(info, ensure_ascii=False, indent=2))
    # This decoded intermediate is reconstructible; retain original clips and results.
    assert vis.resolve().is_relative_to(ROOT.resolve())
    vis.unlink()
    print(name, json.dumps(info), flush=True)
