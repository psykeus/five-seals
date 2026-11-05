import fs from 'fs';
const jsnes = await import(process.env.JSNES + '/src/index.js');
const rom = fs.readFileSync(process.argv[2]).toString('binary');
let samples = [];
const nes = new jsnes.NES({ onFrame: () => {}, onAudioSample: (l, r) => samples.push(l), sampleRate: 44100 });
nes.loadROM(rom);
const B = jsnes.Controller;
function run(n, btn) { for (let i = 0; i < n; i++) { if (btn !== undefined) { if (i < 2) nes.buttonDown(1, btn); else nes.buttonUp(1, btn); } nes.frame(); } }
function rms(tag) { let s = 0; for (const v of samples) s += v * v; console.log(tag, 'rms', Math.sqrt(s / Math.max(1, samples.length)).toFixed(4), 'n', samples.length); samples = []; }
run(120); rms('title music');
run(60, B.BUTTON_START); run(60); rms('start+overworld');
// write a wav of 4 seconds of title for the user to hear if wanted
