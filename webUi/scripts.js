'use strict';
// Launcher web portal. Talks to the device's web server (src/webInterface.cpp); the
// development copy of that server is webUi/backend/server.js.

const $ = s => document.querySelector(s);
const $$ = s => [...document.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
const ico = n => `<svg class="i"><use href="#i-${n}"/></svg>`;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const hex = n => '0x' + n.toString(16).toUpperCase();
const kb = n => n >= 1073741824 ? (n / 1073741824).toFixed(1) + ' GB' : n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : n >= 1024 ? Math.round(n / 1024) + ' KB' : n + ' B';
const psize = n => n >= 1048576 ? +(n / 1048576).toFixed(2) + ' MB' : n >= 1024 ? +(n / 1024).toFixed(2) + ' KB' : n + ' B';
const dur = s => { const d = s / 86400 | 0, h = s / 3600 % 24 | 0, m = s / 60 % 60 | 0; return (d ? d + 'd ' : '') + (d || h ? h + 'h ' : '') + m + 'm'; };

// The device handles one request at a time. While an upload or install runs (busy > 0) the
// status poll stays quiet, so it can't queue behind the transfer and stall it.
let busy = 0;
let info = null;
let dev = 'device';

async function api(url, opt = {}) {
  let r;
  try { r = await fetch(url, opt); } catch { throw new Error(`The ${dev} did not answer.`); }
  if (r.status === 401) { location.reload(); throw new Error('Logged out.'); }
  const t = await r.text();
  if (!r.ok) throw new Error(t || r.statusText);
  return t;
}
const getJson = async url => JSON.parse(await api(url));
const post = (url, fields) => {
  const f = new FormData();
  for (const k in fields) f.append(k, fields[k]);
  return api(url, { method: 'POST', body: f });
};
const postJson = async (url, o) => JSON.parse(await api(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(o) }));

// XMLHttpRequest, for upload progress. Its own timeout caps the whole request, which fails big
// uploads that are slow but still moving, so this aborts only after 30 s without progress.
function upload(url, fields, onProgress) {
  return new Promise((resolve, reject) => {
    const x = new XMLHttpRequest(), f = new FormData();
    for (const k in fields) Array.isArray(fields[k]) ? f.append(k, ...fields[k]) : f.append(k, fields[k]);
    let timer;
    const arm = () => { clearTimeout(timer); timer = setTimeout(() => x.abort(), 30000); };
    x.upload.onprogress = e => { arm(); if (e.lengthComputable) onProgress(e.loaded / e.total); };
    x.onload = () => {
      clearTimeout(timer);
      if (x.status === 200 && x.responseText === 'OK') resolve();
      else if (x.status === 401) { location.reload(); reject(new Error('Logged out.')); }
      else reject(new Error(x.responseText || 'HTTP ' + x.status));
    };
    x.onerror = () => { clearTimeout(timer); reject(new Error('connection lost')); };
    x.onabort = () => { clearTimeout(timer); reject(new Error('stalled, no progress for 30 s')); };
    x.open('POST', url);
    arm();
    x.send(f);
  });
}

// Toasts live in the open dialog, if there is one, so its backdrop doesn't cover them.
const toastHost = () => [...document.querySelectorAll('dialog[open]')].pop() || document.body;
function toast(msg, err) {
  const d = document.createElement('div'), box = $('#toasts');
  d.className = 'toast' + (err ? ' err' : '');
  d.textContent = msg;
  toastHost().append(box);
  box.append(d);
  setTimeout(() => d.remove(), err ? 8000 : 4000);
}
const fail = e => toast(e.message || String(e), true);

// One dialog for confirmations and short forms. Resolves to the field values (true when there
// are none), or null when cancelled.
function ask({ title, text = '', fields = [], ok = 'ok', danger = false }) {
  const d = $('#dlg'), body = $('#dlg-b'), okBtn = $('#dlg-ok');
  if (d.open) return Promise.resolve(null); // one question at a time
  $('#dlg-t').textContent = title;
  body.innerHTML = (text ? `<p>${esc(text)}</p>` : '') + fields.map((f, i) => {
    const id = 'f' + i, label = `<label for="${id}">${esc(f.label)}${f.type === 'range' ? `<output id="${id}o"></output>` : ''}</label>`;
    if (f.options) return label + `<select id="${id}">${f.options.map(o => `<option value="${esc(o.value)}"${o.value === f.value ? ' selected' : ''}>${esc(o.label)}</option>`).join('')}</select>`;
    if (f.type === 'range') return label + `<input id="${id}" type="range" min="${f.min}" max="${f.max}" step="${f.step}" value="${f.value}">`;
    return label + `<input id="${id}" type="${f.type || 'text'}" value="${esc(f.value)}" required spellcheck="false" autocomplete="off">`;
  }).join('');
  okBtn.textContent = ok;
  okBtn.className = danger ? 'dng' : 'pri';
  fields.forEach((f, i) => {
    if (f.type !== 'range') return;
    const r = $('#f' + i), out = $('#f' + i + 'o'), show = () => { out.textContent = f.format(+r.value); };
    r.oninput = show;
    show();
  });
  d.returnValue = '';
  d.showModal();
  const first = $('#f0');
  if (first && first.type === 'text') {
    first.focus();
    const dot = first.value.lastIndexOf('.'); // a file's name is selected without its extension
    first.setSelectionRange(0, fields[0].stem && dot > 0 ? dot : first.value.length);
  } else okBtn.focus();
  return new Promise(resolve => d.addEventListener('close', () => {
    if (d.returnValue !== 'ok') return resolve(null);
    const v = {};
    fields.forEach((f, i) => { v[f.name] = $('#f' + i).value.trim(); });
    resolve(fields.length ? v : true);
  }, { once: true }));
}

