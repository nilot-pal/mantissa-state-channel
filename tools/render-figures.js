// Renders the figures in the README from the data written by the figures
// target. No dependencies, no plotting library, plain SVG.
//
//   cmake --build build --target figures
//   ./build/Release/figures
//   node tools/render-figures.js
//
// Colours are chosen to stay legible on a light and a dark page, because a
// README is read on both and an image cannot ask which one it is in.

const fs = require('fs');
const path = require('path');

const DATA = path.join(__dirname, '..', 'docs', 'data');
const OUT = path.join(__dirname, '..', 'docs');

const INK = '#8b949e';      // axes, ticks, secondary text
const TEXT = '#767e88';     // body text
const CARRIER = '#3fb950';  // the bits the diameter keeps
const EXPONENT = '#4c8dff'; // sign and exponent
const FLAG = '#f0883e';     // the flag bit
const PAYLOAD = '#a371f7';  // the payload
const BOUND = '#f85149';    // the error bound

function readCsv(name) {
    const text = fs.readFileSync(path.join(DATA, name), 'utf8').trim();
    const lines = text.split(/\r?\n/);
    const header = lines[0].split(',');
    return lines.slice(1).map(line => {
        const cells = line.split(',');
        const row = {};
        header.forEach((h, i) => { row[h] = cells[i]; });
        return row;
    });
}

function readPairs(name) {
    const rows = readCsv(name);
    const out = {};
    rows.forEach(r => { out[r.field] = r.value; });
    return out;
}

function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function svg(width, height, body) {
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}" ` +
           `width="${width}" height="${height}" font-family="ui-sans-serif, -apple-system, ` +
           `Segoe UI, Helvetica, Arial, sans-serif">\n${body}\n</svg>\n`;
}

function text(x, y, s, opts = {}) {
    const anchor = opts.anchor || 'start';
    const size = opts.size || 12;
    const fill = opts.fill || TEXT;
    const weight = opts.weight || 'normal';
    const family = opts.mono ? ' font-family="ui-monospace, SFMono-Regular, Consolas, monospace"' : '';
    return `<text x="${x}" y="${y}" font-size="${size}" fill="${fill}" ` +
           `text-anchor="${anchor}" font-weight="${weight}"${family}>${esc(s)}</text>`;
}

// ---------------------------------------------------------------- figure one

