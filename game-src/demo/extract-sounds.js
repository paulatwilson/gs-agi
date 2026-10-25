// Converts the approved mockup's sound note lists into sndc text scripts.
// Usage: node extract-sounds.js approved-mockup.html snd/
const fs = require('fs');
const path = require('path');

const html = fs.readFileSync(process.argv[2], 'utf8');
const outDir = process.argv[3];
const a = html.indexOf('var B = 48;'), b = html.indexOf('function sndBytes');
if (a < 0 || b < 0) throw new Error('sound markers not found');
const SND = new Function(html.slice(a, b) + '\nreturn SND;')();
const waves = ['square', 'triangle', 'saw', 'noise', 'sine'];
// SOUND.1 is the music, 2 the rock drag, 3 the fish splash (the numbers the logic scripts use)
const numbers = { music: 1, drag: 2, splash: 3 };

fs.mkdirSync(outDir, { recursive: true });
Object.keys(numbers).forEach((k) => {
  const d = SND[k], lines = ['; ' + d.name + ' (generated from the approved mockup)', 'kind ' + d.kind];
  d.tracks.forEach((t) => {
    lines.push('gen ' + waves[t.wave]);
    t.notes.forEach((n) => lines.push('note ' + n[0] + ' ' + n[1] + ' ' + n[2]));
  });
  fs.writeFileSync(path.join(outDir, 'sound' + numbers[k] + '.snd'), lines.join('\n') + '\n');
});
console.log('wrote', fs.readdirSync(outDir).join(' '));