// ── Sections: the bar's numbered workspaces, also keys 1-5 ─────────────────────────────
const PAGES = ['overview', 'apps', 'files', 'partitions', 'settings'];
let page = '';
let filesLoaded = false;
function route() {
  page = PAGES.includes(location.hash.slice(1)) ? location.hash.slice(1) : 'overview';
  $$('.page').forEach(p => { p.hidden = p.dataset.p !== page; });
  $$('.ws a').forEach(a => a.classList.toggle('on', a.hash === '#' + page));
  document.title = page + ' · launcher';
  if (page === 'files' && !filesLoaded) { filesLoaded = true; ls('/'); }
  if (page === 'partitions') loadParts();
  if (page === 'settings') loadSettings();
}
addEventListener('hashchange', route);
addEventListener('keydown', e => {
  if (e.key === 'Escape') $('#pmenu').hidden = true;
  if (e.ctrlKey || e.metaKey || e.altKey || e.target.closest('input,select,textarea,dialog')) return;
  const n = '12345'.indexOf(e.key);
  if (n >= 0) location.hash = PAGES[n];
  else if (e.key === '/' && page === 'files') { e.preventDefault(); $('#filter').focus(); }
});

// The clock, in the Omarchy bar's format.
const DAYS = 'Sun Mon Tue Wed Thu Fri Sat'.split(' ');
const MONTHS = 'Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec'.split(' ');
function tick() {
  const d = new Date(), p = n => String(n).padStart(2, '0');
  $('#clock').textContent = `[ ${DAYS[d.getDay()]} ${d.getDate()} ${MONTHS[d.getMonth()]} // ${p(d.getHours())}:${p(d.getMinutes())} ]`;
}

// ── Status: the bar's tray, the overview, the apps ───────────────────────────────────────
async function status() {
  if (busy) return;
  try {
    info = await getJson('/systeminfo');
  } catch {
    $('#wifi').hidden = false;
    $('#wifi').className = 'mod bad';
    $('#wifi').innerHTML = ico('wifi') + '<span>offline</span>';
    return;
  }
  dev = (info.DEVICE || 'device').replace(/^(M5Stack|M5|LilyGO|Lilygo)\s+/, '');
  renderBar();
  renderOverview();
  renderApps();
  renderSd();
  $('#sdpins').hidden = !info.HEADLESS;
}

function renderBar() {
  const w = info.WIFI || {}, b = info.BATTERY, wifi = $('#wifi'), bat = $('#bat');
  wifi.hidden = false;
  wifi.className = 'mod' + (w.rssi && w.rssi < -75 ? ' warn' : '');
  wifi.innerHTML = ico('wifi') + `<span>${esc(w.ap ? 'access point' : w.ssid || 'wifi')}</span>`;
  wifi.title = (w.ap ? "The Launcher's own WiFi" : (w.ssid || '') + (w.rssi ? `, ${w.rssi} dBm` : '')) + (w.ip ? ' · ' + w.ip : '');
  bat.hidden = !(b > 0);
  if (b > 0) {
    bat.className = 'mod' + (b <= 15 && !info.CHARGING ? ' bad' : '');
    bat.innerHTML = ico(info.CHARGING ? 'bolt' : 'bat') + `<span>${b}%</span>`;
    bat.title = info.CHARGING ? `Charging, ${b}%` : `Battery ${b}%`;
  }
}

function meter(label, fraction, text, low) {
  const f = Math.max(0, Math.min(1, fraction || 0));
  const level = low ? (f < 0.15 ? ' crit' : f < 0.3 ? ' hi' : '') : (f > 0.9 ? ' crit' : f > 0.75 ? ' hi' : '');
  return `<div class="mtr">${label ? `<span>${label}</span>` : ''}<div class="meter${level}"><i style="width:${(f * 100).toFixed(1)}%"></i></div><b>${esc(text)}</b></div>`;
}

function renderOverview() {
  const sd = info.SD || {}, w = info.WIFI || {}, name = (info.DEVICE || 'device').toLowerCase().replace(/\s+/g, '-');
  const rows = [
    ['launcher', info.VERSION],
    ['device', info.DEVICE],
    ['uptime', info.UPTIME != null ? dur(info.UPTIME) : ''],
    ['wifi', w.ap ? `access point · ${w.ip}` : [w.ssid, w.ip, w.rssi ? w.rssi + ' dBm' : ''].filter(Boolean).join(' · ')],
    ['memory', info.HEAP ? `${kb(info.HEAP)} free of ${kb(info.HEAP_TOTAL)}` : ''],
    ['sd card', sd.totalBytes ? `${sd.used} used of ${sd.total}` : 'none'],
    ['battery', info.BATTERY > 0 ? info.BATTERY + '%' + (info.CHARGING ? ', charging' : '') : ''],
  ].filter(r => r[1]);
  // fastfetch's layout: user@host, a rule, the facts, then the theme's colours.
  $('#ff').innerHTML = `<div class="who"><b>launcher</b>@<b>${esc(name)}</b></div><div class="sep">${'-'.repeat(9 + name.length)}</div>` +
    '<dl>' + rows.map(([k, v]) => `<dt>${k}</dt><dd>${esc(v)}</dd>`).join('') + '</dl>' +
    '<div class="swatch">' + ['ff3b6b', '00ff9c', 'e6ff5a', '20b8ff', 'ff2bd6', '00e5ff', 'b6f5d2', '3f7d64'].map(c => `<i style="background:#${c}"></i>`).join('') + '</div>';
  $('#res').innerHTML =
    meter('sd card', sd.usedBytes / sd.totalBytes, sd.totalBytes ? `${sd.used} / ${sd.total}` : 'no card') +
    (info.HEAP_TOTAL ? meter('memory', 1 - info.HEAP / info.HEAP_TOTAL, `${kb(info.HEAP_TOTAL - info.HEAP)} / ${kb(info.HEAP_TOTAL)}`) : '') +
    (info.BATTERY > 0 ? meter('battery', info.BATTERY / 100, info.BATTERY + '%' + (info.CHARGING ? ' +' : ''), true) : '');
}

function renderSd() {
  const sd = info.SD || {};
  $('#sd').innerHTML = sd.totalBytes ? meter('', sd.usedBytes / sd.totalBytes, `${sd.free} free`) : '';
}

function renderApps() {
  const apps = info.APPS || [];
  const html = apps.length
    ? apps.map(a => `<li>${ico('chip')}<span class="an">${esc(a.name)}</span><span class="al">${esc(a.label)}</span><button data-boot="${esc(a.label)}" data-name="${esc(a.name)}">${ico('play')}start</button></li>`).join('')
    : '<li class="empty">No apps installed yet.</li>';
  $('#apps-list').innerHTML = html;
  $('#ov-apps').innerHTML = html;
}

