// Digital twin of the instrument: a product-grade 3D model drawn on a 2D canvas
// (no WebGL, works offline). Rounded, chamfered housing; glass, metal, PCB and
// screen materials with specular light; decals for the live screen, PCB traces,
// grilles and lens glass; a soft floor shadow. Drag to rotate (with inertia),
// pinch/wheel to zoom, setExplode(0..1) spreads the modules, tap a module to
// select it. Status (ok/warn/alarm/off/unknown) comes from page_device.js.
(function () {
  const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  const rgb = (hex) => {
    const h = (hex || '#888').replace('#', '');
    const n = parseInt(h.length === 3 ? h.split('').map((c) => c + c).join('') : h, 16);
    return [n >> 16 & 255, n >> 8 & 255, n & 255];
  };
  const norm = (v) => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };

  // ------------------------------------------------------------ geometry
  // Faces: {p: [[x,y,z]...], n: [x,y,z], mat, decal?}. Model space in cm:
  // x right, y down, z towards the lens side (front).
  function rrProfile(w, h, r, seg) {
    const pts = [];
    const cs = [[w / 2 - r, h / 2 - r, 0], [-w / 2 + r, h / 2 - r, Math.PI / 2], [-w / 2 + r, -h / 2 + r, Math.PI], [w / 2 - r, -h / 2 + r, Math.PI * 1.5]];
    cs.forEach((c) => { for (let i = 0; i <= seg; i++) { const a = c[2] + i / seg * Math.PI / 2; pts.push([c[0] + Math.cos(a) * r, c[1] + Math.sin(a) * r]); } });
    return pts;
  }
  // Rounded-rectangle prism along z with chamfered caps (bevel b at each end).
  function rbox(cx, cy, cz, w, h, d, r, b, mat, capMat, seg) {
    seg = seg || 6;
    b = Math.min(b || 0, r * 0.8, d / 3);
    const out = [], P = rrProfile(w, h, r, seg), Q = b ? rrProfile(w - 2 * b, h - 2 * b, Math.max(0.05, r - b), seg) : P;
    const z0 = cz - d / 2, z1 = cz + d / 2;
    const at = (p, z) => [cx + p[0], cy + p[1], z];
    for (let i = 0; i < P.length; i++) {
      const a = P[i], c = P[(i + 1) % P.length];
      const n = norm([c[1] - a[1], -(c[0] - a[0]), 0]);
      out.push({ p: [at(a, z0 + b), at(c, z0 + b), at(c, z1 - b), at(a, z1 - b)], n, mat });
      if (b) {
        const qa = Q[i], qc = Q[(i + 1) % Q.length];
        out.push({ p: [at(a, z1 - b), at(c, z1 - b), at(qc, z1), at(qa, z1)], n: norm([n[0], n[1], 1.1]), mat });
        out.push({ p: [at(qa, z0), at(qc, z0), at(c, z0 + b), at(a, z0 + b)], n: norm([n[0], n[1], -1.1]), mat });
      }
    }
    out.push({ p: Q.map((q) => at(q, z1)), n: [0, 0, 1], mat: capMat || mat });
    out.push({ p: Q.slice().reverse().map((q) => at(q, z0)), n: [0, 0, -1], mat: capMat || mat });
    return out;
  }
  function box(cx, cy, cz, w, h, d, mat, decalTop) {
    const x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - h / 2, y1 = cy + h / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    return [
      { p: [[x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]], n: [0, 0, 1], mat, decal: decalTop },
      { p: [[x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0]], n: [0, 0, -1], mat },
      { p: [[x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1]], n: [1, 0, 0], mat },
      { p: [[x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0]], n: [-1, 0, 0], mat },
      { p: [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1]], n: [0, -1, 0], mat },
      { p: [[x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0]], n: [0, 1, 0], mat },
    ];
  }
  // Cylinder along an axis; knurl > 0 ridges the side (the knob).
  function cyl(cx, cy, cz, r, len, axis, mat, opt) {
    opt = opt || {};
    const n = opt.n || 22, knurl = opt.knurl || 0;
    const map = (u, v, w) => (axis === 'z' ? [cx + u, cy + v, cz + w] : axis === 'y' ? [cx + u, cy + w, cz + v] : [cx + w, cy + u, cz + v]);
    const nm = (u, v, w) => (axis === 'z' ? [u, v, w] : axis === 'y' ? [u, w, v] : [w, u, v]);
    const out = [], cap1 = [], cap0 = [];
    for (let i = 0; i < n; i++) {
      const a = i / n * Math.PI * 2, c = (i + 1) / n * Math.PI * 2;
      const ra = r * (knurl && i % 2 ? 1 - knurl : 1), rc = r * (knurl && (i + 1) % 2 ? 1 - knurl : 1);
      const A = [Math.cos(a) * ra, Math.sin(a) * ra], C = [Math.cos(c) * rc, Math.sin(c) * rc];
      out.push({ p: [map(A[0], A[1], -len / 2), map(C[0], C[1], -len / 2), map(C[0], C[1], len / 2), map(A[0], A[1], len / 2)], n: nm(Math.cos((a + c) / 2), Math.sin((a + c) / 2), 0), mat: opt.side || mat });
      cap1.push(map(A[0], A[1], len / 2)); cap0.unshift(map(A[0], A[1], -len / 2));
    }
    out.push({ p: cap1, n: nm(0, 0, 1), mat: opt.cap || mat, decal: opt.decal });
    out.push({ p: cap0, n: nm(0, 0, -1), mat: opt.back || mat });
    return out;
  }
  // Flat decal quad facing +z (or -z with back).
  function plate(cx, cy, z, w, h, mat, decal, back) {
    const x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - h / 2, y1 = cy + h / 2;
    return back ? [{ p: [[x1, y0, z], [x0, y0, z], [x0, y1, z], [x1, y1, z]], n: [0, 0, -1], mat, decal }]
      : [{ p: [[x0, y0, z], [x1, y0, z], [x1, y1, z], [x0, y1, z]], n: [0, 0, 1], mat, decal }];
  }
  function plate3(cx, y, cz, w, d, mat) {
    const x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    return [{ p: [[x0, y, z0], [x1, y, z0], [x1, y, z1], [x0, y, z1]], n: [0, -1, 0], mat }];
  }
  function sidePlate(x, cy, cz, h, d, mat, decal, left) {
    const y0 = cy - h / 2, y1 = cy + h / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    return left ? [{ p: [[x, y0, z0], [x, y0, z1], [x, y1, z1], [x, y1, z0]], n: [-1, 0, 0], mat, decal }]
      : [{ p: [[x, y0, z1], [x, y0, z0], [x, y1, z0], [x, y1, z1]], n: [1, 0, 0], mat, decal }];
  }
  const cat = (...a) => [].concat(...a);
  // Fan blades in an x = const plane, turned about (cy, cz) at draw time.
  function blades(cx, cy, cz, r, mat) {
    const out = [];
    for (let i = 0; i < 7; i++) {
      const a = i / 7 * Math.PI * 2;
      const p = (t, s) => [cx, cy + Math.cos(a + s) * t, cz + Math.sin(a + s) * t];
      out.push({ p: [p(0.45, 0.25), p(r, 0.05), p(r, 0.5), p(0.45, 0.55)], n: [-1, 0, 0], mat, spin: [cy, cz] });
    }
    return out;
  }

  // ------------------------------------------------------------ product
  const W = 24, H = 15, D = 7.6, R = 2.8;
  const ZF = D / 2, ZB = -D / 2;
  // Glossy caps get a diagonal sheen.
  const sheen = (faces) => faces.map((f) => (f.mat === 'glass' && f.n[2] > 0.9 ? Object.assign({ decal: 'sheen' }, f) : f));
  // Parts on the outside of the housing: drawn over it while the surface they
  // sit on faces the viewer, hidden while it faces away.
  const FRONT = [0, 0, 1], BACK = [0, 0, -1], TOP = [0, -1, 0], BOTTOM = [0, 1, 0], LEFT = [-1, 0, 0], RIGHT = [1, 0, 0];
  const mount = (n, faces) => faces.map((f) => Object.assign({}, f, { mount: n }));
  // Parts inside the closed housing: only drawn once their module moves out.
  const enclosed = (faces) => faces.map((f) => Object.assign({}, f, { inside: true }));
  const MODULES = (() => [
    { id: 'shellR', shell: true, name: '后壳', parts: cat(
      rbox(0, 0, -1.95, W, H, 3.7, R, 0.7, 'shell', 'shell'),
      rbox(0, 0, -0.05, W - 0.5, H - 0.5, 0.18, R - 0.25, 0, 'trim'),
      mount(BOTTOM, cyl(0, H / 2 + 0.18, -1.2, 1.25, 0.36, 'y', 'metal', { n: 18 })),
      mount(LEFT, sidePlate(-W / 2 - 0.01, -1.2, -1.9, 6.2, 2.6, 'grille', 'grille', true)),
      mount(RIGHT, sidePlate(W / 2 + 0.01, 3.4, -1.9, 1.0, 2.2, 'port', 'portC')),
      mount(RIGHT, sidePlate(W / 2 + 0.01, 5.0, -1.9, 1.1, 1.1, 'port', 'portDC'))), ex: [0, 0, -11], delay: 0 },
    { id: 'shellF', shell: true, name: '前壳', parts: cat(
      sheen(rbox(0, 0, 1.95, W, H, 3.7, R, 0.7, 'shell', 'glass')),
      mount(FRONT, plate(-5.2, 3.6, ZF + 0.01, 1.4, 0.5, 'glassHi', 'micHole')),
      mount(FRONT, plate(7.4, -5.3 + 10.6, ZF + 0.01, 3.0, 0.5, 'glass', 'brand'))), ex: [0, 0, 15], delay: 0 },
    { id: 'lcd', short: '触摸屏', name: '7 寸触摸屏', parts: mount(BACK, cat(rbox(0, -0.3, ZB - 0.12, 17.8, 11.2, 0.3, 0.8, 0.08, 'bezel', 'bezel'), plate(0, -0.3, ZB - 0.29, 15.6, 9.2, 'screen', 'screen', true))), ex: [0, 0, -19], delay: 0.05 },
    { id: 'camera', short: '双目相机', name: 'AN5642 双目相机', parts: cat(
      enclosed(box(0, -1.6, 2.35, 10.4, 3.2, 0.22, 'pcbDark', 'pcb')),
      mount(FRONT, cat(
        cyl(-3.3, -1.6, 3.86, 1.32, 0.1, 'z', 'chrome', { cap: 'chrome', n: 28 }), cyl(3.3, -1.6, 3.86, 1.32, 0.1, 'z', 'chrome', { cap: 'chrome', n: 28 }),
        cyl(-3.3, -1.6, 3.27, 1.0, 1.4, 'z', 'metal', { cap: 'lens', decal: 'lens', n: 28 }), cyl(3.3, -1.6, 3.27, 1.0, 1.4, 'z', 'metal', { cap: 'lens', decal: 'lens', n: 28 })))), ex: [0, 0, 9.5], delay: 0.15 },
    { id: 'light', short: '照明', name: '照明灯 LED1', parts: mount(FRONT, cat(rbox(-7.2, -5.2, ZF + 0.08, 4.6, 0.62, 0.16, 0.31, 0, 'lamp'), rbox(7.2, -5.2, ZF + 0.08, 4.6, 0.62, 0.16, 0.31, 0, 'lamp'))), ex: [0, 0, 15], delay: 0, glow: 'light' },
    { id: 'laser', short: '十字投影', name: '十字投影 LED2', parts: mount(FRONT, cyl(0, -5.2, ZF + 0.08, 0.42, 0.18, 'z', 'chrome', { cap: 'laser' })), ex: [0, 0, 15], delay: 0, glow: 'laser' },
    { id: 'sensors', short: '传感器', name: '环境与测距传感器', parts: cat(box(6.4, 3.4, 2.6, 3.0, 2.2, 0.18, 'pcbBlue', 'pcb'), box(6.4, 3.4, 2.85, 1.0, 0.7, 0.3, 'chip'), mount(FRONT, rbox(6.4, 3.4, ZF + 0.03, 1.6, 0.8, 0.06, 0.4, 0, 'sensorWin'))), ex: [6, 3, 7.5], delay: 0.3, inner: true },
    { id: 'mic', short: '麦克风', name: 'MP34DT01 麦克风', parts: cat(box(-5.2, 3.6, 2.6, 2.2, 1.6, 0.16, 'pcbViolet', 'pcb'), box(-5.2, 3.6, 2.8, 0.7, 0.5, 0.25, 'metal')), ex: [-6, 3, 7.5], delay: 0.3, inner: true },
    { id: 'nfc', short: 'NFC', name: 'PN532 NFC', parts: cat(box(-8.2, -1.4, 2.6, 4.4, 4.4, 0.14, 'pcbBlue', 'coil')), ex: [-8, -1, 6], delay: 0.25, inner: true },
    { id: 'carrier', short: '主载板', name: '主载板', parts: cat(
      box(0, 0.6, 0.9, 21.4, 12.2, 0.2, 'pcb', 'pcb'),
      box(-7, 4.8, 1.25, 2.4, 1.2, 0.5, 'conn'), box(8.6, 4.9, 1.25, 1.8, 1.0, 0.5, 'conn'), box(2.4, 3.4, 1.15, 2.0, 2.0, 0.3, 'chip'),
      cyl(5.6, 3.6, 1.6, 0.6, 1.2, 'z', 'cap', { n: 14 }), box(-2.2, 4.6, 1.1, 3.4, 0.6, 0.25, 'conn')), ex: [0, 0, 3], delay: 0.2, inner: true },
    { id: 'nano', short: 'ESP32', name: 'Nano ESP32 协处理器', parts: cat(box(7.6, -3.6, 1.4, 4.6, 1.8, 0.16, 'pcbTeal', 'pcb'), box(7.6, -3.6, 1.62, 1.4, 1.1, 0.28, 'metal'), box(10.1, -3.6, 1.55, 0.6, 0.9, 0.35, 'chrome')), ex: [8, -4, 4], delay: 0.35, inner: true },
    { id: 'core', short: 'AX7010', name: 'AX7010 核心板', parts: cat(
      box(-1.6, -0.4, -0.75, 10.4, 6.8, 0.2, 'pcb', 'pcb'), box(-1.6, -0.4, -1.05, 3.2, 3.2, 0.4, 'chip'),
      ...[0, 1, 2, 3, 4, 5, 6].map((i) => box(-3.0 + i * 0.47, -0.4, -1.55, 0.14, 3.0, 0.7, 'heatsink')),
      box(2.6, -2.6, -1.0, 1.8, 1.0, 0.3, 'chip'), box(2.6, 1.6, -1.0, 1.8, 1.0, 0.3, 'chip')), ex: [0, 0, -5.5], delay: 0.25, inner: true },
    { id: 'printer', short: '打印机', name: 'EM5820H 打印机', parts: cat(
      rbox(-6.6, 4.4, -1.6, 7.4, 3.6, 3.2, 0.5, 0.1, 'printer', 'printer'),
      cyl(-6.6, 4.3, -1.6, 1.3, 6.6, 'x', 'paper', { cap: 'paper' }),
      box(-6.6, 2.55, -0.4, 5.6, 0.12, 0.5, 'chrome')), ex: [-3, 9, -2], delay: 0.4, inner: true },
    { id: 'power', short: '电源', name: '电源与功率监测', parts: cat(box(8.4, 4.6, -1.5, 4.4, 2.6, 0.18, 'pcbDark', 'pcb'), box(8.0, 4.6, -1.05, 1.6, 1.2, 0.7, 'inductor'), cyl(10.7, 5.0, -1.9, 0.5, 1.2, 'x', 'chrome', { n: 14 })), ex: [8, 7, -2.5], delay: 0.4, inner: true },
    { id: 'fan', short: '风扇', name: '散热风扇', parts: cat(box(-10.6, -1.2, -1.9, 0.7, 4.6, 4.6, 'fanFrame'), cyl(-11.0, -1.2, -1.9, 0.55, 0.3, 'x', 'metal', { n: 14 }), blades(-11.0, -1.2, -1.9, 2.0, 'blade')), ex: [-9, 0, -3], delay: 0.35, inner: true },
    { id: 'panel', short: '前面板', name: '前面板（旋钮/状态灯）', parts: cat(
      mount(TOP, cat(
        cyl(6.6, -H / 2 - 0.55, -0.4, 1.0, 1.1, 'y', 'knob', { n: 28, knurl: 0.07, cap: 'knobTop' }),
        cyl(3.6, -H / 2 - 0.12, -0.4, 0.55, 0.24, 'y', 'button', { n: 18 }),
        // The light bar is a flat strip on the top surface.
        plate3(-2.4, -H / 2 - 0.02, -0.4, 5.2, 0.4, 'ledbar'))),
      enclosed(box(1.5, -H / 2 + 1.2, -0.4, 10, 0.16, 2.6, 'pcbDark'))), ex: [0, -8, 0], delay: 0.1 },
  ])().map((m) => (m.inner ? Object.assign(m, { parts: m.parts.map((f) => (f.mount ? f : Object.assign({}, f, { inside: true }))) }) : m));

  // ------------------------------------------------------------ materials
  function palette(dark) {
    return {
      shell: dark ? { c: [56, 61, 71], s: 0.4, p: 18 } : { c: [234, 237, 242], s: 0.3, p: 16 },
      trim: { c: rgb(css('--accent')), s: 0.8, p: 30, e: 0.25 },
      glass: { c: [12, 15, 22], s: 0.9, p: 60 },
      glassHi: { c: [18, 22, 30], s: 0.5, p: 40 },
      bezel: { c: [20, 23, 29], s: 0.5, p: 40 },
      screen: { c: [8, 12, 20], s: 0.8, p: 70 },
      metal: { c: [120, 126, 136], s: 0.9, p: 26 },
      chrome: { c: [176, 182, 192], s: 1.1, p: 40 },
      lens: { c: [6, 8, 14], s: 1.0, p: 80 },
      lamp: { c: [214, 218, 224], s: 0.4, p: 20 },
      laser: { c: [60, 20, 26], s: 0.9, p: 50 },
      sensorWin: { c: [14, 16, 22], s: 0.9, p: 60 },
      pcb: { c: [24, 84, 66], s: 0.25, p: 12 },
      pcbDark: { c: [28, 34, 44], s: 0.25, p: 12 },
      pcbBlue: { c: [30, 64, 128], s: 0.25, p: 12 },
      pcbTeal: { c: [16, 110, 112], s: 0.25, p: 12 },
      pcbViolet: { c: [86, 52, 122], s: 0.25, p: 12 },
      chip: { c: [26, 28, 32], s: 0.35, p: 20 },
      conn: { c: [232, 228, 218], s: 0.2, p: 10 },
      cap: { c: [70, 74, 82], s: 0.8, p: 30 },
      heatsink: { c: [150, 156, 166], s: 0.7, p: 20 },
      printer: { c: dark ? [70, 76, 86] : [196, 201, 209], s: 0.3, p: 14 },
      paper: { c: [246, 244, 238], s: 0.15, p: 8 },
      inductor: { c: [46, 48, 54], s: 0.4, p: 16 },
      fanFrame: { c: [34, 37, 43], s: 0.3, p: 14 },
      blade: { c: [58, 62, 70], s: 0.4, p: 16 },
      knob: { c: [92, 98, 108], s: 0.9, p: 28 },
      knobTop: { c: [140, 146, 156], s: 1.0, p: 36 },
      button: { c: dark ? [40, 44, 52] : [210, 214, 220], s: 0.5, p: 20 },
      ledbar: { c: [20, 24, 30], s: 0.6, p: 40 },
      port: { c: [16, 18, 22], s: 0.3, p: 10 },
      grille: { c: dark ? [44, 48, 56] : [216, 220, 226], s: 0.2, p: 10 },
    };
  }

  function rot(p, ry, rx) {
    const c = Math.cos(ry), s = Math.sin(ry);
    const x = p[0] * c + p[2] * s;
    let z = -p[0] * s + p[2] * c;
    const c2 = Math.cos(rx), s2 = Math.sin(rx);
    const y = p[1] * c2 - z * s2;
    z = p[1] * s2 + z * c2;
    return [x, y, z];
  }
  const STAG = 0.45;  // spread of the explode start times
  const ease = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
  const bil = (q, u, v) => {
    const a = [q[0][0] + (q[1][0] - q[0][0]) * u, q[0][1] + (q[1][1] - q[0][1]) * u];
    const b = [q[3][0] + (q[2][0] - q[3][0]) * u, q[3][1] + (q[2][1] - q[3][1]) * u];
    return [a[0] + (b[0] - a[0]) * v, a[1] + (b[1] - a[1]) * v];
  };
  function rr(g, x, y, w, h, r) { g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); }
  function inside(x, y, pts) {
    let c = false;
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
      if ((pts[i][1] > y) !== (pts[j][1] > y) && x < (pts[j][0] - pts[i][0]) * (y - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0]) c = !c;
    }
    return c;
  }
  // Deterministic PCB trace pattern in (u, v).
  const TRACES = (() => {
    let s = 7;
    const r = () => ((s = (s * 16807) % 2147483647) / 2147483647);
    const out = [];
    for (let i = 0; i < 26; i++) {
      const u = r(), v = r(), horiz = r() > 0.5, l = 0.15 + r() * 0.4;
      out.push(horiz ? [[u, v], [Math.min(0.97, u + l), v], [Math.min(0.97, u + l), Math.min(0.97, v + r() * 0.2)]] : [[u, v], [u, Math.min(0.97, v + l)], [Math.min(0.97, u + r() * 0.2), Math.min(0.97, v + l)]]);
    }
    return out;
  })();

  class Twin {
    constructor(canvas, opts) {
      this.c = canvas;
      this.o = opts || {};
      this.ry = -0.55; this.rx = -0.32; this.zoom = 1; this.vel = 0;
      this.explodeT = 0; this.explodeTo = 0;
      this.sel = null; this.status = {}; this.lightOn = false; this.laserOn = false; this.fanOn = false; this.ledLevel = 'unknown';
      this.auto = true; this.running = false; this.hits = []; this.spin = 0; this.wave = null;
      this.bind();
    }
    setStatus(map) { this.status = map || {}; this.kick(); }
    setOutputs(light, laser, level, fan, wave) { this.lightOn = light; this.laserOn = laser; this.ledLevel = level; this.fanOn = fan; if (wave) this.wave = wave; this.kick(); }
    setExplode(v) { this.explodeTo = U.clamp(v, 0, 1); this.kick(); if (this.o.onExplode) this.o.onExplode(this.explodeTo); }
    explode(on) { this.setExplode(on ? 1 : 0); }
    select(id) { this.sel = id; if (id && this.explodeTo < 0.5) this.setExplode(1); this.kick(); if (this.o.onSelect) this.o.onSelect(id); }
    reset() { this.ry = -0.55; this.rx = -0.32; this.zoom = 1; this.auto = true; this.kick(); }
    modules() { return MODULES.filter((m) => !m.shell); }
    bind() {
      const c = this.c;
      let last = null, pinch = null, moved = 0, t0 = 0, lastTouch = 0;
      // A tap fires touch events and then emulated mouse events; handling both
      // selected and immediately deselected the part.
      const fromMouse = (e) => !e.touches && !e.changedTouches;
      const stale = (e) => fromMouse(e) && Date.now() - lastTouch < 900;
      const pos = (e) => (e.touches ? [e.touches[0].clientX, e.touches[0].clientY] : [e.clientX, e.clientY]);
      const start = (e) => {
        if (e.touches) lastTouch = Date.now();
        else if (stale(e)) return;
        if (e.touches && e.touches.length === 2) { pinch = Math.hypot(e.touches[0].clientX - e.touches[1].clientX, e.touches[0].clientY - e.touches[1].clientY); last = null; return; }
        last = pos(e); moved = 0; t0 = Date.now(); this.auto = false; this.vel = 0;
      };
      const move = (e) => {
        if (e.touches && e.touches.length === 2 && pinch) {
          const d = Math.hypot(e.touches[0].clientX - e.touches[1].clientX, e.touches[0].clientY - e.touches[1].clientY);
          this.zoom = U.clamp(this.zoom * d / pinch, 0.6, 2.6); pinch = d; this.kick();
          if (e.cancelable) e.preventDefault();
          return;
        }
        if (!last) return;
        const p = pos(e);
        const dx = p[0] - last[0], dy = p[1] - last[1];
        moved += Math.abs(dx) + Math.abs(dy);
        this.vel = dx * 0.011;
        this.ry += dx * 0.011; this.rx = U.clamp(this.rx - dy * 0.008, -1.3, 1.3);
        last = p; this.kick();
        if (e.cancelable) e.preventDefault();
      };
      const end = (e) => {
        if (e.changedTouches) { lastTouch = Date.now(); if (e.cancelable) e.preventDefault(); }  // no emulated click / dblclick
        if (pinch && (!e.touches || e.touches.length < 2)) { pinch = null; last = null; return; }
        if (last && moved < 8 && Date.now() - t0 < 400) this.tap(last[0], last[1]);
        last = null; this.kick();
      };
      c.addEventListener('touchstart', start, { passive: true });
      c.addEventListener('touchmove', move, { passive: false });
      c.addEventListener('touchend', end, { passive: false });
      c.addEventListener('touchcancel', () => { last = null; pinch = null; });
      c.addEventListener('mousedown', start);
      window.addEventListener('mousemove', (e) => { if (last) move(e); });
      window.addEventListener('mouseup', (e) => { if (last) end(e); });
      c.addEventListener('wheel', (e) => { this.zoom = U.clamp(this.zoom * (e.deltaY < 0 ? 1.08 : 0.93), 0.6, 2.6); this.kick(); e.preventDefault(); }, { passive: false });
      c.addEventListener('dblclick', (e) => { if (!stale(e)) this.reset(); });
      if (typeof IntersectionObserver !== 'undefined') new IntersectionObserver((en) => { this.visible = en[0].isIntersecting; if (this.visible) this.kick(); }).observe(c);
    }
    tap(x, y) {
      const r = this.c.getBoundingClientRect();
      const px = x - r.left, py = y - r.top;
      for (let i = this.hits.length - 1; i >= 0; i--) {
        const h = this.hits[i];
        if (inside(px, py, h.pts)) {
          U.haptic(8);
          if (h.shell) { if (this.explodeTo < 0.5) this.setExplode(1); return; }  // the closed device opens
          this.select(this.sel === h.id ? null : h.id);
          return;
        }
      }
      if (this.sel) this.select(null);
    }
    kick() {
      if (this.running) return;
      this.running = true;
      const step = () => {
        if (!this.c.isConnected || document.hidden || this.visible === false || !this.c.offsetParent) { this.running = false; return; }
        this.draw();
        requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
    }
    draw() {
      const c = this.c;
      const dpr = Math.min(2.5, window.devicePixelRatio || 1);
      const Wc = c.clientWidth, Hc = c.clientHeight;
      if (!Wc || !Hc) return;
      if (c.width !== Math.round(Wc * dpr) || c.height !== Math.round(Hc * dpr)) { c.width = Math.round(Wc * dpr); c.height = Math.round(Hc * dpr); }
      const g = c.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, Wc, Hc);
      const t = performance.now() / 1000;
      if (this.auto && this.explodeTo > 0.3) {
        // Exploded: settle into a three-quarter view that shows the layers.
        const target = -0.95 + Math.round((this.ry + 0.95) / (Math.PI * 2)) * Math.PI * 2;
        this.ry += (target - this.ry) * 0.05;
        this.rx += (-0.42 - this.rx) * 0.05;
      } else if (this.auto) this.ry += 0.0028;
      else if (Math.abs(this.vel) > 0.0004) { this.ry += this.vel; this.vel *= 0.93; }
      this.explodeT += (this.explodeTo - this.explodeT) * 0.08;
      const e = ease(U.clamp(this.explodeT, 0, 1));
      this.spin += this.fanOn ? 0.35 : 0.015;
      const dark = document.documentElement.getAttribute('data-theme') === 'dark';
      const M = palette(dark);
      const scale = Math.min(Wc / (29 + 16 * e), Hc / (22 + 24 * e)) * this.zoom;
      const cx = Wc / 2, cy = Hc * 0.5, F = 140;
      const L = norm([-0.45, -0.75, 0.55]), L2 = norm([0.8, 0.1, -0.4]);
      const proj = (q) => { const s = F / (F - q[2]); return [cx + q[0] * s * scale, cy + q[1] * s * scale, q[2]]; };
      const accent = rgb(css('--accent'));
      const stCol = { ok: rgb(css('--ok')), warn: rgb(css('--warn')), alarm: rgb(css('--alarm')), off: rgb(css('--text-3')), unknown: rgb(css('--unknown')) };
      // Soft floor shadow under the (spread) device.
      const floorY = H / 2 + 1.2 + e * 9;
      const sh = [[-W / 2 - 1.5, floorY, ZB - 1.5 - 10 * e], [W / 2 + 1.5, floorY, ZB - 1.5 - 10 * e], [W / 2 + 1.5, floorY, ZF + 1.5 + 8 * e], [-W / 2 - 1.5, floorY, ZF + 1.5 + 8 * e]].map((p) => proj(rot(p, this.ry, this.rx)));
      g.save();
      g.filter = 'blur(18px)';
      g.fillStyle = dark ? 'rgba(0,0,0,.6)' : 'rgba(30,40,80,.16)';
      g.beginPath(); sh.forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.closePath(); g.fill();
      g.restore();
      const polys = [], centers = {};
      const spinC = Math.cos(this.spin), spinS = Math.sin(this.spin);
      MODULES.forEach((m) => {
        // Each module leaves on its own beat: shells first, then the layers inside.
        const raw = U.clamp(this.explodeT, 0, 1);
        const em = ease(U.clamp((raw * (1 + STAG) - (m.delay || 0)) / 1, 0, 1));
        const attached = em < 0.5;  // still on (or close to) the housing
        const off = m.ex.map((v) => v * em);
        const shellAlpha = m.shell ? 1 - 0.9 * U.clamp(em * 1.3, 0, 1) : 1;
        let sx = 0, sy = 0, sz = 0, cnt = 0;
        m.parts.forEach((f) => {
          const pts = f.p.map((p0) => {
            let p = p0;
            if (f.spin) { const dy = p[1] - f.spin[0], dz = p[2] - f.spin[1]; p = [p[0], f.spin[0] + dy * spinC - dz * spinS, f.spin[1] + dy * spinS + dz * spinC]; }
            const q = rot([p[0] + off[0], p[1] + off[1], p[2] + off[2]], this.ry, this.rx);
            sx += q[0]; sy += q[1]; sz += q[2]; cnt++;
            return proj(q);
          });
          if (f.inside && em < 0.03) return;  // hidden inside the closed housing
          const n = rot(f.n, this.ry, this.rx);
          if (n[2] <= 0.0001 && !f.spin) return;
          const onTop = f.mount && attached;
          if (onTop && rot(f.mount, this.ry, this.rx)[2] < 0.02) return;  // its surface faces away
          let z = 0; pts.forEach((p) => { z += p[2]; });
          polys.push({ m, f, pts, n, z: z / pts.length, alpha: shellAlpha, onTop });
        });
        if (cnt) centers[m.id] = proj([sx / cnt, sy / cnt, sz / cnt]);
      });
      // Painter's order. Closed: faces by depth (only the housing and what is
      // on it remain). Exploded: each module is a layer, ordered by its mean
      // depth, so a large housing face can no longer cut through a small part.
      // Parts mounted on the housing go last, over the surface they sit on.
      const layered = e > 0.15;
      const depth = {};
      if (layered) {
        const acc = {};
        polys.forEach((p) => { if (p.onTop) return; const a = acc[p.m.id] || (acc[p.m.id] = [0, 0]); a[0] += p.z; a[1]++; });
        Object.keys(acc).forEach((k) => { depth[k] = acc[k][0] / acc[k][1]; });
      }
      polys.sort((a, b) => {
        if (a.onTop !== b.onTop) return a.onTop ? 1 : -1;
        if (layered && !a.onTop && a.m !== b.m) return depth[a.m.id] - depth[b.m.id];
        return a.z - b.z;
      });
      this.hits = [];
      const V = [0, 0, 1];
      const hv = norm([L[0] + V[0], L[1] + V[1], L[2] + V[2]]);
      polys.forEach((p) => {
        const m = p.m, f = p.f, n = p.n;
        const mat = M[f.mat] || M.shell;
        const lam = Math.max(0, n[0] * L[0] + n[1] * L[1] + n[2] * L[2]);
        const rim = Math.max(0, n[0] * L2[0] + n[1] * L2[1] + n[2] * L2[2]) * 0.18;
        const spec = Math.pow(Math.max(0, n[0] * hv[0] + n[1] * hv[1] + n[2] * hv[2]), mat.p) * mat.s;
        let col = mat.c.map((v) => v * (0.5 + 0.55 * lam + rim) + 255 * spec * 0.55);
        if (mat.e) col = col.map((v, i) => v + mat.c[i] * mat.e);
        if (m.glow === 'light' && this.lightOn && f.mat === 'lamp' && n[2] > 0.4) col = [255, 252, 240];
        if (f.mat === 'laser' && this.laserOn) col = [255, 70, 80];
        if (f.mat === 'ledbar') col = this.ledCol(t);
        let alpha = p.alpha;
        if (this.sel && this.sel !== m.id) alpha *= m.shell ? 0.5 : 0.2;
        if (this.sel === m.id) col = col.map((v, i) => v * 0.72 + accent[i] * 0.28);
        col = col.map((v) => Math.max(0, Math.min(255, Math.round(v))));
        g.globalAlpha = alpha;
        g.beginPath();
        p.pts.forEach((q, i) => (i ? g.lineTo(q[0], q[1]) : g.moveTo(q[0], q[1])));
        g.closePath();
        const fill = 'rgb(' + col.join(',') + ')';
        g.fillStyle = fill; g.fill();
        g.strokeStyle = fill; g.lineWidth = 0.6; g.stroke();  // closes hairline seams between faces
        if (f.decal) this.decal(g, f.decal, p.pts, t, dark);
        if (this.sel === m.id) { g.strokeStyle = 'rgba(' + accent.join(',') + ',.9)'; g.lineWidth = 1; g.stroke(); }
        g.globalAlpha = 1;
        if (!m.shell || e < 0.5) this.hits.push({ id: m.id, pts: p.pts, shell: m.shell, onTop: p.onTop });
      });
      this.glows(g, proj, e, dark);
      this.centers = centers;
    }
    glows(g, proj, e, dark) {
      const front = rot([0, 0, 1], this.ry, this.rx)[2];
      if (front <= 0.05 || !(this.lightOn || this.laserOn)) return;
      g.save(); g.globalCompositeOperation = dark ? 'lighter' : 'source-over';
      const at = (x) => proj(rot([x, -5.2, ZF + 0.2 + 15 * ease(U.clamp(this.explodeT * (1 + STAG), 0, 1))], this.ry, this.rx));
      if (this.lightOn) [-7.2, 7.2].forEach((x) => {
        const q = at(x), r = 46 * this.zoom;
        const gr = g.createRadialGradient(q[0], q[1], 0, q[0], q[1], r);
        gr.addColorStop(0, 'rgba(255,248,225,' + (dark ? 0.35 : 0.5) + ')'); gr.addColorStop(1, 'rgba(255,248,225,0)');
        g.fillStyle = gr; g.beginPath(); g.arc(q[0], q[1], r, 0, Math.PI * 2); g.fill();
      });
      if (this.laserOn) {
        const q = at(0);
        g.strokeStyle = 'rgba(255,60,72,.65)'; g.lineWidth = 1.2;
        g.beginPath(); g.moveTo(q[0] - 30, q[1]); g.lineTo(q[0] + 30, q[1]); g.moveTo(q[0], q[1] - 30); g.lineTo(q[0], q[1] + 30); g.stroke();
      }
      g.restore();
    }
    ledCol(t) {
      const lv = this.ledLevel;
      const base = lv === 'alarm' ? [255, 77, 90] : lv === 'warn' ? [255, 176, 32] : lv === 'normal' ? [40, 210, 150] : rgb(css('--accent'));
      const k = 0.75 + 0.25 * Math.sin(t * (lv === 'alarm' ? 8 : 2.2));
      return base.map((v) => v * k);
    }
    decal(g, kind, q, t, dark) {
      g.save();
      g.beginPath(); q.forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.closePath(); g.clip();
      if (kind === 'sheen') {
        let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        q.forEach((p) => { x0 = Math.min(x0, p[0]); y0 = Math.min(y0, p[1]); x1 = Math.max(x1, p[0]); y1 = Math.max(y1, p[1]); });
        const gr = g.createLinearGradient(x0, y0, x1, y1);
        gr.addColorStop(0, 'rgba(255,255,255,.02)'); gr.addColorStop(0.32, 'rgba(255,255,255,.13)'); gr.addColorStop(0.36, 'rgba(255,255,255,.04)');
        gr.addColorStop(0.62, 'rgba(120,140,255,.05)'); gr.addColorStop(1, 'rgba(255,255,255,0)');
        g.fillStyle = gr; g.fill();
      } else if (kind === 'screen') {
        const acc = rgb(css('--accent'));
        const a = bil(q, 0, 0), b = bil(q, 1, 1);
        const gr = g.createLinearGradient(a[0], a[1], b[0], b[1]);
        gr.addColorStop(0, 'rgba(' + acc.join(',') + ',.32)'); gr.addColorStop(1, 'rgba(10,14,24,0)');
        g.fillStyle = gr; g.fill();
        g.strokeStyle = 'rgba(' + acc.join(',') + ',.95)'; g.lineWidth = 1.3;
        g.beginPath();
        for (let i = 0; i <= 60; i++) {
          const u = 0.08 + i / 60 * 0.84;
          const w = this.wave && this.wave.length ? this.wave[Math.floor(i / 60 * (this.wave.length - 1))] : Math.sin(i * 0.7 + t * 6);
          const p = bil(q, u, 0.66 + 0.14 * U.clamp(w, -1, 1));
          if (i) g.lineTo(p[0], p[1]); else g.moveTo(p[0], p[1]);
        }
        g.stroke();
        g.fillStyle = 'rgba(255,255,255,.7)';
        [[0.08, 0.14, 0.24], [0.08, 0.25, 0.38]].forEach((r) => {
          const p0 = bil(q, r[0], r[1]), p1 = bil(q, r[2], r[1] + 0.05);
          g.fillRect(Math.min(p0[0], p1[0]), Math.min(p0[1], p1[1]), Math.abs(p1[0] - p0[0]), Math.max(1, Math.abs(p1[1] - p0[1])));
        });
        const rc = bil(q, 0.8, 0.28), rad = Math.abs(bil(q, 0.9, 0.28)[0] - rc[0]);
        g.strokeStyle = 'rgb(' + this.ledCol(t).map(Math.round).join(',') + ')'; g.lineWidth = 2;
        g.beginPath(); g.arc(rc[0], rc[1], Math.max(2, rad), -Math.PI * 0.8, Math.PI * 0.4); g.stroke();
        const r0 = bil(q, 0.55, 0), r1 = bil(q, 0.95, 0), r2 = bil(q, 0.6, 1), r3 = bil(q, 0.2, 1);
        g.fillStyle = 'rgba(255,255,255,.05)'; g.beginPath(); g.moveTo(r0[0], r0[1]); g.lineTo(r1[0], r1[1]); g.lineTo(r2[0], r2[1]); g.lineTo(r3[0], r3[1]); g.fill();
      } else if (kind === 'pcb' || kind === 'coil') {
        g.strokeStyle = 'rgba(214,178,94,.55)'; g.lineWidth = 0.7;
        if (kind === 'coil') {
          for (let k = 1; k <= 5; k++) {
            const s = k * 0.08;
            const pts = [bil(q, s, s), bil(q, 1 - s, s), bil(q, 1 - s, 1 - s), bil(q, s, 1 - s)];
            g.beginPath(); pts.forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.closePath(); g.stroke();
          }
        } else {
          TRACES.forEach((tr) => { g.beginPath(); tr.forEach((uv, i) => { const p = bil(q, uv[0], uv[1]); if (i) g.lineTo(p[0], p[1]); else g.moveTo(p[0], p[1]); }); g.stroke(); });
          g.fillStyle = 'rgba(255,255,255,.35)';
          [[0.06, 0.08], [0.9, 0.85], [0.92, 0.1]].forEach((uv) => { const p = bil(q, uv[0], uv[1]); g.beginPath(); g.arc(p[0], p[1], 1.1, 0, Math.PI * 2); g.fill(); });
        }
      } else if (kind === 'lens') {
        let x = 0, y = 0; q.forEach((p) => { x += p[0]; y += p[1]; });
        const ctr = [x / q.length, y / q.length];
        let rad = 0; q.forEach((p) => { rad = Math.max(rad, Math.hypot(p[0] - ctr[0], p[1] - ctr[1])); });
        const gr = g.createRadialGradient(ctr[0] - rad * 0.3, ctr[1] - rad * 0.3, rad * 0.05, ctr[0], ctr[1], rad);
        gr.addColorStop(0, 'rgba(96,116,200,.6)'); gr.addColorStop(0.45, 'rgba(34,26,78,.45)'); gr.addColorStop(1, 'rgba(0,0,0,.6)');
        g.fillStyle = gr; g.fill();
        g.strokeStyle = 'rgba(160,170,200,.25)'; g.lineWidth = 1;
        g.beginPath(); g.arc(ctr[0], ctr[1], rad * 0.55, 0, Math.PI * 2); g.stroke();
        g.fillStyle = 'rgba(255,255,255,.85)'; g.beginPath(); g.ellipse(ctr[0] - rad * 0.35, ctr[1] - rad * 0.38, rad * 0.16, rad * 0.09, -0.6, 0, Math.PI * 2); g.fill();
        g.fillStyle = 'rgba(255,255,255,.35)'; g.beginPath(); g.arc(ctr[0] + rad * 0.3, ctr[1] + rad * 0.32, rad * 0.06, 0, Math.PI * 2); g.fill();
      } else if (kind === 'grille') {
        g.strokeStyle = dark ? 'rgba(0,0,0,.6)' : 'rgba(40,50,70,.3)'; g.lineWidth = 1.6; g.lineCap = 'round';
        for (let i = 0; i < 9; i++) { const v = 0.1 + i * 0.1; const a = bil(q, 0.15, v), b = bil(q, 0.85, v); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); }
      } else if (kind === 'portC' || kind === 'portDC') {
        g.fillStyle = 'rgba(0,0,0,.7)';
        const a = bil(q, 0.15, 0.3), b = bil(q, 0.85, 0.7);
        g.beginPath(); g.ellipse((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, Math.max(1, Math.abs(b[0] - a[0]) / 2), Math.max(1, Math.abs(b[1] - a[1]) / 2), 0, 0, Math.PI * 2); g.fill();
      } else if (kind === 'micHole') {
        g.fillStyle = 'rgba(0,0,0,.8)';
        for (let i = 0; i < 5; i++) { const p = bil(q, 0.15 + i * 0.175, 0.5); g.beginPath(); g.arc(p[0], p[1], 0.9, 0, Math.PI * 2); g.fill(); }
      } else if (kind === 'brand') {
        const a = bil(q, 0, 0.5), b = bil(q, 1, 0.5);
        g.fillStyle = 'rgba(255,255,255,.5)';
        g.font = '600 ' + Math.max(5, Math.hypot(b[0] - a[0], b[1] - a[1]) / 6).toFixed(1) + 'px Inter, sans-serif';
        g.textAlign = 'center'; g.textBaseline = 'middle';
        g.fillText('JIANWEI', (a[0] + b[0]) / 2, (a[1] + b[1]) / 2);
      }
      g.restore();
    }
  }
  window.Twin3D = { Twin, MODULES };
})();
