/*
 * Normalises both traces onto the same shape so the diff shows semantic
 * differences rather than bookkeeping ones.
 *
 * seq is rebased on the first EVT seen. The firmware has been running since it
 * was flashed and is well past zero, while the model starts fresh every run;
 * an absolute comparison would report 32 differences that mean nothing. Both
 * sequences are contiguous, so a rebased comparison still catches a genuine
 * gap, duplicate or wrap error — which is the property actually under test.
 */
import { readFileSync } from 'node:fs';

const [file, kind] = process.argv.slice(2);
let base = null;
const out = [];

// .split('\n') leaves a trailing \r on every line of the CRLF capture, and JS
// `.` does not match \r — so a `(.*)$` anchor fails on every single line and
// the trace normalises to nothing. Strip it before matching.
for (const raw of readFileSync(file, 'utf8').replace(/\r/g, '').split('\n')) {
  let line;
  if (kind === 'fw') {
    if (/^---/.test(raw) || raw.trim() === '') continue;
    const m = raw.match(/^\s*\d+ ms\s+([<>.])\s?(.*)$/);
    if (!m) continue;
    if (m[1] === '.') continue;
    line = `${m[1]} ${m[2]}`;
  } else {
    if (raw.trim() === '') continue;
    line = raw.startsWith('> ') ? raw : `< ${raw}`;
  }

  const evt = line.match(/^< EVT (\S+) (\S+) (\S+) (\d+)$/);
  if (evt) {
    const seq = Number(evt[4]);
    if (base === null) base = seq;
    line = `< EVT ${evt[1]} ${evt[2]} ${evt[3]} #${seq - base}`;
  }
  out.push(line);
}
console.log(out.join('\n'));