async function bootApp(label, name) {
  if (!await ask({ title: `Start ${name}?`, text: `The ${dev} restarts into ${name}, and this page goes offline.`, ok: 'start' })) return;
  try {
    await api('/bootapp', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'label=' + encodeURIComponent(label) });
    offline(`Starting ${name}.`);
  } catch (e) { fail(e); }
}

async function reboot(now) {
  if (!now && !await ask({ title: `Restart the ${dev}?`, text: 'This page goes offline until Web UI is started on it again.', ok: 'restart', danger: true })) return;
  try { await api('/reboot'); offline(`The ${dev} is restarting.`); } catch (e) { fail(e); }
}

function offline(msg) {
  busy++;
  $('#gone-m').textContent = msg;
  $('#gone').hidden = false;
}

// ── Install a .bin from this computer ────────────────────────────────────────────────────
// How a .bin is read: a merged image (partition table at 0x8000) gives its app and data
// partitions; anything else is a bare app image. The device copies each part from the upload.
const le32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const alignUp = (v, a) => Math.ceil(v / a) * a;
function partLabel(b, o) {
  let s = '';
  for (let i = 0; i < 16 && b[o + i]; i++) s += String.fromCharCode(b[o + i]);
  return s;
}
function blank(data, offset, len = 16) {
  const end = Math.min(offset + len, data.length);
  if (end <= offset) return true;
  let ff = true, zero = true;
  for (let i = offset; i < end; i++) { if (data[i] !== 0xFF) ff = false; if (data[i] !== 0) zero = false; }
  return ff || zero;
}
// The size of the ESP app image at imageOffset (header, segments, checksum, optional hash), or 0.
function imageSize(data, imageOffset) {
  if (imageOffset + 24 > data.length) return 0;
  const segments = data[imageOffset + 1], hashed = data[imageOffset + 23];
  if (data[imageOffset] !== 0xE9 || segments === 0 || segments > 16) return 0;
  let at = imageOffset + 24;
  for (let i = 0; i < segments; i++) {
    if (at + 8 > data.length) return 0;
    const size = le32(data, at + 4);
    at += 8;
    if (size > data.length || at > data.length - size) return 0;
    at += size;
  }
  let end = alignUp(at, 16) + 1;
  if (hashed) end += 32;
  end = alignUp(end, 16);
  return end <= imageOffset || end > data.length ? 0 : end - imageOffset;
}
function analyzeBin(data) {
  const parts = [];
  let appOffset = 0, appSize = 0;
  if (data[0x8000] === 0xAA && data[0x8001] === 0x50 && data[0x8002] === 0x01) {
    for (let pos = 0x8000; pos < 0x9000 && pos + 32 <= data.length; pos += 32) {
      const s = data.subarray(pos, pos + 32);
      if ((s[0] === 0xEB && s[1] === 0xEB) || (s[0] === 0xFF && s[1] === 0xFF)) break;
      const type = s[2], subtype = s[3], offset = le32(s, 4), declared = le32(s, 8), label = partLabel(s, 12);
      if (type === 0x00 && [0x00, 0x10, 0x20].includes(subtype)) {
        appOffset = offset || 0x10000;
        const measured = imageSize(data, appOffset);
        appSize = declared;
        if (data.length < appOffset + appSize) appSize = data.length - appOffset;
        if (measured > 0 && (appSize === 0 || measured < appSize)) appSize = measured;
      }
      if (type === 0x01 && [0x81, 0x82, 0x83].includes(subtype) && offset < data.length) {
        let size = declared;
        if (data.length < offset + size) size = data.length - offset;
        const fsLike = subtype === 0x82 || subtype === 0x83;
        // An empty SPIFFS/LittleFS partition is still listed, so the device creates it, with nothing to copy.
        if (fsLike && blank(data, offset)) size = 0;
        if (size > 0 || (fsLike && declared > 0)) parts.push({ kind: 'data', subtype, label, offset, size, declared });
      }
    }
  } else {
    appSize = imageSize(data, 0) || data.length;
  }
  if (appSize > 0) parts.unshift({ kind: 'app', subtype: 0, label: 'app', offset: appOffset, size: appSize, declared: appSize });
  return parts;
}

let otaPending = null;
async function pickBin(file) {
  if (!file) return;
  if (!/\.bin$/i.test(file.name)) return toast(`${file.name} is not a .bin file.`, true);
  const parts = analyzeBin(new Uint8Array(await file.arrayBuffer()));
  const out = $('#ota');
  if (!parts.length) {
    otaPending = null;
    out.innerHTML = `<p class="warn">Nothing installable found in ${esc(file.name)}.</p>`;
    return;
  }
  otaPending = {
    file,
    manifest: {
      sourceName: file.name,
      parts: parts.map(p => ({ kind: p.kind, label: p.label || '', subtype: p.subtype, sourceOffset: p.offset, copySize: p.size, declaredSize: p.declared || p.size })),
    },
  };
  out.innerHTML = `<table class="list"><tr><th>part</th><th>label</th><th class="sz">offset</th><th class="sz">size</th></tr>` +
    parts.map(p => `<tr><td class="${p.kind === 'app' ? 'bin' : 'dir'}">${p.kind}</td><td>${esc(p.label)}</td><td class="sz">${hex(p.offset)}</td><td class="sz">${psize(p.size)}</td></tr>`).join('') + '</table>' +
    (parts.some(p => p.kind === 'data') ? '<p class="note">The data partition can be SPIFFS, LittleFS or FAT, depending on the firmware.</p>' : '') +
    `<div class="row end"><button data-act="otacancel">cancel</button><button class="pri" data-act="otago">${ico('install')}install ${esc(file.name)}</button></div>`;
}

async function installBin() {
  if (!otaPending) return;
  const { file, manifest } = otaPending, out = $('#ota');
  otaPending = null;
  out.innerHTML = `<p>installing ${esc(file.name)}</p><div class="meter"><i id="ota-bar"></i></div><p class="note" id="ota-pct">0%</p>`;
  busy++;
  try {
    await post('/OTA', { update: 1 });
    await post('/OTA', { command: 0, size: file.size, manifest: JSON.stringify(manifest) });
    await upload('/OTAFILE', { file1: [file, file.name] }, f => {
      $('#ota-bar').style.width = f * 100 + '%';
      $('#ota-pct').textContent = Math.round(f * 100) + '%';
    });
    out.innerHTML = `<p class="ok">${esc(file.name)} is installed. Restart the ${esc(dev)} to finish.</p><div class="row end"><button class="pri" data-act="restart">${ico('power')}restart now</button></div>`;
  } catch (e) {
    out.innerHTML = `<p class="warn">The install failed: ${esc(e.message)}</p>`;
  } finally {
    busy--;
  }
  status();
}