function renderBitLayout() {
    const d = readPairs('bit-layout.csv');
    const before = d.before_bits;
    const after = d.after_bits;

    const cell = 23, x0 = 20, rowH = 34;
    const width = x0 * 2 + cell * 32;
    const height = 310;
    const parts = [];

    // Which region each bit index belongs to. Index 0 is the sign bit.
    function region(i) {
        if (i === 0) return { fill: EXPONENT, name: 'sign' };
        if (i <= 8) return { fill: EXPONENT, name: 'exponent' };
        if (i <= 16) return { fill: CARRIER, name: 'carrier' };
        if (i === 17) return { fill: FLAG, name: 'flag' };
        return { fill: PAYLOAD, name: 'payload' };
    }

    parts.push(text(x0, 24, 'One 100 micrometre grain, before and after the state goes in',
                    { size: 14, weight: '600' }));

    [[before, 'as the solver stored it', 60], [after, 'with the state packed in', 60 + rowH + 26]]
        .forEach(([bits, label, y]) => {
            parts.push(text(x0, y - 8, label, { size: 11, fill: INK }));
            for (let i = 0; i < 32; ++i) {
                const r = region(i);
                const changed = before[i] !== after[i];
                const x = x0 + i * cell;
                const highlight = (bits === after && changed);
                parts.push(`<rect x="${x}" y="${y}" width="${cell - 2}" height="${rowH - 4}" ` +
                           `rx="2" fill="${r.fill}" fill-opacity="${highlight ? 0.38 : 0.16}" ` +
                           `stroke="${r.fill}" stroke-opacity="${highlight ? 0.9 : 0.35}"/>`);
                parts.push(text(x + (cell - 2) / 2, y + rowH / 2 + 1, bits[i],
                                { anchor: 'middle', size: 11, mono: true, fill: r.fill }));
            }
        });

    // Region brackets underneath.
    const bracketY = 60 + 2 * rowH + 30;
    const spans = [
        [0, 1, 'sign', EXPONENT],
        [1, 9, 'exponent', EXPONENT],
        [9, 17, 'diameter keeps 8 bits', CARRIER],
        [17, 18, 'flag', FLAG],
        [18, 32, 'payload, 14 bits', PAYLOAD],
    ];
    spans.forEach(([a, b, label, colour]) => {
        const xa = x0 + a * cell, xb = x0 + b * cell - 2;
        parts.push(`<path d="M${xa} ${bracketY} L${xa} ${bracketY + 5} L${xb} ${bracketY + 5} ` +
                   `L${xb} ${bracketY}" fill="none" stroke="${colour}" stroke-opacity="0.6"/>`);
        parts.push(text((xa + xb) / 2, bracketY + 19, label,
                        { anchor: 'middle', size: 10.5, fill: colour }));
    });

    // The three numbers that matter, stated plainly.
    const lines = [
        ['true diameter', `${Number(d.true_diameter_um).toFixed(3)} um`, INK],
        ['read back by code that knows nothing about any of this',
         `${Number(d.read_diameter_um).toFixed(3)} um, out by ${Number(d.diameter_error_um).toFixed(3)} um`,
         CARRIER],
        ['state carried through, 12.500 goes in',
         `${Number(d.decoded_state).toFixed(3)} comes out, out by ${Number(d.state_error_percent).toFixed(3)} per cent`,
         PAYLOAD],
    ];
    lines.forEach(([label, value, colour], i) => {
        const y = bracketY + 48 + i * 20;
        parts.push(text(x0, y, label, { size: 11.5, fill: TEXT }));
        parts.push(text(width - x0, y, value, { size: 11.5, fill: colour, anchor: 'end', mono: true }));
    });

    fs.writeFileSync(path.join(OUT, 'bit-layout.svg'), svg(width, height, parts.join('\n')));
}

// ---------------------------------------------------------------- figure two

