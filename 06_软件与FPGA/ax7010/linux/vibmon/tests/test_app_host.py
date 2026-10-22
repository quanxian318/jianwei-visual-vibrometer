#!/usr/bin/env python3
"""Host regressions using synthetic frames and a memory-backed register fixture."""
import json
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import time

binary = pathlib.Path(sys.argv[1]).resolve()
failures = []

def check(ok, message):
    print(('PASS ' if ok else 'FAIL ') + message, flush=True)
    if not ok:
        failures.append(message)

class Daemon:
    def __init__(self, directory, calibrated=True, carrier=False, extra=()):
        self.dir = pathlib.Path(directory)
        self.sock = self.dir / 'ctl.sock'
        cfg = {
            'camera': {'source': 'synthetic', 'synthetic': {'fps': 60}},
            'measurement': {'roi_width_px': 48, 'roi_height_px': 48, 'search_radius_px': 6,
                            'duration_s': 1.2, 'calibration_file': str(self.dir / 'cal.json')},
            'storage': {'records_dir': str(self.dir / 'records'), 'settings_file': str(self.dir / 'settings.json')},
            'ui': {'enabled': False}, 'voice': {'enabled': False},
            'control_socket': str(self.sock), 'poweroff_on_shutdown': False,
            'imu': {'enabled': False}, 'adxl345': {'enabled': False},
        }
        if calibrated:
            (self.dir / 'cal.json').write_text(json.dumps({'id': 'test', 'mm_per_px': .05, 'axis': 'y'}))
        if carrier:
            regs = bytearray(4096)
            struct.pack_into('<III', regs, 0, 0x56424331, 0x10000, 100000000)
            (self.dir / 'carrier').write_bytes(regs)  # PFAIL remains low at boot.
            if carrier == 'lighting':
                struct.pack_into('<I', regs, 0x20, 1 << 6)
                (self.dir / 'carrier').write_bytes(regs)
            cfg['hardware'] = {'devices': {'carrier_uio': str(self.dir / 'carrier')}}
        for e in extra:  # merged one level deep
            for k, v in e.items():
                cfg[k] = {**cfg.get(k, {}), **v} if isinstance(v, dict) else v
        (self.dir / 'cfg.json').write_text(json.dumps(cfg))
        self.log = (self.dir / 'daemon.log').open('w')
        self.p = subprocess.Popen([str(binary), '-c', str(self.dir / 'cfg.json')], stdout=self.log, stderr=self.log)

    def ready(self):
        for _ in range(150):
            if self.p.poll() is not None:
                return False
            if self.sock.exists():
                return True
            time.sleep(.02)
        return False

    def cmd(self, cmd, **args):
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(4)
            s.connect(str(self.sock))
            s.sendall((json.dumps(dict(cmd=cmd, **args)) + '\n').encode())
            data = b''
            while b'\n' not in data:
                data += s.recv(65536)
            return json.loads(data)

    def result(self, polls=200):
        for _ in range(polls):
            st = self.cmd('status')['data']
            last = st['measurement'].get('last')
            if last:
                return last
            time.sleep(.03)
        raise AssertionError('measurement did not finish')

    def close(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()
        self.log.close()

with tempfile.TemporaryDirectory(prefix='vibmon-regression-') as temp:
    root = pathlib.Path(temp)
    for name in ['point', 'pixel', 'startup', 'lighting', 'laser', 'shutdown', 'settings']:
        d = root / name
        d.mkdir()
        app = Daemon(d, calibrated=name != 'pixel', carrier={'startup': 'startup', 'lighting': 'lighting', 'laser': 'lighting'}.get(name, False))
        try:
            ready = app.ready()
            check(ready, name + ': starts without crashing')
            if not ready:
                print((d / 'daemon.log').read_text()[-1800:])
                continue
            if name == 'startup':
                check(app.cmd('status')['data']['power']['pfail'], 'PFAIL low at boot is retained')
            elif name == 'lighting':
                app.cmd('measure_start')
                check(app.cmd('status')['data']['light']['on'], 'automatic light is on during capture')
                last = app.result()
                rec = json.loads((d / 'records' / last['measurement_id'] / 'record.json').read_text())
                check(rec['lighting']['on'], 'record retains automatic light state after it is switched off')
            elif name == 'point':
                app.cmd('set_point', id='A', name='point A')
                check(app.cmd('roi', x=0.5, y=0.5, w=80, h=40)['ok'], 'measuring box accepts width and height')
                check(not app.cmd('roi', w=8)['ok'] and app.cmd('settings')['data']['roi_width_px'] == 80,
                      'a box below 16 px is refused and leaves the size unchanged')
                check(app.cmd('measure_start')['ok'], 'capture accepted')
                time.sleep(.25)
                app.cmd('set_point', id='B', name='point B')
                check(not app.cmd('roi', w=64, h=64)['ok'], 'capture refuses resizing the measuring box')
                last = app.result()
                rec = json.loads((d / 'records' / last['measurement_id'] / 'record.json').read_text())
                check(rec['point_id'] == 'A' and last['valid'], 'record keeps the point selected at capture start')
                check(last['peak_clear'] is True and rec['result']['peak_clear'] is True,
                      'a clean line is reported as a clear dominant frequency')
                check((rec['roi']['w'], rec['roi']['h']) == (80, 40), 'capture tracks the box size set by the operator')
            elif name == 'pixel':
                check(app.cmd('status')['data']['alarm_level'] == '未判级', 'startup has no normal vibration judgment')
                app.cmd('measure_start')
                last = app.result()
                check(last['valid'] and last['unit'] == 'px' and last['level'] == '未判级', 'uncalibrated pixels do not use mm alarm thresholds')
            elif name == 'laser':
                check(app.cmd('laser', on=True)['ok'], 'laser projector switches on')
                st = app.cmd('status')['data']
                check(st['laser']['on'] and not st['light']['on'], 'lighting and laser are separate outputs')
                app.cmd('measure_start')
                check(not app.cmd('status')['data']['laser']['on'], 'laser is off while displacement is tracked')
                check(not app.cmd('laser', on=True)['ok'], 'capture refuses manually re-enabling the projector')
                check(not app.cmd('status')['data']['laser']['on'], 'a refused projector command leaves LED2 off')
                check(not app.cmd('set', laser_off_during_measurement=False)['ok'],
                      'capture refuses changing the projector interlock policy')
                last = app.result()
                rec = json.loads((d / 'records' / last['measurement_id'] / 'record.json').read_text())
                check(rec['laser_projector'] == {'on': False, 'switched_off_for_capture': True},
                      'record notes the laser switched off for the capture')
                check(app.cmd('status')['data']['laser']['on'], 'laser is back on after the capture')
            elif name == 'settings':
                app.cmd('zoom', steps=4)
                check(app.cmd('pan', x=0.99, y=0.01)['ok'], 'the zoomed view can be moved')
                m = app.cmd('status')['data']['measurement']
                h = 0.5 / m['zoom']
                check(m['zoom'] > 2 and abs(m['zoom_center_x'] - (1 - h)) < 1e-9 and abs(m['zoom_center_y'] - h) < 1e-9,
                      'the zoomed view stays inside the image')
                app.cmd('zoom_reset')
                app.cmd('roi', w=96, h=32)
                r = app.cmd('set', duration_s=30, warn_pp=0.5, alarm_pp=1.0)
                check(r['ok'] and r['data']['duration_s'] == 30, 'settings are applied')
                check(not app.cmd('set', warn_pp=2.0, alarm_pp=1.0)['ok'], 'warning threshold above the alarm threshold is refused')
                check(app.cmd('settings')['data']['warn_pp'] == 0.5, 'a refused change leaves the settings unchanged')
                saved = json.loads((d / 'settings.json').read_text())
                check(saved['measurement']['duration_s'] == 30 and saved['alarm']['alarm_peak_to_peak'] == 1.0,
                      'settings are written to the settings file')
                app.close()
                app = Daemon(d)
                s = app.ready() and app.cmd('settings')['data']
                check(s and s['duration_s'] == 30 and (s['roi_width_px'], s['roi_height_px']) == (96, 32),
                      'settings and the box size survive a restart')
            elif name == 'shutdown':
                app.cmd('measure_start', duration_s=3)
                time.sleep(.15)
                app.cmd('shutdown')
                time.sleep(2)
                check(app.cmd('status')['data']['shutdown_prepared'], 'shutdown completes during capture')
                check(not list((d / 'records').glob('*/record.json')), 'cancelled capture produces no late result')
        finally:
            app.close()
            check(app.p.returncode == 0, name + ': SIGTERM follows orderly exit')

    # Multi-point method: motor-off baseline, then the same layout with 7 Hz motion.
    d = root / 'multipoint'
    d.mkdir()
    mp = {'measurement': {'method': 'multipoint', 'duration_s': 4,
                          'multipoint': {'baseline_dir': str(d / 'baselines'), 'baseline_s': 5.5}}}
    app = Daemon(d, extra=[mp, {'camera': {'synthetic': {'fps': 60, 'amplitude_px': 0}}}])
    try:
        check(app.ready(), 'multipoint: starts')
        time.sleep(1.5)  # frame size known, baseline looked up
        st = app.cmd('status')['data']['measurement']['multipoint']
        check(not st['baseline_valid'] and st['layout_error'] == '', 'multipoint: valid layout, no baseline yet')
        r = app.cmd('measure_start')
        check(not r['ok'] and r['error'] == 'baseline_required', 'multipoint: measuring needs a baseline')
        check(app.cmd('baseline_start')['ok'], 'multipoint: baseline capture starts')
        for _ in range(300):
            st = app.cmd('status')['data']['measurement']['multipoint']
            if st['baseline_valid']:
                break
            time.sleep(.05)
        check(st['baseline_valid'] and st['baseline_windows'] >= 5, 'multipoint: baseline saved for this layout')
        check(len(list((d / 'baselines').glob('*.vbb'))) == 1, 'multipoint: one baseline file')
        check(not list((d / 'records').glob('*/record.json')), 'multipoint: a baseline is not a measurement record')
    finally:
        app.close()
    app = Daemon(d, extra=[mp, {'camera': {'synthetic': {'fps': 60, 'amplitude_px': 1.5, 'freq_hz': 7}}}])
    try:
        check(app.ready(), 'multipoint: restarts')
        time.sleep(1.5)
        check(app.cmd('status')['data']['measurement']['multipoint']['baseline_valid'],
              'multipoint: baseline found again after a restart')
        check(app.cmd('measure_start')['ok'], 'multipoint: measurement starts')
        last = app.result(polls=400)
        check(last['valid'] and abs(last['dominant_hz'] - 7) < 0.3, 'multipoint: 7 Hz found above the baseline')
        rec = json.loads((d / 'records' / last['measurement_id'] / 'record.json').read_text())
        res = rec['result']
        check(res['method'] == 'multipoint' and 0 < res['multipoint']['passed_points'] == len(res['multipoint']['points']),
              'multipoint: record lists the points above the baseline')
        check(res['unit'] == 'px' and res['level'] == 'unknown', 'multipoint: pixels, no mm alarm level')
        app.cmd('roi', x=0.45)
        time.sleep(1.5)
        check(not app.cmd('status')['data']['measurement']['multipoint']['baseline_valid'],
              'multipoint: moving the box needs a new baseline')
    finally:
        app.close()
        check(app.p.returncode == 0, 'multipoint: SIGTERM follows orderly exit')

    # Acoustic reference from the synthetic PDM source: a tone modulated at the
    # camera's 12 Hz; the record carries the analysis, its spectrum and the WAV.
    d = root / 'acoustic'
    d.mkdir()
    ac = {'acoustic': {'enabled': True, 'source': 'synthetic', 'synthetic_hz': 12}, 'measurement': {'duration_s': 3}}
    app = Daemon(d, extra=[ac, {'camera': {'synthetic': {'fps': 60, 'freq_hz': 12}}}])
    try:
        check(app.ready(), 'acoustic: starts')
        for _ in range(100):
            a = app.cmd('status')['data']['measurement']['acoustic']
            if a.get('state') == 'ready':
                break
            time.sleep(.05)
        check(a['state'] == 'ready' and a['source'] == 'synthetic' and abs(a['sample_rate_hz'] - 31250) < 1,
              'acoustic: synthetic source ready at 31.25 kS/s')
        check(app.cmd('measure_start')['ok'], 'acoustic: capture accepted')
        last = app.result(polls=300)
        al = last.get('acoustic') or {}
        check(last['valid'] and al.get('valid') and al.get('relation') == 'agree' and abs((al.get('frequency_hz') or 0) - 12) < 0.3,
              'acoustic: the envelope agrees with the camera at 12 Hz')
        rd = d / 'records' / last['measurement_id']
        rec = json.loads((rd / 'record.json').read_text())['acoustic']
        n = rec['window']['samples'] if rec['available'] else 0
        check(rec['valid'] and rec['source'] == 'synthetic' and abs(n - 3 * 31250) < 1100,  # the video window, within two frames
              'acoustic: record holds the capture window')
        check((rd / 'acoustic.wav').stat().st_size == 44 + 2 * n and (rd / 'acoustic_spectrum.csv').exists(),
              'acoustic: WAV and envelope spectrum saved')
    finally:
        app.close()
        check(app.p.returncode == 0, 'acoustic: SIGTERM follows orderly exit')

sys.exit(bool(failures))