// ── Files on the SD card ─────────────────────────────────────────────────────────────────
const TEXT = /\.(txt|ini|conf|cfg|json|csv|log|md|c|cpp|h|hpp|js|css|html?|ts|xml|ya?ml|py|sh|lua|toml|sub|ir|bat)$/i;
const BIN = /\.bin$/i;
let cwd = '/';
let entries = [];
const join = (dir, name) => (dir === '/' ? '' : dir.replace(/\/$/, '')) + '/' + name;
const parentOf = p => p.slice(0, p.lastIndexOf('/')) || '/';

async function ls(dir = cwd) {
  let text;
  try { text = await api('/listfiles?folder=' + encodeURIComponent(dir)); } catch (e) { return fail(e); }
  cwd = dir;
  entries = [];
  for (const line of text.split('\n')) {
    const type = line.slice(0, 2), at = line.lastIndexOf(':'), name = line.slice(3, at);
    if (type === 'Fo' || type === 'Fi') entries.push({ name, dir: type === 'Fo', size: type === 'Fi' ? line.slice(at + 1) : '', path: join(dir, name) });
  }
  entries.sort((a, b) => b.dir - a.dir || a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: 'base' }));
  $('#filter').value = '';
  renderFiles();
  let p = '';
  $('#crumbs').innerHTML = `<a href="#files" data-dir="/">sd:/</a>` + cwd.split('/').filter(Boolean).map((s, i) => {
    p += '/' + s;
    return `${i ? '<i>/</i>' : ''}<a href="#files" data-dir="${esc(p)}">${esc(s)}</a>`;
  }).join('');
}

const rowBtn = (act, title, cls = '') => `<button class="ab ${cls}" data-a="${act}" title="${title}" aria-label="${title}">${ico(act)}</button>`;
function renderFiles() {
  const q = $('#filter').value.toLowerCase();
  const shown = entries.map((e, i) => ({ e, i })).filter(({ e }) => e.name.toLowerCase().includes(q));
  $('#flist').innerHTML = (cwd !== '/' ? `<tr data-up><td class="ic">${ico('up')}</td><td class="nm"><a href="#files">..</a></td><td class="sz"></td><td class="ac"></td></tr>` : '') +
    shown.map(({ e, i }) => {
      const bin = BIN.test(e.name), text = TEXT.test(e.name), kind = e.dir ? 'dir' : bin ? 'bin' : '';
      const name = esc(e.name) + (e.dir ? '/' : '');
      // Folders open and text files edit from their name; other names are plain text.
      return `<tr data-i="${i}"><td class="ic ${kind}">${ico(e.dir ? 'dir' : bin ? 'chip' : 'file')}</td>` +
        `<td class="nm">${e.dir || text ? `<a href="#files" class="${kind}">${name}</a>` : `<span class="${kind}">${name}</span>`}</td><td class="sz">${esc(e.size)}</td><td class="ac">` +
        (bin ? rowBtn('install', 'Install on the device') : '') + (text ? rowBtn('edit', 'Edit') : '') + (e.dir ? '' : rowBtn('download', 'Download')) +
        rowBtn('rename', 'Rename') + rowBtn('trash', 'Delete', 'del') + '</td></tr>';
    }).join('') +
    (shown.length ? '' : `<tr><td></td><td class="empty" colspan="3">${q ? 'Nothing here matches.' : 'This folder is empty.'}</td></tr>`);
}

function download(path, name) {
  const a = document.createElement('a');
  a.href = `/file?name=${encodeURIComponent(path)}&action=download`;
  a.download = name;
  document.body.append(a);
  a.click();
  a.remove();
}

const badName = n => /[\\/]/.test(n) && (toast('A name cannot contain / or \\.', true), true);
async function fileAction(act, e) {
  try {
    if (act === 'download') return download(e.path, e.name);
    if (act === 'edit') return edit(e.path);
    if (act === 'install') {
      if (!await ask({ title: `Install ${e.name}?`, text: `The ${dev} installs it from the SD card and shows the progress on its screen. Restart it when it says so.`, ok: 'install' })) return;
      return toast(await post('/UPDATE', { fileName: e.path }));
    }
    if (act === 'rename') {
      const v = await ask({ title: 'Rename', fields: [{ name: 'n', label: 'new name', value: e.name, stem: !e.dir }], ok: 'rename' });
      if (!v || v.n === e.name || badName(v.n)) return;
      toast(await post('/rename', { filePath: e.path, fileName: v.n }));
      return ls();
    }
    if (act === 'trash') {
      if (!await ask({ title: `Delete ${e.name}?`, text: e.dir ? 'The folder and everything in it are deleted. This cannot be undone.' : 'This cannot be undone.', ok: 'delete', danger: true })) return;
      toast(await api(`/file?name=${encodeURIComponent(e.path)}&action=delete`));
      ls();
    }
  } catch (err) { fail(err); }
}

async function mkdir() {
  const v = await ask({ title: 'New folder', fields: [{ name: 'n', label: `name, inside ${cwd}` }], ok: 'create' });
  if (!v || badName(v.n)) return;
  try { toast(await api(`/file?name=${encodeURIComponent(join(cwd, v.n))}&action=create`)); ls(); } catch (e) { fail(e); }
}

async function mkfile() {
  const v = await ask({ title: 'New file', fields: [{ name: 'n', label: `name, inside ${cwd}`, value: 'notes.txt', stem: true }], ok: 'create' });
  if (v && !badName(v.n)) edit(join(cwd, v.n), true);
}