function renderCarrierError() {
    const rows = readCsv('carrier-error.csv').map(r => ({
        d: Number(r.diameter_um),
        realised: Number(r.realised_percent),
        lo: Number(r.envelope_low_percent),
        hi: Number(r.envelope_high_percent),
    }));
    const env = readCsv('carrier-envelope.csv').map(r => ({
        d: Number(r.diameter_um), worst: Number(r.worst_percent),
    }));

    const width = 900, height = 380;
    const top = 70, bottom = 52;
    const h = height - top - bottom;
    const panelW = 356;
    const leftX = 62, rightX = 62 + panelW + 104;

    const parts = [];
    parts.push(text(leftX, 24, 'What every other routine in the solver reads',
                    { size: 14, weight: '600' }));
    parts.push(text(leftX, 41,
                    'Left: the error on one grain, zoomed in until the pattern shows. ' +
                    'Band is every possible state, line is one particular state.',
                    { size: 11, fill: INK }));
    parts.push(text(leftX, 56,
                    'Right: the worst a grain of each size can suffer, which resets at every ' +
                    'power of two and is why 100 um is not the 0.391 per cent headline.',
                    { size: 11, fill: INK }));

    // ---- left panel, the shape
    const xMin = rows[0].d, xMax = rows[rows.length - 1].d;
    const yMin = -0.25, yMax = 0.45;
    const X = v => leftX + (v - xMin) / (xMax - xMin) * panelW;
    const Y = v => top + (yMax - v) / (yMax - yMin) * h;

    for (let v = -0.2; v <= 0.4001; v += 0.1) {
        const y = Y(v);
        parts.push(`<line x1="${leftX}" y1="${y}" x2="${leftX + panelW}" y2="${y}" ` +
                   `stroke="${INK}" stroke-opacity="0.15"/>`);
        parts.push(text(leftX - 8, y + 4, v.toFixed(1), { anchor: 'end', size: 10.5, fill: INK }));
    }

    const up = rows.map(r => `${X(r.d).toFixed(1)},${Y(r.hi).toFixed(1)}`).join(' L');
    const down = rows.slice().reverse().map(r => `${X(r.d).toFixed(1)},${Y(r.lo).toFixed(1)}`).join(' L');
    parts.push(`<path d="M${up} L${down} Z" fill="${CARRIER}" fill-opacity="0.20"/>`);
    const line = rows.map(r => `${X(r.d).toFixed(1)},${Y(r.realised).toFixed(1)}`).join(' L');
    parts.push(`<path d="M${line}" fill="none" stroke="${CARRIER}" stroke-width="1.3"/>`);

    [[0.390613, 'bound, +0.391'], [-0.195301, 'bound, -0.195']].forEach(([v, label]) => {
        parts.push(`<line x1="${leftX}" y1="${Y(v)}" x2="${leftX + panelW}" y2="${Y(v)}" ` +
                   `stroke="${BOUND}" stroke-width="1.1" stroke-dasharray="5 3"/>`);
        parts.push(text(leftX + panelW - 3, Y(v) - 5, label, { anchor: 'end', size: 10, fill: BOUND }));
    });

    // The worst this particular size can reach, which is the number the README
    // quotes and the point the right hand panel makes.
    const localWorst = Math.max(...rows.map(r => r.hi));
    parts.push(`<line x1="${leftX}" y1="${Y(localWorst)}" x2="${leftX + panelW}" ` +
               `y2="${Y(localWorst)}" stroke="${FLAG}" stroke-width="1.1" stroke-dasharray="3 3"/>`);
    parts.push(text(leftX + panelW - 3, Y(localWorst) - 5,
                    `worst at this size, ${localWorst.toFixed(3)}`,
                    { anchor: 'end', size: 10, fill: FLAG }));

    parts.push(`<line x1="${leftX}" y1="${top + h}" x2="${leftX + panelW}" y2="${top + h}" ` +
               `stroke="${INK}" stroke-opacity="0.5"/>`);
    [100.0, 100.3, 100.6, 100.9, 101.2].forEach(v => {
        parts.push(text(X(v), top + h + 18, v.toFixed(1), { anchor: 'middle', size: 10.5, fill: INK }));
    });
    parts.push(text(leftX + panelW / 2, height - 12, 'grain diameter, micrometres',
                    { anchor: 'middle', size: 11, fill: INK }));
    parts.push(text(leftX + 6, top + 14, 'one period is 0.24 um', { size: 10, fill: INK }));

    // ---- right panel, the worst case across the size range
    const eMin = 40, eMax = 400;
    const EX = v => rightX + (Math.log10(v) - Math.log10(eMin)) /
                             (Math.log10(eMax) - Math.log10(eMin)) * panelW;
    const EY = v => top + (0.45 - v) / (0.45 - 0.0) * h;

    for (let v = 0.0; v <= 0.4001; v += 0.1) {
        const y = EY(v);
        parts.push(`<line x1="${rightX}" y1="${y}" x2="${rightX + panelW}" y2="${y}" ` +
                   `stroke="${INK}" stroke-opacity="0.15"/>`);
        parts.push(text(rightX - 8, y + 4, v.toFixed(1), { anchor: 'end', size: 10.5, fill: INK }));
    }
    parts.push(`<line x1="${rightX}" y1="${EY(0.390613)}" x2="${rightX + panelW}" ` +
               `y2="${EY(0.390613)}" stroke="${BOUND}" stroke-width="1.1" stroke-dasharray="5 3"/>`);
    parts.push(text(rightX + panelW - 3, EY(0.390613) - 5, 'bound, 0.391',
                    { anchor: 'end', size: 10, fill: BOUND }));

    // Break the curve at each reset so the jump is not drawn as a line.
    let segment = [];
    const segments = [];
    for (let i = 0; i < env.length; ++i) {
        if (i > 0 && env[i].worst > env[i - 1].worst + 0.05) { segments.push(segment); segment = []; }
        segment.push(env[i]);
    }
    segments.push(segment);
    segments.forEach(seg => {
        if (seg.length < 2) return;
        const d = seg.map(r => `${EX(r.d).toFixed(1)},${EY(r.worst).toFixed(1)}`).join(' L');
        parts.push(`<path d="M${d}" fill="none" stroke="${CARRIER}" stroke-width="1.6"/>`);
    });

    // The grain the README quotes.
    const marker = env.reduce((a, b) => Math.abs(b.d - 100) < Math.abs(a.d - 100) ? b : a);
    parts.push(`<circle cx="${EX(marker.d)}" cy="${EY(marker.worst)}" r="3.5" fill="${FLAG}"/>`);
    parts.push(text(EX(marker.d) + 8, EY(marker.worst) + 4,
                    `100 um: ${marker.worst.toFixed(3)} per cent, 0.24 um`,
                    { size: 10.5, fill: FLAG }));

    parts.push(`<line x1="${rightX}" y1="${top + h}" x2="${rightX + panelW}" y2="${top + h}" ` +
               `stroke="${INK}" stroke-opacity="0.5"/>`);
    [40, 60, 100, 200, 400].forEach(v => {
        parts.push(text(EX(v), top + h + 18, String(v), { anchor: 'middle', size: 10.5, fill: INK }));
    });
    parts.push(text(rightX + panelW / 2, height - 12, 'grain diameter, micrometres, log scale',
                    { anchor: 'middle', size: 11, fill: INK }));

    parts.push(text(14, top + h / 2, 'error, per cent', { anchor: 'middle', size: 11, fill: INK })
               .replace('<text ', `<text transform="rotate(-90 14 ${top + h / 2})" `));

    fs.writeFileSync(path.join(OUT, 'carrier-error.svg'), svg(width, height, parts.join('\n')));
}

