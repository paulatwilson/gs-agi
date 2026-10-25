// Converts the approved mockup's room definitions into PIC text scripts.
// Usage: node extract-mockup.js approved-mockup.html pic/
// The mockup's own drawing code is the source of truth for the art; this
// script only re-expresses it as picc input.
const fs = require('fs');
const path = require('path');

const html = fs.readFileSync(process.argv[2], 'utf8');
const outDir = process.argv[3];
const a = html.indexOf('/* ---------- scene pieces ---------- */');
const b = html.indexOf('/* ---------- game state ---------- */');
if (a < 0 || b < 0) throw new Error('mockup markers not found');

const code = 'var W=320,H=200,T=255,HORIZON=92,GROUND_BOTTOM=184;\n' +
  html.slice(a, b) + '\nreturn {ROOMS: ROOMS, tree: tree, rock: rock, holeOps: holeOps, ROCK_HOME: ROCK_HOME, ROCK_Y: ROCK_Y};';
const M = new Function(code)();

function ops(list, kind) {
  const out = [];
  list.forEach((o) => {
    const op = o[0], r = o.slice(1);
    if (op === 'c') out.push((kind || 'vis') + ' ' + r[0]);
    else if (op === 'p') out.push('poly ' + r.join(' '));
    else if (op === 'l') out.push('line ' + r.join(' '));
    else if (op === 'L') out.push('loop ' + r.join(' '));
    else if (op === 'e') out.push('ellipse ' + r.join(' '));
    else if (op === 'f') out.push('flood ' + r.join(' '));
    else if (op === 'd') out.push('dot ' + r.join(' '));
  });
  return out;
}

const MOVABLE = { 1: 3 };   // room 1, object 3 is the rock you can move: it is a view, not part of the picture

fs.mkdirSync(outDir, { recursive: true });
Object.keys(M.ROOMS).forEach((k) => {
  const R = M.ROOMS[k];
  const lines = ['; ' + R.name + ' (generated from the approved mockup)', '', '; backdrop', 'pri off', 'ctl off'];
  lines.push(...ops(R.bg), '');
  const objs = R.defs.map((d, i) => ({ i: i, o: (d[0] === 'tree' ? M.tree : M.rock)(d[1], d[2], d[3]) }))
    .filter((e) => MOVABLE[k] !== e.i);
  lines.push('; ground shadows');
  objs.forEach((e) => lines.push(...ops(e.o.shadow)));
  lines.push('', '; trees and rocks, back to front. pri is the baseline row: a walker above it is hidden');
  objs.slice().sort((x, y) => x.o.baseY - y.o.baseY).forEach((e) => {
    lines.push('; ' + R.names[e.i] + ' at ' + e.o.x + ',' + e.o.baseY, 'pri ' + e.o.baseY);
    lines.push(...ops(e.o.vis));
  });
  lines.push('pri off', '', '; control layer: 1 blocks walking, 2 is an exit', 'vis off');
  lines.push(...ops(R.ctl, 'ctl'));
  objs.forEach((e) => { lines.push('; ' + R.names[e.i] + ' footprint'); lines.push(...ops(e.o.ctl, 'ctl')); });
  fs.writeFileSync(path.join(outDir, 'pic' + k + '.pic'), lines.join('\n') + '\n');
});
// the hole the rock leaves behind: a small picture overlaid on room 1
fs.writeFileSync(path.join(outDir, 'pic3.pic'),
  ['; hole under the movable rock (overlay.pic on room 1)', 'pri off', 'ctl off', ...ops(M.holeOps(M.ROCK_HOME, M.ROCK_Y, false))].join('\n') + '\n');
console.log('wrote', fs.readdirSync(outDir).join(' '));