// Uploads go one at a time, each retried twice, with a short pause between files so the device
// can close one before the next arrives.
async function uploadFiles(files, dir = cwd) {
  if (!files.length) return;
  busy++;
  const list = $('#uplist');
  $('#upwin').hidden = false;
  list.innerHTML = '';
  let done = 0;
  const failed = [];
  for (const file of files) {
    const name = file.webkitRelativePath || file.name, row = document.createElement('div');
    row.className = 'up';
    row.innerHTML = `<i></i><span>${esc(name)}</span>`;
    list.append(row);
    const bar = row.firstChild, label = row.lastChild;
    for (let attempt = 1; ; attempt++) {
      try {
        await upload('/', { folder: dir, file: [file, name] }, f => { bar.style.width = f * 100 + '%'; });
        row.classList.add('ok');
        done++;
        break;
      } catch (e) {
        if (attempt >= 3) { row.classList.add('fail'); label.textContent = `${name}: ${e.message}`; failed.push(name); break; }
        bar.style.width = '0';
        label.textContent = `${name} · retry ${attempt + 1} of 3`;
        await sleep(400 * attempt);
      }
    }
    await sleep(60);
  }
  busy--;
  if (failed.length) toast(`Uploaded ${done} of ${files.length}. ${failed.length} failed, listed under uploads.`, true);
  else { toast(`Uploaded ${done} file${done === 1 ? '' : 's'} to ${dir}.`); setTimeout(() => { if (!busy) $('#upwin').hidden = true; }, 4000); }
  if (dir === cwd) ls();
  status();
}

// Dropped folders arrive as entries; readEntries hands them over in batches, so it is called
// until it returns none.
function walk(entry, path, out) {
  return new Promise(resolve => {
    if (entry.isFile) {
      entry.file(f => { out.push(new File([f], path + f.name, { type: f.type })); resolve(); }, resolve);
    } else if (entry.isDirectory) {
      const reader = entry.createReader(), all = [];
      const next = () => reader.readEntries(batch => {
        if (batch.length) { all.push(...batch); next(); }
        else Promise.all(all.map(c => walk(c, path + entry.name + '/', out))).then(resolve);
      }, resolve);
      next();
    } else resolve();
  });
}

// The text editor. The device saves at most 32 KB from the browser (editfileHandler).
const MAX_EDIT = 32768;
let edPath = '', edSaved = '';
async function edit(path, isNew) {
  const d = $('#ed'), x = $('#ed-x');
  edPath = path;
  $('#ed-t').textContent = path;
  x.value = '';
  x.disabled = true;
  if (!d.open) d.showModal();
  try {
    edSaved = isNew ? '' : await api('/editfile?name=' + encodeURIComponent(path));
  } catch (e) { d.close(); return fail(e); }
  x.value = edSaved;
  x.disabled = false;
  x.focus();
  x.setSelectionRange(0, 0);
  x.scrollTop = 0;
  edState();
}
function edState() {
  const v = $('#ed-x').value, n = new Blob([v]).size;
  $('#ed-m').textContent = v !== edSaved ? 'modified' : '';
  $('#ed-n').textContent = kb(n) + (n > MAX_EDIT ? ', too big to save from here (32 KB at most)' : '');
}
async function edSave() {
  const v = $('#ed-x').value;
  if (new Blob([v]).size > MAX_EDIT) return toast('Too big to save from here: 32 KB at most.', true);
  try {
    const r = await api('/editfile?name=' + encodeURIComponent(edPath), { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: v });
    if (r !== 'OK') throw new Error(r || 'Could not save.');
    edSaved = v;
    edState();
    toast('Saved ' + edPath);
    if (filesLoaded && parentOf(edPath) === cwd) ls();
  } catch (e) { fail(e); }
}
async function edClose() {
  if ($('#ed-x').value !== edSaved && !await ask({ title: 'Close without saving?', text: edPath, ok: 'discard changes', danger: true })) return;
  $('#ed').close();
}

