/*
 * Drives dongleModel.js with the same script the firmware gets, so the two
 * traces can be diffed line for line. PLAN.md §5.5 makes the emulator the
 * executable reference the firmware has to match; this is what turns that
 * claim into a check rather than an assertion.
 *
 * Real timers, real dwell times: the model uses setTimeout/setInterval, and
 * driving it with fake timers would remove the very thing being compared for
 * the TEST sweeps.
 */
import { readFileSync } from 'node:fs';
import { DongleModel } from 'file:///C:/Users/schme/Desktop/Projects/wrsl-app/src/emulator/dongleModel.js';

const script = readFileSync(process.argv[2], 'utf8').split('\n').filter(Boolean);
const out = [];
const model = new DongleModel({ write: (line) => out.push(line) });
model.start();

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

for (const step of script) {
  const [line, dwell] = step.split('|');
  out.push(`> ${line}`);
  // setTestMode is the model's own entry point; the wire keyword routes there
  // through receiveLine like any other line, so nothing special is needed.
  model.receiveLine(line);
  await sleep(Number(dwell));
}

model.stop();
console.log(out.join('\n'));
