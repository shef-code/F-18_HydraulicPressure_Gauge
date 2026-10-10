// Desktop preview: renders BOTH gauge styles to PNG with the needles in place.
//   - procedural: line-for-line port of GaugeStyleProcedural.h
//   - bitmap:     reads the generated assets_bitmap/*.h headers
// Usage: node tools/preview_gauges.js   (writes docs/preview/*.png)
const fs = require('fs'), path = require('path'), zlib = require('zlib');
const ROOT = path.join(__dirname, '..'), OUT = path.join(ROOT, 'docs', 'preview');
fs.mkdirSync(OUT, { recursive: true });

// ── minimal PNG writer ──────────────────────────────────────────────────────
const crcT = (() => { const t = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
const crc = b => { let c = 0xffffffff; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
function writePNG(file, w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3); }
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 2;
  fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

// ── RGB565 "sprite" ─────────────────────────────────────────────────────────
class Sprite {
  constructor(w, h) { this.w = w; this.h = h; this.px = new Uint16Array(w * h); }
  drawPixel(x, y, c) { if (x >= 0 && y >= 0 && x < this.w && y < this.h) this.px[y * this.w + x] = c; }
  readPixel(x, y) { return this.px[y * this.w + x]; }
  toRGB() { const o = Buffer.alloc(this.w * this.h * 3); for (let i = 0; i < this.px.length; i++) { const v = this.px[i]; const r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31; o[i * 3] = r << 3 | r >> 2; o[i * 3 + 1] = g << 2 | g >> 4; o[i * 3 + 2] = b << 3 | b >> 2; } return o; }
}
const TRANSPARENT = 0x0120;

// ── glyph atlas (parsed from GaugeGlyphs.h) ─────────────────────────────────
function loadFonts() {
  const src = fs.readFileSync(path.join(ROOT, 'GaugeGlyphs.h'), 'utf8'), fonts = {};
  for (const name of ['Numerals', 'Legend', 'Small']) {
    const gs = src.match(new RegExp('glyphs' + name + '\\[\\] = \\{([\\s\\S]*?)\\};'))[1];
    const glyphs = [...gs.matchAll(/\{ '(.)', (\d+), (\d+), (-?\d+), (-?\d+), (\d+), (\d+) \}/g)].map(m => ({ c: m[1], w: +m[2], h: +m[3], xo: +m[4], yo: +m[5], adv: +m[6], off: +m[7] }));
    const ds = src.match(new RegExp('glyphData' + name + '\\[\\] = \\{([\\s\\S]*?)\\};'))[1];
    const data = ds.split(',').map(s => s.trim()).filter(s => s.length).map(Number);
    const capH = +src.match(new RegExp('font' + name + ' = \\{[^,]*, \\d+, (\\d+),'))[1];
    fonts[name] = { glyphs, data, capH };
  }
  return fonts;
}
const F = loadFonts();

// ── procedural style (port of GaugeStyleProcedural.h) ───────────────────────
const CX = 120, CY = 120, A0 = 80, DEG_PER_K = 63, MINOR_DEG = 12.6, LAST_TICK = 25, BAND0 = 240, BAND1 = 287;
const D2R = Math.PI / 180, R2D = 180 / Math.PI;
const PAL_DAY = { tint: [1.00, 1.00, 1.02], hubLo: 60, hubHi: 200, mark: [238, 234, 222], dialC: [34, 34, 36], dialE: [8, 8, 9], glass: 1.0,
  ptrA: [255, 255, 252], ptrB: [168, 168, 164], boxA: [52, 52, 54], boxB: [18, 18, 19], txt: [240, 240, 232], edge: [120, 120, 118] };
const PAL_NVG = { tint: [0.55, 0.62, 0.55], hubLo: 16, hubHi: 90, mark: [90, 255, 100], dialC: [10, 16, 10], dialE: [3, 6, 3], glass: 0.35,
  ptrA: [40, 255, 70], ptrB: [10, 150, 30], boxA: [0, 28, 4], boxB: [0, 12, 2], txt: [120, 255, 140], edge: [40, 220, 60] };

const clamp01 = v => v < 0 ? 0 : v > 1 ? 1 : v;
const cov = d => clamp01(d + 0.5);
const lerpf = (a, b, t) => a + (b - a) * t;
const mix = (c, to, t) => { c[0] = lerpf(c[0], to[0], t); c[1] = lerpf(c[1], to[1], t); c[2] = lerpf(c[2], to[2], t); };
const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
function pack565(c, x, y) {
  const d = BAYER[(y & 3) * 4 + (x & 3)] - 7.5;
  const cl = v => v < 0 ? 0 : v > 255 ? 255 : v;
  const r = cl(Math.trunc(c[0] + d * 0.5)), g = cl(Math.trunc(c[1] + d * 0.25)), b = cl(Math.trunc(c[2] + d * 0.5));
  const v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  return v === TRANSPARENT ? v + 1 : v;
}
function metal(th, lightDeg, lo, hi) {
  const d = Math.cos((th - lightDeg) * D2R), v = Math.pow(0.5 + 0.5 * d, 1.6), spec = d > 0 ? Math.pow(d, 24) * 0.35 : 0;
  return lo + (hi - lo) * v + 255 * spec;
}

function blendPx(s, x, y, col, a) {
  if (a <= 0 || x < 0 || y < 0 || x >= s.w || y >= s.h) return;
  const p = s.readPixel(x, y);
  const c = [Math.trunc(((p >> 11) & 31) * 255 / 31), Math.trunc(((p >> 5) & 63) * 255 / 63), Math.trunc((p & 31) * 255 / 31)];
  mix(c, col, a); s.drawPixel(x, y, pack565(c, x, y));
}
function drawText(s, f, str, cx, cy, col) {
  let total = 0; for (const ch of str) { const g = f.glyphs.find(g => g.c === ch); if (g) total += g.adv; }
  let x = Math.floor(cx - total * 0.5 + 0.5); const base = Math.floor(cy + f.capH * 0.5 + 0.5);
  for (const ch of str) {
    const g = f.glyphs.find(g => g.c === ch); if (!g) continue;
    for (let j = 0; j < g.h; j++) for (let i = 0; i < g.w; i++) blendPx(s, x + g.xo + i, base + g.yo + j, col, f.data[g.off + j * g.w + i] / 255);
    x += g.adv;
  }
}

function shadeFacePixel(px, py, P) {
  const dx = px + 0.5 - CX, dy = py + 0.5 - CY, r = Math.sqrt(dx * dx + dy * dy);
  if (r > 120.5) return [0, 0, 0];
  let th = 0;
  if (r >= 84 || r <= 16.5) { th = Math.atan2(dx, -dy) * R2D; if (th < 0) th += 360; }
  const c = [0, 0, 0];
  const hx = px + 0.5 - (CX - 22), hy = py + 0.5 - (CY - 26), t = clamp01(Math.sqrt(hx * hx + hy * hy) / 140);
  for (let i = 0; i < 3; i++) c[i] = lerpf(P.dialC[i], P.dialE[i], t);
  if (r > 106) { const f = 0.45 * clamp01((r - 106) / 14); c[0] *= 1 - f; c[1] *= 1 - f; c[2] *= 1 - f; }
  if (r >= 90 && th >= BAND0 - 2 && th <= BAND1 + 2) {
    const a = Math.min(th - BAND0, BAND1 - th) * D2R * r;
    mix(c, P.mark, cov(a) * cov(Math.min(r - 91, 108.5 - r)));
  }
  if (r >= 84) {
    let trel = th - A0; if (trel < 0) trel += 360;
    const idx = Math.floor(trel / MINOR_DEG + 0.5);
    if (idx <= LAST_TICK) {
      const major = idx % 5 === 0, dist = Math.abs(trel - idx * MINOR_DEG) * D2R * r, w = major ? 3.5 : 2.0, r0 = major ? 86 : 98;
      mix(c, P.mark, cov(w * 0.5 - dist) * cov(Math.min(r - r0, 109 - r)));
    }
  }
  if (r <= 16.5) {
    mix(c, [4, 4, 4], cov(13.6 - r));
    const k = metal(th, 315, P.hubLo, P.hubHi);
    mix(c, [k * P.tint[0], k * P.tint[1], k * P.tint[2]], cov(Math.min(r - 13.6, 15.6 - r)));
  }
  const clip = cov(120 - r);
  if (clip > 0) {
    const x1 = dx, y1 = dy, r1 = Math.sqrt(x1 * x1 + y1 * y1);
    const x2 = x1 - 18, y2 = y1 - 22, r2 = Math.sqrt(x2 * x2 + y2 * y2);
    const cres = cov(112 - r1) * (1 - cov(112 - r2));
    const gx = x1 + 100, gy = y1 + 100, tg = clamp01((gx * 100 + gy * 100) / (100 * 100 * 2));
    const a1 = cres * (44 * P.glass / 255) * (1 - tg);
    const s = [[0, 0], [44, -120], [88, -82]]; let dmin = 1e9;
    for (let e = 0; e < 3; e++) {
      const [ax, ay] = s[e], [bx, by] = s[(e + 1) % 3], ex = bx - ax, ey = by - ay, len = Math.sqrt(ex * ex + ey * ey);
      const dd = (ex * (y1 - ay) - ey * (x1 - ax)) / len; if (dd < dmin) dmin = dd;
    }
    const ts = clamp01(((x1 - 66) * -55 + (y1 + 100) * 78) / (55 * 55 + 78 * 78));
    const a2 = cov(dmin) * (12 * P.glass / 255) * (1 - ts);
    mix(c, [255, 255, 255], clip * (a1 + a2 - a1 * a2));
  }
  return c;
}
function buildFace(s, P) {
  for (let y = 0; y < 240; y++) for (let x = 0; x < 240; x++) s.drawPixel(x, y, pack565(shadeFacePixel(x, y, P), x, y));
  for (let k = 0; k <= 5; k++) { const th = (A0 + k * DEG_PER_K) * D2R; let nx = CX + 74 * Math.sin(th), ny = CY - 74 * Math.cos(th); if (k === 1) { nx = 163; ny = 191; } if (k === 2) { nx = 96; ny = 190; } drawText(s, F.Numerals, String(k), nx, ny, P.mark); }
  drawText(s, F.Legend, 'HYD PRESS', 121, 81.5, P.mark);
  drawText(s, F.Small, 'PSI X 1000', 128, 164.5, P.mark);
}
function buildNeedle(s, num, P) {
  for (let py = 0; py < 120; py++) for (let px = 0; px < 33; px++) {
    const x = px - 16, y = py - 103, ax = Math.abs(x), col = [10, 10, 11]; let alpha = 0;
    if (y > -104.5 && y < -14.5) {
      const hw = Math.min(9.5, 0.5 + 0.4 * (y + 103.5));
      alpha = cov(hw - ax) * cov(y + 103.5) * cov(-15.5 - y);
      if (alpha > 0) {
        const tp = clamp01(ax / hw), tb = ax / 9.5;
        const pc = [0, 1, 2].map(i => lerpf(P.ptrA[i], P.ptrB[i], tp)), bc = [0, 1, 2].map(i => lerpf(P.boxA[i], P.boxB[i], tb));
        const pw = cov(-49 - y);
        mix(bc, P.edge, (1 - pw) * clamp01(0.85 - Math.abs(ax - 9.5) * 1.2));
        for (let i = 0; i < 3; i++) col[i] = lerpf(10, lerpf(bc[i], pc[i], pw), alpha);
      }
    }
    s.drawPixel(px, py, alpha < 0.35 ? TRANSPARENT : pack565(col, px, py));
  }
  drawText(s, F.Numerals, String(num), 16.5, 103.5 - 32, P.txt);
}

// ── bitmap style: load generated headers ────────────────────────────────────
function loadHeader(name) {
  const s = fs.readFileSync(path.join(ROOT, 'assets_bitmap', name), 'utf8');
  return (s.slice(s.indexOf('{')).match(/0x[0-9a-fA-F]{4}/g) || []).map(h => parseInt(h, 16));
}
function spriteFrom(arr, w, h) { const s = new Sprite(w, h); s.px.set(arr); return s; }

// ── compositing: face + shadow + rotated needles (mirrors the sketch) ───────
function pushRotated(dst, src, pivSx, pivSy, pivDx, pivDy, angDeg) {
  const a = angDeg * D2R, ca = Math.cos(a), sa = Math.sin(a);
  for (let y = 0; y < dst.h; y++) for (let x = 0; x < dst.w; x++) {
    const dx = x - pivDx, dy = y - pivDy;                // dest offset from pivot; rotate by -angle into source
    const sx = Math.round(pivSx + dx * ca + dy * sa), sy = Math.round(pivSy - dx * sa + dy * ca);
    if (sx < 0 || sy < 0 || sx >= src.w || sy >= src.h) continue;
    const v = src.px[sy * src.w + sx]; if (v !== TRANSPARENT) dst.px[y * dst.w + x] = v;
  }
}
function shadowOf(n) { const s = new Sprite(n.w, n.h); for (let i = 0; i < n.px.length; i++) s.px[i] = n.px[i] === TRANSPARENT ? TRANSPARENT : 0x0000; return s; }
const mapHyd = psiK => A0 + psiK * DEG_PER_K;   // angle for a reading in 1000 psi (matches map_hyd range -280..40)
function compose(face, n1, n2, psi1, psi2) {
  const dst = new Sprite(240, 240); dst.px.set(face.px);
  const sh = shadowOf(n1);
  for (const [n, p] of [[n1, psi1], [n2, psi2]]) {
    const ang = (A0 + p * DEG_PER_K);
    pushRotated(dst, sh, 16, 103, 120 + 3, 120 + 4, ang);
    pushRotated(dst, n, 16, 103, 120, 120, ang);
  }
  return dst;
}

// ── main ────────────────────────────────────────────────────────────────────
const results = {};
for (const [tag, P, bmpTag] of [['Day', PAL_DAY, 'Day'], ['Green', PAL_NVG, 'Green']]) {
  const face = new Sprite(240, 240), n1 = new Sprite(33, 120), n2 = new Sprite(33, 120);
  buildFace(face, P); buildNeedle(n1, 1, P); buildNeedle(n2, 2, P);
  const bface = spriteFrom(loadHeader(`face${bmpTag}.h`), 240, 240);
  const bn1 = spriteFrom(loadHeader(`needle1${bmpTag}.h`), 33, 120), bn2 = spriteFrom(loadHeader(`needle2${bmpTag}.h`), 33, 120);
  const pFaceOnly = face;
  writePNG(path.join(OUT, `proc_face${tag}.png`), 240, 240, face.toRGB());
  const pa = compose(face, n1, n2, 4.2, 0.5), ba = compose(bface, bn1, bn2, 4.2, 0.5);
  writePNG(path.join(OUT, `proc_gauge${tag}.png`), 240, 240, pa.toRGB());
  writePNG(path.join(OUT, `bitmap_gauge${tag}.png`), 240, 240, ba.toRGB());
  results[tag] = { pa, ba };
}
// side-by-side comparison sheet: [procedural day | bitmap day | procedural nvg | bitmap nvg], shown 2x
const order = [results.Day.pa, results.Day.ba, results.Green.pa, results.Green.ba], Z = 2, GAP = 8;
const W = order.length * 240 * Z + (order.length - 1) * GAP, H = 240 * Z;
const sheet = Buffer.alloc(W * H * 3, 24);
order.forEach((spr, k) => { const rgb = spr.toRGB(), x0 = k * (240 * Z + GAP);
  for (let y = 0; y < H; y++) for (let x = 0; x < 240 * Z; x++) { const si = ((y / Z | 0) * 240 + (x / Z | 0)) * 3, di = (y * W + x0 + x) * 3; sheet[di] = rgb[si]; sheet[di + 1] = rgb[si + 1]; sheet[di + 2] = rgb[si + 2]; } });
writePNG(path.join(OUT, 'compare.png'), W, H, sheet);
console.log('wrote previews to', OUT);