// ── Partitions ───────────────────────────────────────────────────────────────────────────
let pm = null;
async function loadParts() {
  try { pm = await getJson('/partitions'); renderParts(); } catch (e) { fail(e); }
}
function renderParts() {
  const items = [...pm.entries].sort((a, b) => a.offset - b.offset);
  const seg = (size, kind, title) => size > 0 ? `<i class="k-${kind}" style="flex:${size}" title="${esc(title)}"></i>` : '';
  let map = items.length && items[0].offset > 0 ? seg(items[0].offset, 'sys', 'system: bootloader and partition table') : '';
  items.forEach((e, i) => {
    const end = i + 1 < items.length ? items[i + 1].offset : pm.flashSize, gap = end - e.offset - e.size;
    map += seg(e.size, e.type === 0 ? 'app' : 'data', `${e.label} · ${psize(e.size)}`) + seg(gap, 'free', `free · ${psize(gap)}`);
  });
  $('#pm-map').innerHTML = `<div class="pmap">${map}</div><div class="legend"><span><i class="k-app"></i>app</span><span><i class="k-data"></i>data</span>` +
    `<span><i class="k-free"></i>free</span><span><i class="k-sys"></i>system</span><span class="dim">${psize(pm.flashSize)} of flash</span></div>`;
  $('#pm-tools').innerHTML = (pm.dirty
    ? `<div class="banner">Nothing is written until you apply, which restarts the ${esc(dev)}.<span class="row"><button data-pm="discard">discard</button><button class="pri" data-pm="apply">apply and restart</button></span></div>`
    : '') +
    `<div class="btns"><button data-pm="create" data-t="0" data-s="16" data-l="app" data-k="1024">${ico('plus')}app</button>` +
    `<button data-pm="create" data-t="1" data-s="129" data-l="vfs" data-k="512">${ico('plus')}FAT</button>` +
    `<button data-pm="create" data-t="1" data-s="130" data-l="spiffs" data-k="256">${ico('plus')}SPIFFS</button></div>`;
  $('#pm-list').innerHTML = '<tr><th>label</th><th class="pt">type</th><th class="sz">offset</th><th class="sz">size</th><th class="ac"></th></tr>' +
    pm.entries.map((e, i) => `<tr data-i="${i}"><td class="nm" title="flags ${hex(e.flags || 0)}"><b>${esc(e.label)}</b>` +
      (e.running ? '<span class="tag run">running</span>' : '') + (e.protected ? '<span class="tag">protected</span>' : '') +
      (e.appName ? `<small>${esc(e.appName)}</small>` : '') + (e.dataLabels && e.dataLabels.length ? `<small>data: ${esc(e.dataLabels.join(', '))}</small>` : '') +
      `</td><td class="pt dim">${esc(e.typeName)}/${esc(e.subtypeName)}</td><td class="sz">${hex(e.offset)}</td><td class="sz">${psize(e.size)}</td><td class="ac">` +
      (e.protected ? '' : rowBtn('resize', 'Resize') + (e.type === 1 ? rowBtn('save', 'Back up to the SD card') + rowBtn('restore', 'Restore from the SD card') + rowBtn('wipe', 'Erase its data', 'del') : '') + rowBtn('trash', 'Remove', 'del')) +
      '</td></tr>').join('');
  const free = pm.freeRanges || [];
  $('#pm-freewin').hidden = !free.length;
  $('#pm-free').innerHTML = '<tr><th>offset</th><th class="sz">size</th></tr>' + free.map(r => `<tr><td>${hex(r.offset)}</td><td class="sz">${psize(r.size)}</td></tr>`).join('');
}
async function pmChange(fields) { await post('/partitions', fields); loadParts(); }
async function partAction(act, btn, e) {
  try {
    if (act === 'create') {
      const v = await ask({ title: 'New partition', fields: [{ name: 'label', label: 'label', value: btn.dataset.l }, { name: 'kb', label: 'size in KB', value: btn.dataset.k, type: 'number' }], ok: 'add' });
      if (!v) return;
      const size = parseInt(v.kb, 10) * 1024;
      if (!(size > 0)) return toast('That size is not valid.', true);
      return pmChange({ action: 'create', type: btn.dataset.t, subtype: btn.dataset.s, label: v.label, size });
    }
    if (act === 'apply') {
      if (!await ask({ title: 'Write the partition table?', text: `The ${dev} writes it and restarts, and this page goes offline.`, ok: 'apply', danger: true })) return;
      await post('/partitions', { action: 'apply' });
      return offline('Writing the partition table and restarting.');
    }
    if (act === 'discard') {
      if (await ask({ title: 'Discard the changes?', ok: 'discard' })) await pmChange({ action: 'discard' });
      return;
    }
    if (act === 'resize') {
      const step = e.alignment || 4096, max = e.maxOffset - e.offset;
      const v = await ask({ title: 'Resize ' + e.label, fields: [{ name: 's', label: 'size', type: 'range', min: step, max, step, value: Math.min(Math.max(e.size, step), max), format: n => `${psize(n)} · ${hex(n)}` }], ok: 'resize' });
      if (v) await pmChange({ action: 'resize', offset: e.offset, size: v.s });
      return;
    }
    if (act === 'save') return toast('Backed up to ' + JSON.parse(await post('/partitions', { action: 'backup', label: e.label })).path);
    if (act === 'restore') {
      const list = (await getJson('/partitions?list=backups&label=' + encodeURIComponent(e.label))).backups || [];
      if (!list.length) return toast(`There are no backups of ${e.label} on the SD card.`, true);
      const v = await ask({ title: 'Restore ' + e.label, text: 'This overwrites what is on the partition now.', fields: [{ name: 'p', label: 'backup', options: list.map(b => ({ value: b.path, label: b.path })), value: list[list.length - 1].path }], ok: 'restore', danger: true });
      if (!v) return;
      await post('/partitions', { action: 'restore', label: e.label, path: v.p });
      return toast(`Restored ${e.label}.`);
    }
    if (act === 'wipe') {
      if (!await ask({ title: `Erase ${e.label}?`, text: 'Its data is erased right away, not when you apply. This cannot be undone.', ok: 'erase', danger: true })) return;
      await pmChange({ action: 'format', offset: e.offset });
      return toast(`Erased ${e.label}.`);
    }
    if (act === 'trash' && await ask({ title: `Remove ${e.label}?`, text: 'Nothing changes on the device until you apply.', ok: 'remove', danger: true })) await pmChange({ action: 'delete', offset: e.offset });
  } catch (err) { fail(err); }
}

// ── Settings: the same values as the device's Settings menu ─────────────────────────────
const DIM_TIMES = [[0, 'never'], [10, '10 s'], [15, '15 s'], [30, '30 s'], [45, '45 s'], [60, '60 s']];
// The device's UI Color presets (setUiColor in src/settings.cpp): FG, BG, AL, odd, even, as RGB565.
const THEMES = [
  ['omarchy', 0x07F3, 0x0041, 0xB7BA, 0x30C5, 0x32E5], ['default', 0x07E0, 0x0000, 0xF800, 0x30C5, 0x32E5],
  ['red', 0xF800, 0x0000, 0xE3E0, 0xFBC0, 0xAAC0], ['blue', 0x94BF, 0x0000, 0xD81F, 0xD69F, 0x079F],
  ['yellow', 0xFFE0, 0x0000, 0xFB80, 0x9480, 0xBAE0], ['purple', 0xE01F, 0x0000, 0xF800, 0xF57F, 0x89D3],
  ['white', 0xFFFF, 0x0000, 0x6B6D, 0x630C, 0x8410], ['black', 0x0000, 0xFFFF, 0x6B6D, 0x8C71, 0xB596],
];
// [setting, shown inverted, label, what it does]
const TOGGLES = [
  ['bootToApp', true, 'Boot to Launcher', 'Start in the Launcher. Off: start your installed app, or ask which one.'],
  ['autoConnect', false, 'Auto connect to known WiFi', 'Join a saved network without asking.'],
  ['onlyBins', true, 'See all files', "The device's file browser shows every file, not only .bin files."],
  ['noDotFiles', true, 'Show dotfiles', "The device's file browser shows names that start with a dot."],
  ['autoBackup', false, 'Auto backup', "Offer to back up an app's data partition before the app is removed."],
  ['askSpiffs', false, 'Ask to copy SPIFFS', 'Ask whether to copy a SPIFFS partition when installing. Off: it is never copied.'],
  ['DDLB', true, 'DeepSleep starts Launcher', 'Waking from deep sleep opens the Launcher.'],
];
const rgb = c => `rgb(${(c >> 11 & 31) * 255 / 31 | 0},${(c >> 5 & 63) * 255 / 63 | 0},${(c & 31) * 255 / 31 | 0})`;
let st = null;
async function loadSettings() {
  try { st = await getJson('/settings'); renderSettings(); } catch (e) { fail(e); }
}
function renderSettings() {
  $('#s-bright').value = st.bright;
  $('#s-bright-v').textContent = st.bright + '%';
  const dims = DIM_TIMES.some(d => d[0] === st.dimmerSet) ? DIM_TIMES : [...DIM_TIMES, [st.dimmerSet, st.dimmerSet + ' s']];
  $('#s-dim').innerHTML = dims.map(([v, l]) => `<option value="${v}"${v === st.dimmerSet ? ' selected' : ''}>${l}</option>`).join('');
  $('#themes').innerHTML = THEMES.map(([name, fg, bg, al], i) =>
    `<button class="th${fg === st.FGCOLOR && bg === st.BGCOLOR ? ' on' : ''}" data-th="${i}" aria-pressed="${fg === st.FGCOLOR && bg === st.BGCOLOR}"><i style="background:${rgb(bg)}"><b style="background:${rgb(fg)}"></b><b style="background:${rgb(al)}"></b></i>${name}</button>`).join('');
  $('#toggles').innerHTML = TOGGLES.map(([k, inv, label, about]) =>
    `<label class="tg"><input type="checkbox" class="sw" data-k="${k}" data-inv="${inv ? 1 : ''}"${(inv ? !st[k] : st[k]) ? ' checked' : ''}><div><b>${label}</b><small>${about}</small></div></label>`).join('');
  $('#s-boot').innerHTML = [0, 1, 2, 3, 4].map(v => `<option value="${v}"${v === st.bootTimer ? ' selected' : ''}>${v + 1} s</option>`).join('');
  $('#nets').innerHTML = st.wifi.length
    ? st.wifi.map(n => `<li>${ico(n.pwd ? 'lock' : 'wifi')}<span>${esc(n.ssid)}</span><button class="ab del" data-forget="${esc(n.ssid)}" title="Forget" aria-label="Forget ${esc(n.ssid)}">${ico('trash')}</button></li>`).join('')
    : '<li class="empty">No saved networks.</li>';
  $('#deflogin').hidden = !st.defaultLogin;
  const usr = $('#loginform').usr;
  if (document.activeElement !== usr) usr.value = st.wui_usr;
}
async function saveSetting(change) {
  try { st = await postJson('/settings', change); renderSettings(); toast('Saved.'); } catch (e) { fail(e); loadSettings(); }
}