// -------------------------------------------------------------- figure three

function renderStateRoundTrip() {
    const res = readCsv('state-resolution.csv').map(r => ({
        s: Number(r.state),
        log: Number(r.log_worst_percent),
        lin: Number(r.linear_worst_percent),
    }));
    const zoom = readCsv('state-zoom.csv').map(r => ({
        s: Number(r.true_state), d: Number(r.decoded_state),
    }));

    const width = 900, height = 340;
    const m = { top: 54, bottom: 52 };
    const h = height - m.top - m.bottom;
    const panelW = 356;
    const leftX = 62, rightX = 62 + panelW + 96;

    const parts = [];
    parts.push(text(leftX, 24, 'What comes back out', { size: 14, weight: '600' }));
    parts.push(text(leftX, 41,
                    'Left: why the payload is log quantised, both schemes having the same ' +
                    '16384 codes. Right: the steps, zoomed in far enough to see them.',
                    { size: 11, fill: INK }));

    // Panel A: worst relative error against state, log on both axes.
    const ax = v => leftX + (Math.log10(v) + 3) / 6 * panelW;
    const eHi = 4, eLo = -3;   // decades of error, per cent
    const ay = v => m.top + (eHi - Math.log10(Math.max(v, 1e-12))) / (eHi - eLo) * h;

    for (let e = eLo; e <= eHi; ++e) {
        const y = ay(Math.pow(10, e));
        parts.push(`<line x1="${leftX}" y1="${y}" x2="${leftX + panelW}" y2="${y}" ` +
                   `stroke="${INK}" stroke-opacity="0.13"/>`);
        parts.push(text(leftX - 8, y + 4, `1e${e}`, { anchor: 'end', size: 10, fill: INK }));
    }

    const linLine = res.map(r => `${ax(r.s).toFixed(1)},${ay(r.lin).toFixed(1)}`).join(' L');
    parts.push(`<path d="M${linLine}" fill="none" stroke="${BOUND}" stroke-width="1.6"/>`);
    const logLine = res.map(r => `${ax(r.s).toFixed(1)},${ay(r.log).toFixed(1)}`).join(' L');
    parts.push(`<path d="M${logLine}" fill="none" stroke="${PAYLOAD}" stroke-width="1.6"/>`);

    parts.push(text(leftX + 6, ay(res[0].lin) - 8, 'linear, same 16384 codes',
                    { size: 10.5, fill: BOUND }));
    parts.push(text(leftX + 6, ay(res[0].log) - 8, 'log, flat at 0.042 per cent',
                    { size: 10.5, fill: PAYLOAD }));

    for (let e = -3; e <= 3; ++e) {
        const v = Math.pow(10, e);
        parts.push(text(ax(v), m.top + h + 18, `1e${e}`, { anchor: 'middle', size: 10.5, fill: INK }));
    }
    parts.push(text(leftX + panelW / 2, height - 12,
                    'state carried, against worst error in per cent',
                    { anchor: 'middle', size: 11, fill: INK }));

    // Panel B: decoded against true, zoomed until the steps show.
    const zMin = Math.min(...zoom.map(r => r.s)), zMax = Math.max(...zoom.map(r => r.s));
    const bx = v => rightX + (v - zMin) / (zMax - zMin) * panelW;
    const by = v => m.top + (zMax - v) / (zMax - zMin) * h;

    parts.push(`<path d="M${bx(zMin)},${by(zMin)} L${bx(zMax)},${by(zMax)}" fill="none" ` +
               `stroke="${INK}" stroke-opacity="0.5" stroke-dasharray="4 3"/>`);
    parts.push(text(bx(zMax) - 4, by(zMax) + 14, 'exact', { anchor: 'end', size: 10, fill: INK }));

    const bLine = zoom.map(r => `${bx(r.s).toFixed(1)},${by(r.d).toFixed(1)}`).join(' L');
    parts.push(`<path d="M${bLine}" fill="none" stroke="${PAYLOAD}" stroke-width="1.4"/>`);

    [zMin, (zMin + zMax) / 2, zMax].forEach(v => {
        parts.push(text(bx(v), m.top + h + 18, v.toFixed(3),
                        { anchor: 'middle', size: 10.5, fill: INK }));
    });
    parts.push(text(rightX + panelW / 2, height - 12, 'state in, state out',
                    { anchor: 'middle', size: 11, fill: INK }));

    fs.writeFileSync(path.join(OUT, 'state-roundtrip.svg'), svg(width, height, parts.join('\n')));
}

