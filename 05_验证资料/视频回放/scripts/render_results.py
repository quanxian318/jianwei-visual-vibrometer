"""Windows bundled Python: render actual production C++ outputs. No model substitution."""
import json
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
PC = ROOT.parent / '06_软件与FPGA' / 'pc_sim'
sys.path.insert(0, str(PC))
import vision_model as vm

FONT = r'C:/Windows/Fonts/Noto Sans SC Medium (TrueType).otf'
font = ImageFont.truetype(FONT, 18)
small = ImageFont.truetype(FONT, 15)
COLORS = ['#e34a33', '#ff9f1c', '#2a9d8f', '#4361ee']

def wp(p):
    p = pathlib.Path(p).resolve()
    return '/mnt/' + p.drive[0].lower() + p.as_posix().split(':', 1)[1]

def heat(a):
    idx = vm.energy_display(a)
    stops = np.array([[0, 0, 5], [45, 12, 80], [123, 31, 107], [200, 65, 60], [249, 142, 8], [250, 248, 145]])
    x = np.linspace(0, 255, len(stops))
    return np.stack([np.interp(idx, x, stops[:, k]) for k in range(3)], axis=-1).astype('uint8')

def plot_panels(panels, title, dest):
    # Keep plots self-contained and retain the underlying CSVs; no extra package.
    image = Image.new('RGB', (1320, 110 + 250 * len(panels)), 'white')
    d = ImageDraw.Draw(image);d.text((28, 12), title, font=font, fill='black')
    for k, (label, traces) in enumerate(panels):
        x0, x1, y0, y1 = 90, 1290, 78 + k*250, 245 + k*250
        xs = np.concatenate([np.asarray(t[1]) for t in traces])
        vals = np.concatenate([np.asarray(t[2])[np.isfinite(t[2])] for t in traces])
        xmin, xmax = float(xs.min()), float(xs.max())
        ymin, ymax = (float(vals.min()), float(vals.max())) if vals.size else (0., 1.)
        pad = max(.05, (ymax-ymin)*.08);ymin-=pad;ymax+=pad
        def xy(x,y):return (x0+(x-xmin)/max(1e-9,xmax-xmin)*(x1-x0), y1-(y-ymin)/(ymax-ymin)*(y1-y0))
        for x in np.linspace(xmin,xmax,9):
            px,_=xy(x,ymin);d.line((px,y0,px,y1),fill='#dddddd');d.text((px-15,y1+3),f'{x:.1f}',font=small,fill='black')
        for y in np.linspace(ymin,ymax,5):
            _,py=xy(xmin,y);d.line((x0,py,x1,py),fill='#dddddd');d.text((3,py-8),f'{y:.2f}',font=small,fill='black')
        d.rectangle((x0,y0,x1,y1),outline='black')
        d.text((x0,y0-24),label,font=small,fill='black')
        for j,(name,x,y,color) in enumerate(traces):
            points=[]
            for xx,yy in zip(x,y):
                if np.isfinite(yy):points.append(xy(xx,yy))
                else:
                    if len(points)>1:d.line(points,fill=color,width=2)
                    points=[]
            if len(points)>1:d.line(points,fill=color,width=2)
            d.text((430+(j%3)*270,y0-24-(j//3)*18),name,font=small,fill=color)
    d.text((550,image.height-28),'X: seconds / frequency (Hz), as labeled above',font=small,fill='black')
    image.save(dest)

overall = {}
for name in ['video1', 'video2']:
    out = ROOT / name
    info = json.loads((out / 'run.json').read_text())
    summary = json.loads((out / 'tracking.json').read_text())
    rows = summary[::2]
    base = Image.open(ROOT / (pathlib.Path(info['source']).stem + '_first.png')).convert('RGB')
    draw = ImageDraw.Draw(base)
    for k, row in enumerate(rows):
        x, y, size = row['roi_x'], row['roi_y'], row['roi_size']
        draw.rectangle((x, y, x + size, y + size), outline=COLORS[k], width=3)
        draw.text((x - 6, max(0, y - 23)), row['name'], font=font, fill=COLORS[k], stroke_width=1, stroke_fill='black')
    base.save(out / 'roi.png')

    traces = [[], [], []]
    diag = {}
    for k, row in enumerate(rows):
        data = np.genfromtxt(out / (row['name'] + '_samples.csv'), delimiter=',', names=True)
        good = data['ok'].astype(bool) & (data['ncc'] >= .6) & ~data['edge'].astype(bool) & ~data['saturated'].astype(bool)
        for a, field in zip(traces[:2], ['dx_px', 'dy_px']):
            a.append((row['name'], data['t_s'], np.where(good, data[field], np.nan), COLORS[k]))
        traces[2].append((row['name'], data['t_s'], data['ncc'], COLORS[k]))
        diag[row['name']] = dict(initialized=row['initialized'], accepted_fraction=float(good.mean()),
                                 production_valid_x=row['valid'], production_valid_y=summary[2*k+1]['valid'])
    plot_panels(list(zip(['X displacement (px) vs time (s)', 'Y displacement (px) vs time (s)', 'NCC vs time (s); acceptance >= 0.6'], traces)),
                name + ': camera motion not compensated; gaps = rejected samples', out / 'tracking.png')

    spectra = [j for j in summary if j['valid']]
    if spectra:
        spec_traces = []
        for j in spectra:
            a = np.genfromtxt(out / (j['name'] + '_' + j['axis'] + '_spectrum.csv'), delimiter=',', names=True)
            spec_traces.append((j['name'] + '/' + j['axis'], a['frequency_hz'], a['amplitude_px'], COLORS[(len(spec_traces)//2)%4]))
        plot_panels([('Amplitude (px) vs image frequency (Hz)', spec_traces)],
                    'Production spectra: shared camera motion may dominate; NOT motor speed', out / 'spectrum.png')

    cpp = vm.read_vout(out / 'vision.vout')
    frames, levels, ch, cl, gain = vm.read_vis(out / 'model_excerpt.vis')
    st = None
    for i, f in enumerate(frames):
        model = vm.core(f, levels, ch, cl, gain, st);st = model.state
        for actual, expected in zip(cpp[i], [model.g0, model.mag, model.band, model.amp]):
            if not np.array_equal(actual, expected):raise RuntimeError(f'{name}: model mismatch frame {i}')
    diag['model_check'] = 'first 4 full-resolution frames: all four C++ outputs bit-exact with Python'
    raw = out / 'comparison.rgb'
    snap = min(len(cpp) - 1, int(info['fps'] * 4))
    with raw.open('wb') as target:
        for i, (g0, mag, band, amp) in enumerate(cpp):
            canvas = Image.new('RGB', (960, 240), '#101923')
            d = ImageDraw.Draw(canvas)
            for k, (label, data) in enumerate([('原始工作图 160×90', g0), ('运动放大 ×20（显示）', mag), ('带通亮度能量（非位移）', heat(amp))]):
                d.text((k*320+8, 8), label, font=font, fill='white')
                im = Image.fromarray(data).convert('RGB').resize((320, 180), Image.Resampling.BILINEAR)
                canvas.paste(im, (k*320, 36))
            d.text((10, 218), f'{name}  {i/info["fps"]:.2f}s | 0.5–10Hz | PS C++ 离线输出 | 含机身移动与光照变化', font=small, fill='#e6c680')
            target.write(canvas.tobytes())
            if i == snap:canvas.save(out / 'comparison.png')
    subprocess.run(['wsl.exe', '--', 'ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pixel_format', 'rgb24',
                    '-video_size', '960x240', '-framerate', str(info['fps']), '-i', wp(raw), '-c:v', 'libx264',
                    '-crf', '20', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', wp(out / 'comparison.mp4')], check=True)
    assert raw.resolve().is_relative_to(ROOT.resolve());raw.unlink()
    (out / 'diagnostics.json').write_text(json.dumps(diag, ensure_ascii=False, indent=2), encoding='utf-8')
    overall[name] = dict(run=info, tracking=summary, diagnostics=diag)
    print(name, 'rendered', flush=True)
(ROOT / 'results.json').write_text(json.dumps(overall, ensure_ascii=False, indent=2), encoding='utf-8')