// ── NVS editor ───────────────────────────────────────────────────────────────────────────
const INTS = new Set(['u8', 'i8', 'u16', 'i16', 'u32', 'i32', 'u64', 'i64']);
let nvsData = null;
async function openNvs() {
  const d = $('#nvs'), body = $('#nvs-b');
  body.innerHTML = '<p class="dim">loading</p>';
  if (!d.open) d.showModal();
  try { nvsData = await getJson('/nvs'); } catch (e) { d.close(); return fail(e); }
  body.innerHTML = Object.entries(nvsData).map(([ns, fields], n) =>
    `<section><h4>${esc(ns)}${ns === 'launcher' ? '' : `<button class="ab del" data-ns="${esc(ns)}" title="Erase this namespace" aria-label="Erase ${esc(ns)}">${ico('trash')}</button>`}</h4>` +
    fields.map((f, i) => {
      const id = `n${n}-${i}`;
      const control = f.t === 'u8' && (f.v === 0 || f.v === 1) ? `<input type="checkbox" class="sw" id="${id}"${f.v ? ' checked' : ''}>`
        : INTS.has(f.t) ? `<input type="number" id="${id}" value="${esc(f.v)}">`
        : f.t === 'str' ? `<input id="${id}" value="${esc(f.v)}" spellcheck="false">`
        : `<span class="dim">${f.t === 'blob' ? f.sz + ' bytes' : 'not editable'}</span>`;
      return `<div class="kv"><label for="${id}">${esc(f.k)}</label>${control}<small>${esc(f.t)}</small></div>`;
    }).join('') + '</section>').join('') || '<p class="dim">Nothing stored.</p>';
}
async function saveNvs() {
  const out = {};
  Object.entries(nvsData || {}).forEach(([ns, fields], n) => {
    out[ns] = fields.map((f, i) => {
      const el = $(`#n${n}-${i}`);
      return el && { k: f.k, t: f.t, v: el.type === 'checkbox' ? +el.checked : INTS.has(f.t) ? parseInt(el.value, 10) : el.value };
    }).filter(Boolean);
  });
  try {
    const r = await api('/nvs', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(out) });
    if (r !== 'OK') throw new Error(r);
    toast('NVS saved.');
    $('#nvs').close();
  } catch (e) { fail(e); }
}

async function eraseBle() {
  if (!await ask({ title: 'Erase all BLE bonds?', text: 'Every paired Bluetooth device has to be paired again.', ok: 'erase', danger: true })) return;
  try { toast(await api('/blebonds', { method: 'POST' })); } catch (e) { fail(e); }
}
async function sdPins() {
  const v = await ask({ title: 'SD card pins', fields: ['miso', 'mosi', 'sck', 'cs'].map(n => ({ name: n, label: n, type: 'number' })), ok: 'save' });
  if (v) try { toast(await api('/sdpins?' + new URLSearchParams(v))); } catch (e) { fail(e); }
}

// ── Wiring ───────────────────────────────────────────────────────────────────────────────
const ACTIONS = {
  reboot: () => reboot(), restart: () => reboot(true), reload: () => location.reload(),
  up: () => ls(parentOf(cwd)), ls: () => ls(), mkdir, mkfile,
  otago: installBin, otacancel: () => { otaPending = null; $('#ota').innerHTML = ''; },
  edsave: edSave, edclose: edClose, nvs: openNvs, nvssave: saveNvs, nvsclose: () => $('#nvs').close(),
  ble: eraseBle, sdpins: sdPins,
};
document.addEventListener('click', e => {
  if (!e.target.closest('#pwr')) $('#pmenu').hidden = true;
  const act = e.target.closest('[data-act]');
  if (act) return ACTIONS[act.dataset.act]?.();
  const boot = e.target.closest('[data-boot]');
  if (boot) bootApp(boot.dataset.boot, boot.dataset.name);
});
$('#pwr').onclick = () => {
  const m = $('#pmenu');
  m.hidden = !m.hidden;
  $('#pwr').setAttribute('aria-expanded', !m.hidden);
};

