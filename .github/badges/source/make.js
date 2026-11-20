// Makes the DeltaLoader README badges as SVG files (own design, no shields.io): a dark left part with an icon and the label, the accent
// colour with the value on the right. Icons: Simple Icons (CC0) for brand marks, Tabler Icons (MIT) for the rest.
// Usage: node make.js <out folder> [<icons folder>]
const fs = require('fs');
const path = require('path');
const out = process.argv[2] || path.join(__dirname, 'out');
const icons = process.argv[3] || path.join(__dirname, 'icons');
fs.mkdirSync(out, { recursive: true });

// Verdana-like widths at 11 px (what GitHub falls back to): good enough to size the parts
function textWidth(s, bold) {
    let w = 0;
    for (const c of s) {
        if ('iljI.,:;|!\''.includes(c)) w += 3.4;
        else if ('frt()[] '.includes(c)) w += 4.4;
        else if ('mwMW'.includes(c)) w += 9.6;
        else if (c >= 'A' && c <= 'Z') w += 7.6;
        else if (c >= '0' && c <= '9') w += 7.0;
        else w += 6.6;
    }
    return Math.round(w * (bold ? 1.06 : 1));
}

// The drawing of an icon in a 24 x 24 box: a filled path (Simple Icons) or stroked paths (Tabler).
function icon(spec, colour) {
    const text = fs.readFileSync(path.join(icons, spec.file), 'utf8');
    const ds = [...text.matchAll(/<path[^>]*\sd="([^"]+)"/g)].map((m) => m[1]).filter((d) => d !== 'M0 0h24v24H0z');
    if (!ds.length) throw new Error('no path in ' + spec.file);
    const body = spec.kind === 'brand'
        ? `<path fill="${colour}" d="${ds.join(' ')}"/>`
        : `<g fill="none" stroke="${colour}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${ds.map((d) => `<path d="${d}"/>`).join('')}</g>`;
    return `<g transform="translate(8 7) scale(0.5833)">${body}</g>`;
}

const H = 28;
function badge({ label, value, accent, ink, ic }, id) {
    const markW = 29;                         // the icon
    const lw = markW + textWidth(label, false) + 13;
    const vw = textWidth(value, true) + 22;
    const W = lw + vw;
    const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;');
    const credit = ic.kind === 'brand' ? 'icon: Simple Icons (CC0)' : 'icon: Tabler Icons (MIT, tabler.io)';
    return `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" role="img" aria-label="${esc(label)}: ${esc(value)}">
<!-- DeltaLoader badge, ${credit} -->
<title>${esc(label)}: ${esc(value)}</title>
<clipPath id="${id}"><rect width="${W}" height="${H}" rx="7"/></clipPath>
<g clip-path="url(#${id})">
<rect width="${lw}" height="${H}" fill="#0b110e"/>
<rect x="${lw}" width="${vw}" height="${H}" fill="${accent}"/>
<rect x="${lw}" width="1" height="${H}" fill="#000" fill-opacity=".25"/>
</g>
<rect x=".5" y=".5" width="${W - 1}" height="${H - 1}" rx="6.5" fill="none" stroke="#ffffff" stroke-opacity=".08"/>
${icon(ic, accent)}
<g font-family="Verdana,DejaVu Sans,Segoe UI,sans-serif" font-size="11">
<text x="${markW}" y="18" fill="#c3d3de">${esc(label)}</text>
<text x="${lw + vw / 2}" y="18" fill="${ink}" font-weight="bold" text-anchor="middle">${esc(value)}</text>
</g>
</svg>
`;
}

const list = [
    ['discord', { label: 'Discord', value: 'join us', accent: '#5865F2', ink: '#ffffff', ic: { kind: 'brand', file: 'si_discord.svg' } }],
    ['website', { label: 'Website', value: 'stormworks-tools.com', accent: '#3A9BD0', ink: '#06121a', ic: { kind: 'line', file: 'tb_world.svg' } }],
    ['docs', { label: 'Docs', value: 'read online', accent: '#2FB37F', ink: '#04241a', ic: { kind: 'line', file: 'tb_book.svg' } }],
    ['launcher', { label: 'Launcher', value: 'download', accent: '#3A9BD0', ink: '#06121a', ic: { kind: 'line', file: 'tb_download.svg' } }],
    ['game', { label: 'Stormworks', value: '1.15.27', accent: '#E8A33D', ink: '#2b1a03', ic: { kind: 'line', file: 'tb_ship.svg' } }],
    ['platform', { label: 'Platform', value: 'Windows x64', accent: '#8AA0B2', ink: '#0d1820', ic: { kind: 'line', file: 'tb_brand-windows.svg' } }],
    ['sdk', { label: 'SDK', value: 'C++17', accent: '#7F77DD', ink: '#ffffff', ic: { kind: 'brand', file: 'si_cplusplus.svg' } }],
    ['license', { label: 'License', value: 'DeltaLoader SDK 2.0', accent: '#5F6B73', ink: '#ffffff', ic: { kind: 'line', file: 'tb_scale.svg' } }],
];
for (const [name, b] of list) fs.writeFileSync(path.join(out, `${name}.svg`), badge(b, `dl-${name}`));
console.log(`${list.length} badges in ${out}`);
