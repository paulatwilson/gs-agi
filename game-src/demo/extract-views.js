// Converts the approved mockup's sprite art (heroine, fish, rock, coins,
// water glints, ripples, splash drops) into viewc text scripts.
// Usage: node extract-views.js approved-mockup.html view/
const fs = require('fs');
const path = require('path');

const html = fs.readFileSync(process.argv[2], 'utf8');
const outDir = process.argv[3];
const cut = (from, to) => {
  const a = html.indexOf(from), b = html.indexOf(to, a);
  if (a < 0 || b < 0) throw new Error('marker not found: ' + from);
  return html.slice(a, b);
};
const code = 'var W=320,H=200,T=255,HORIZON=92,GROUND_BOTTOM=184,SW=24,SH=48;\n' +
  cut('/* ---------- rasterizers', '/* ---------- scene pieces') +
  cut('/* ---------- scene pieces', '/* ---------- game state') +
  cut('/* ---------- the heroine', 'var cv = document') +
  cut('function fishBase()', 'function drawPond') +
  '\nreturn {mk:mk, run:run, rock:rock, holeOps:holeOps, CELLS:CELLS, FISH:FISH, ROCK_HOME:ROCK_HOME, ROCK_Y:ROCK_Y, bbox:null};';
const M = new Function(code)();
const T = 255;

const toCel = (buf, w, h, ax, ay) => ({ buf, w, h, ax, ay });
function crop(buf, W, H, x0, y0, x1, y1, ax, ay) {
  const w = x1 - x0 + 1, h = y1 - y0 + 1, out = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) out[y * w + x] = buf[(y0 + y) * W + x0 + x];
  return toCel(out, w, h, ax - x0, ay - y0);
}
function bounds(buf, W, H) {
  let x0 = W, y0 = H, x1 = -1, y1 = -1;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (buf[y * W + x] !== T) { x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y); }
  return [x0, y0, x1, y1];
}
function celText(c) {
  const rows = [];
  for (let y = 0; y < c.h; y++) {
    let r = '';
    for (let x = 0; x < c.w; x++) { const v = c.buf[y * c.w + x]; r += v === T ? '.' : v.toString(16).toUpperCase(); }
    rows.push(r);
  }
  return 'cel ' + c.w + ' ' + c.h + ' ' + c.ax + ' ' + c.ay + '\n' + rows.join('\n');
}
function write(name, comment, header, loops) {
  const lines = ['; ' + comment + ' (generated from the approved mockup)'];
  if (header) lines.push(header);
  loops.forEach((cels, i) => { lines.push('loop ; ' + i); cels.forEach((c) => lines.push(celText(c))); });
  fs.writeFileSync(path.join(outDir, name), lines.join('\n') + '\n');
}
function mirror(c) {
  const b = new Uint8Array(c.buf.length);
  for (let y = 0; y < c.h; y++) for (let x = 0; x < c.w; x++) b[y * c.w + x] = c.buf[y * c.w + (c.w - 1 - x)];
  return toCel(b, c.w, c.h, c.w - 1 - c.ax, c.ay);
}
function fromBuf(b, w, h, ax, ay) { return toCel(Uint8Array.from(b), w, h, ax, ay); }

fs.mkdirSync(outDir, { recursive: true });

// 0: heroine. loops: 0 right, 1 left, 2 front, 3 back. cels: stand, left foot, stand, right foot
const hero = (d) => [0, 1, 0, 2].map((c) => fromBuf(M.CELLS[d][c], 24, 48, 12, 47));
write('view0.view', 'heroine, 24x48, four directions, scaled by depth', 'scaled 92 184 50', [hero('E'), hero('W'), hero('S'), hero('N')]);

// 1: the movable rock, with its ground shadow
{
  const b = M.mk(T), r = M.rock(M.ROCK_HOME, M.ROCK_Y, 0.9);
  M.run(b, r.shadow); M.run(b, r.vis);
  const [x0, y0, x1, y1] = bounds(b, 320, 200);
  write('view1.view', 'movable rock and its shadow', null, [[crop(b, 320, 200, x0, y0, x1, y1, M.ROCK_HOME, M.ROCK_Y)]]);
}

// 2: the leaping fish. loop 0 faces right, loop 1 left. cels: rising, level, falling
const fish = M.FISH.map((f) => fromBuf(f, 20, 20, 10, 10));
write('view2.view', 'fish, three poses, both directions', null, [fish, fish.map(mirror)]);

// 3: gold coins lying in the hole
{
  const b = M.mk(T), ops = M.holeOps(M.ROCK_HOME, M.ROCK_Y, true).slice(M.holeOps(M.ROCK_HOME, M.ROCK_Y, false).length);
  M.run(b, ops);
  const [x0, y0, x1, y1] = bounds(b, 320, 200);
  write('view3.view', 'five gold coins in the hole', null, [[crop(b, 320, 200, x0, y0, x1, y1, M.ROCK_HOME, M.ROCK_Y)]]);
}

// 4: water glint. cels: nothing, short, long, pale
{
  const line = (n, c) => toCel(new Uint8Array(n).fill(c), n, 1, 0, 0);
  write('view4.view', 'water glint', null, [[toCel(new Uint8Array([T]), 1, 1, 0, 0), line(2, 15), line(5, 15), line(2, 1)]]);
}

// 5: ripple rings, age 0 to 9
{
  const cels = [];
  for (let a = 0; a <= 9; a++) {
    const w = 23, h = 9, b = new Uint8Array(w * h).fill(T), rx = 2 + a, ry = Math.max(1, Math.round(rx / 3)), steps = rx * 6;
    for (let i = 0; i < steps; i++) {
      const th = i / steps * Math.PI * 2, px = 11 + Math.round(Math.cos(th) * rx), py = 4 + Math.round(Math.sin(th) * ry);
      if (px >= 0 && px < w && py >= 0 && py < h) b[py * w + px] = a < 5 ? 15 : 1;
    }
    cels.push(toCel(b, w, h, 11, 4));
  }
  write('view5.view', 'water ripple rings, nine ages', null, [cels]);
}

// 6: splash drops, ages 1 to 5
{
  const cels = [];
  for (let a = 1; a <= 5; a++) {
    const w = 25, h = 12, b = new Uint8Array(w * h).fill(T);
    for (let i = 0; i < 6; i++) {
      const vx = (i - 2.5) * 0.9, vy = -2.2 - (i % 2) * 0.8;
      const px = 12 + Math.round(vx * a), py = 10 + Math.round(vy * a + 0.25 * a * a);
      if (px >= 0 && px < w && py >= 0 && py < h) b[py * w + px] = 15;
    }
    cels.push(toCel(b, w, h, 12, 10));
  }
  write('view6.view', 'splash drops, five ages', null, [cels]);
}
// 7: the coin icon shown in message boxes (three stacked coins)
{
  const b = M.mk(T), ops = [];
  for (let k = 0; k < 3; k++) {
    const ox = 30 + (k === 1 ? 5 : k === 2 ? -4 : 0), oy = 30 - k * 5 + (k === 0 ? 4 : 0);
    ops.push(['c', 8], ['e', ox, oy + 2, 11, 4], ['e', ox, oy, 11, 4], ['c', 7], ['e', ox, oy, 9, 3], ['c', 15], ['e', ox - 2, oy - 1, 3, 1]);
  }
  M.run(b, ops);
  const [x0, y0, x1, y1] = bounds(b, 320, 200);
  write('view7.view', 'gold coins icon for message boxes', null, [[crop(b, 320, 200, x0, y0, x1, y1, 30, 30)]]);
}
console.log('wrote', fs.readdirSync(outDir).join(' '));