// --------------------------------------------------------------- figure four

function renderDesignSpace() {
    const rows = readCsv('design-space.csv').map(r => ({
        bits: Number(r.field_bits),
        carrier: Number(r.carrier_percent),
        state: Number(r.state_flag_percent),
        guarded: Number(r.state_guarded_percent),
        states: Number(r.states_flag),
        guardedStates: Number(r.states_guarded),
    }));

    const width = 900, height = 380;
    const m = { top: 82, right: 210, bottom: 52, left: 62 };
    const w = width - m.left - m.right, h = height - m.top - m.bottom;

    const bLo = 8, bHi = 20, eHi = 2, eLo = -4;
    const X = v => m.left + (v - bLo) / (bHi - bLo) * w;
    const Y = v => m.top + (eHi - Math.log10(Math.max(v, 1e-12))) / (eHi - eLo) * h;

    const parts = [];
    parts.push(text(m.left, 24, 'The two designs, and what each bit of the field buys',
                    { size: 14, weight: '600' }));
    parts.push(text(m.left, 41,
                    'The diameter pays for the whole field. What the field buys depends on how ' +
                    'much of it goes to integrity: one bit for a flag, seven for a checksum.',
                    { size: 11, fill: INK }));
    parts.push(text(m.left, 56,
                    'The checksum costs one more bit of carrier and five bits of state, and buys ' +
                    'a false accept rate of 1 in 128 instead of 1 in 2, plus detection of a',
                    { size: 11, fill: INK }));
    parts.push(text(m.left, 71,
                    'carrier that something else has overwritten, which the flag cannot see at all.',
                    { size: 11, fill: INK }));

    for (let e = eLo; e <= eHi; ++e) {
        const y = Y(Math.pow(10, e));
        parts.push(`<line x1="${m.left}" y1="${y}" x2="${m.left + w}" y2="${y}" ` +
                   `stroke="${INK}" stroke-opacity="0.13"/>`);
        parts.push(text(m.left - 8, y + 4, `1e${e}`, { anchor: 'end', size: 10, fill: INK }));
    }

    // The two field widths that were actually built.
    [[15, 'flag, 15 bits'], [16, 'checksum, 16 bits']].forEach(([b, label], i) => {
        parts.push(`<line x1="${X(b)}" y1="${m.top}" x2="${X(b)}" y2="${m.top + h}" ` +
                   `stroke="${FLAG}" stroke-width="1.1" stroke-dasharray="4 3"/>`);
        parts.push(text(X(b) + 5, m.top + 12 + i * 15, label, { size: 10.5, fill: FLAG }));
    });

    const carrierPath = rows.map(r => `${X(r.bits).toFixed(1)},${Y(r.carrier).toFixed(1)}`).join(' L');
    parts.push(`<path d="M${carrierPath}" fill="none" stroke="${CARRIER}" stroke-width="1.8"/>`);
    const statePath = rows.map(r => `${X(r.bits).toFixed(1)},${Y(r.state).toFixed(1)}`).join(' L');
    parts.push(`<path d="M${statePath}" fill="none" stroke="${PAYLOAD}" stroke-width="1.8"/>`);
    // Only where it fits on the axis. Below eleven bits of field the checksum
    // leaves so little payload that the state resolution is meaningless, which
    // is itself the point: seven bits of integrity is a large fixed toll.
    const guardedPath = rows.filter(r => r.guarded > 0 && r.guarded <= 100)
        .map(r => `${X(r.bits).toFixed(1)},${Y(r.guarded).toFixed(1)}`).join(' L');
    parts.push(`<path d="M${guardedPath}" fill="none" stroke="${PAYLOAD}" stroke-width="1.8" ` +
               `stroke-dasharray="5 3"/>`);

    const flagPoint = rows.find(r => r.bits === 15);
    const guardedPoint = rows.find(r => r.bits === 16);
    parts.push(`<circle cx="${X(15)}" cy="${Y(flagPoint.carrier)}" r="3.5" fill="${CARRIER}"/>`);
    parts.push(`<circle cx="${X(15)}" cy="${Y(flagPoint.state)}" r="3.5" fill="${PAYLOAD}"/>`);
    parts.push(`<circle cx="${X(16)}" cy="${Y(guardedPoint.carrier)}" r="3.5" fill="${CARRIER}"/>`);
    parts.push(`<circle cx="${X(16)}" cy="${Y(guardedPoint.guarded)}" r="3.5" fill="${PAYLOAD}"/>`);

    parts.push(text(m.left + w + 10, Y(rows[rows.length - 1].carrier) + 4,
                    'diameter given up', { size: 11, fill: CARRIER }));

    // The two shipped points, spelled out.
    const notes = [
        ['flag, 15 bits, state solid', INK],
        [`  ${flagPoint.carrier.toFixed(2)} per cent carrier`, CARRIER],
        [`  ${flagPoint.states.toLocaleString('en-GB')} states, ${flagPoint.state.toFixed(3)} per cent`, PAYLOAD],
        ['  false accepts 1 in 2', BOUND],
        ['checksum, 16 bits, state dashed', INK],
        [`  ${guardedPoint.carrier.toFixed(2)} per cent carrier`, CARRIER],
        [`  ${guardedPoint.guardedStates.toLocaleString('en-GB')} states, ${guardedPoint.guarded.toFixed(2)} per cent`, PAYLOAD],
        ['  false accepts 1 in 128', BOUND],
    ];
    notes.forEach(([s, colour], i) => {
        parts.push(text(m.left + w + 10, m.top + h / 2 - 40 + i * 16, s,
                        { size: 10, fill: colour }));
    });

    parts.push(`<line x1="${m.left}" y1="${m.top + h}" x2="${m.left + w}" y2="${m.top + h}" ` +
               `stroke="${INK}" stroke-opacity="0.5"/>`);
    for (let b = bLo; b <= bHi; b += 2) {
        parts.push(text(X(b), m.top + h + 18, String(b), { anchor: 'middle', size: 10.5, fill: INK }));
    }
    parts.push(text(m.left + w / 2, height - 12, 'field width taken from the carrier, bits',
                    { anchor: 'middle', size: 11, fill: INK }));
    parts.push(text(14, m.top + h / 2, 'cost, per cent', { anchor: 'middle', size: 11, fill: INK })
               .replace('<text ', `<text transform="rotate(-90 14 ${m.top + h / 2})" `));

    fs.writeFileSync(path.join(OUT, 'design-space.svg'), svg(width, height, parts.join('\n')));
}

renderBitLayout();
renderCarrierError();
renderStateRoundTrip();
renderDesignSpace();
console.log('figures written to docs');
