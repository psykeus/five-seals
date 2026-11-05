// Headless test runner: node test/run.js <rom> <script.json> <outdir>
// Script: array of steps. Each step: {"frames": n, "hold": ["right","a"], "shot": "name", "press": ["start"]}
//  - hold: buttons held for all n frames
//  - press: buttons pressed for the first 2 frames of the step only
//  - shot: save a PNG after the step
// Also supports {"save": "file"} / {"load": "file"} for battery RAM and {"peek": [addr,...]}.
import fs from 'fs';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const jsnes = await import(process.env.JSNES + '/src/index.js');
const { PNG } = require(process.env.PNGJS);

const [romPath, scriptPath, outDir] = process.argv.slice(2);
fs.mkdirSync(outDir, { recursive: true });
const script = JSON.parse(fs.readFileSync(scriptPath, 'utf8'));
const B = jsnes.Controller;
const map = { a: B.BUTTON_A, b: B.BUTTON_B, select: B.BUTTON_SELECT, start: B.BUTTON_START,
  up: B.BUTTON_UP, down: B.BUTTON_DOWN, left: B.BUTTON_LEFT, right: B.BUTTON_RIGHT };

let frame = null;
const nes = new jsnes.NES({
  onFrame: (buf) => { frame = buf; },
  onAudioSample: () => {},
  emulateSound: false,
});
nes.loadROM(fs.readFileSync(romPath).toString('binary'));

function shot(name) {
  const png = new PNG({ width: 256, height: 240 });
  for (let i = 0; i < 256 * 240; i++) {
    const c = frame[i];
    png.data[i * 4] = c & 0xff;
    png.data[i * 4 + 1] = (c >> 8) & 0xff;
    png.data[i * 4 + 2] = (c >> 16) & 0xff;
    png.data[i * 4 + 3] = 255;
  }
  fs.writeFileSync(path.join(outDir, name + '.png'), PNG.sync.write(png));
}

const syms = {};
const dbgPath = romPath.replace(/\.nes$/, '.dbg');
if (fs.existsSync(dbgPath)) {
  for (const line of fs.readFileSync(dbgPath, 'utf8').split('\n')) {
    const m = line.match(/^sym\t.*name="([^"]+)".*val=0x([0-9A-Fa-f]+)/);
    if (m) syms[m[1]] = parseInt(m[2], 16);
  }
}
const addr = (k) => (typeof k === 'number' ? k : (k.includes('+') ? syms[k.split('+')[0]] + parseInt(k.split('+')[1]) : syms[k]));
let total = 0;
for (const step of script) {
  if (step.load) {
    const ram = JSON.parse(fs.readFileSync(step.load));
    for (let i = 0; i < ram.length; i++) nes.cpu.mem[0x6000 + i] = ram[i];
  }
  if (step.poke) for (const [k, v] of Object.entries(step.poke)) nes.cpu.mem[addr(k)] = v;
  const n = step.frames || 0;
  for (let f = 0; f < n; f++) {
    const held = new Set([...(step.hold || []), ...(f < 2 ? step.press || [] : [])]);
    for (const [k, v] of Object.entries(map)) {
      if (held.has(k)) nes.buttonDown(1, v); else nes.buttonUp(1, v);
    }
    nes.frame();
    total++;
    if (nes.crashMessage) { console.log('CRASH', nes.crashMessage); process.exit(1); }
  }
  if (step.shot) shot(step.shot);
  if (step.peek) {
    console.log('frame', total, step.peek.map(a => a + '=' + nes.cpu.mem[addr(a)]).join(' '));
  }
  if (step.save) {
    fs.writeFileSync(step.save, JSON.stringify(Array.from(nes.cpu.mem.slice(0x6000, 0x6100))));
  }
}
console.log('ran', total, 'frames');