$('#flist').addEventListener('click', e => {
  const tr = e.target.closest('tr'), link = e.target.closest('a');
  if (link) e.preventDefault();
  if (!tr) return;
  if (tr.hasAttribute('data-up')) return link && ls(parentOf(cwd));
  const entry = entries[tr.dataset.i];
  if (!entry) return;
  const btn = e.target.closest('[data-a]');
  if (btn) return fileAction(btn.dataset.a, entry);
  if (link) entry.dir ? ls(entry.path) : edit(entry.path);
});
$('#crumbs').addEventListener('click', e => {
  const a = e.target.closest('[data-dir]');
  if (a) { e.preventDefault(); ls(a.dataset.dir); }
});
$('#filter').addEventListener('input', renderFiles);
$('#filter').addEventListener('keydown', e => { if (e.key === 'Escape') { e.target.value = ''; renderFiles(); e.target.blur(); } });
for (const id of ['#up-files', '#up-dir']) {
  $(id).addEventListener('change', e => { const files = [...e.target.files]; e.target.value = ''; uploadFiles(files); });
}

// Dropping files anywhere else must not open them in the browser.
addEventListener('dragover', e => e.preventDefault());
addEventListener('drop', e => e.preventDefault());
const fm = $('#fm');
const hasFiles = e => [...e.dataTransfer.types].includes('Files');
fm.addEventListener('dragover', e => { if (!hasFiles(e)) return; e.preventDefault(); fm.classList.add('drag'); $('#cover-dir').textContent = cwd; });
fm.addEventListener('dragleave', e => { if (!fm.contains(e.relatedTarget)) fm.classList.remove('drag'); });
fm.addEventListener('drop', async e => {
  e.preventDefault();
  fm.classList.remove('drag');
  const files = [];
  await Promise.all([...e.dataTransfer.items].map(i => i.webkitGetAsEntry && i.webkitGetAsEntry()).filter(Boolean).map(en => walk(en, '', files)));
  uploadFiles(files);
});
const drop = $('#ota-drop');
$('#ota-file').addEventListener('change', e => { pickBin(e.target.files[0]); e.target.value = ''; });
drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('drag'); });
drop.addEventListener('dragleave', () => drop.classList.remove('drag'));
drop.addEventListener('drop', e => { e.preventDefault(); drop.classList.remove('drag'); pickBin(e.dataTransfer.files[0]); });

$('[data-p=partitions]').addEventListener('click', e => {
  const btn = e.target.closest('[data-pm],[data-a]');
  if (btn) partAction(btn.dataset.pm || btn.dataset.a, btn, pm.entries[btn.closest('tr')?.dataset.i]);
});

$('#s-bright').addEventListener('input', e => { $('#s-bright-v').textContent = e.target.value + '%'; });
$('#s-bright').addEventListener('change', e => saveSetting({ bright: +e.target.value }));
$('#s-dim').addEventListener('change', e => saveSetting({ dimmerSet: +e.target.value }));
$('#s-boot').addEventListener('change', e => saveSetting({ bootTimer: +e.target.value }));
$('#themes').addEventListener('click', e => {
  const b = e.target.closest('[data-th]');
  if (!b) return;
  const [, FGCOLOR, BGCOLOR, ALCOLOR, odd, even] = THEMES[b.dataset.th];
  saveSetting({ FGCOLOR, BGCOLOR, ALCOLOR, odd, even });
});
$('#toggles').addEventListener('change', e => {
  const c = e.target;
  saveSetting({ [c.dataset.k]: c.dataset.inv ? !c.checked : c.checked });
});
$('#nets').addEventListener('click', async e => {
  const b = e.target.closest('[data-forget]');
  if (!b) return;
  const ssid = b.dataset.forget;
  if (!await ask({ title: `Forget ${ssid}?`, text: `The ${dev} won't join it again until it is saved again.`, ok: 'forget', danger: true })) return;
  try { toast(await post('/wifi', { forget: ssid })); loadSettings(); } catch (err) { fail(err); }
});
const netForm = $('#netform');
netForm.open.addEventListener('change', () => { netForm.pwd.disabled = netForm.open.checked; });
netForm.addEventListener('submit', async e => {
  e.preventDefault();
  if (!netForm.open.checked && !netForm.pwd.value) return toast('Type the password, or tick open network.', true);
  try {
    toast(await post('/wifi', { ssid: netForm.ssid.value.trim(), pwd: netForm.open.checked ? '' : netForm.pwd.value }));
    netForm.reset();
    netForm.pwd.disabled = false;
    loadSettings();
  } catch (err) { fail(err); }
});
const loginForm = $('#loginform');
loginForm.addEventListener('submit', async e => {
  e.preventDefault();
  if (loginForm.pwd.value !== loginForm.pwd2.value) return toast("The two passwords don't match.", true);
  try {
    toast(await post('/wifi', { usr: loginForm.usr.value.trim(), pwd: loginForm.pwd.value }));
    loginForm.pwd.value = loginForm.pwd2.value = '';
    loadSettings();
  } catch (err) { fail(err); }
});
$('#nvs-b').addEventListener('click', async e => {
  const b = e.target.closest('[data-ns]');
  if (!b) return;
  const ns = b.dataset.ns;
  if (!await ask({ title: `Erase the ${ns} namespace?`, text: 'Everything stored in it is erased. This cannot be undone.', ok: 'erase', danger: true })) return;
  try {
    const r = await api('/nvs?ns=' + encodeURIComponent(ns), { method: 'DELETE' });
    if (r !== 'OK') throw new Error(r);
    toast(`Erased ${ns}.`);
    openNvs();
  } catch (err) { fail(err); }
});

const editor = $('#ed-x');
editor.addEventListener('input', edState);
editor.addEventListener('keydown', e => {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); edSave(); }
  else if (e.key === 'Tab' && !e.shiftKey) { e.preventDefault(); editor.setRangeText('\t', editor.selectionStart, editor.selectionEnd, 'end'); edState(); }
});
$('#ed').addEventListener('cancel', e => { e.preventDefault(); edClose(); });
for (const d of $$('dialog')) d.addEventListener('close', () => toastHost().append($('#toasts')));

tick();
setInterval(tick, 10000);
route();
status();
setInterval(() => { if (!document.hidden) status(); }, 30000);
