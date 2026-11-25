'use strict';
// Launcher web portal. Talks to the device's web server (src/webInterface.cpp); the
// development copy of that server is webUi/backend/server.js.

const $ = s => document.querySelector(s);
const $$ = s => [...document.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
const ico = n => `<svg class="i"><use href="#i-${n}"/></svg>`;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const hex = n => '0x' + n.toString(16).toUpperCase();
const kb = n => n >= 1073741824 ? +(n / 1073741824).toFixed(1) + ' GB' : n >= 1048576 ? +(n / 1048576).toFixed(1) + ' MB' : n >= 1024 ? Math.round(n / 1024) + ' KB' : n + ' B';
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
    return label + `<input id="${id}" type="${f.type || 'text'}" value="${esc(f.value)}"${f.max ? ` maxlength="${f.max}"` : ''} required spellcheck="false" autocomplete="off">`;
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

// ── Sections: the bar's numbered workspaces, also keys 1-6 ─────────────────────────────
// 6 chat exists only on a device built with Grok (info.CHAT).
const PAGES = ['overview', 'apps', 'files', 'partitions', 'settings', 'chat'];
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
  if (page === 'chat') chatShow();
}
addEventListener('hashchange', route);
addEventListener('keydown', e => {
  if (e.key === 'Escape') $('#pmenu').hidden = true;
  if (e.ctrlKey || e.metaKey || e.altKey || e.target.closest('input,select,textarea,dialog')) return;
  const n = '123456'.indexOf(e.key);
  if (n >= 0 && (PAGES[n] !== 'chat' || info?.CHAT)) location.hash = PAGES[n];
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
  $('#ws-chat').hidden = !info.CHAT;
  if (!info.CHAT && page === 'chat') location.hash = 'overview';
  if (page === 'chat' && chat && !chat.items.length) chatRender(); // its welcome names the device
  renderBar();
  renderOverview();
  renderApps();
  renderSd();
  $('#sdpins').hidden = !info.HEADLESS;
  if (filesLoaded && info.KEYS && !document.querySelector('#flist [data-a=key]')) renderFiles(); // adds the key buttons
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
  // Flash is where installed apps live; its free ranges come from the partition table.
  const flash = pm && pm.flashSize, flashFree = pm ? (pm.freeRanges || []).reduce((n, r) => n + r.size, 0) : 0;
  const rows = [
    ['launcher', info.VERSION],
    ['device', info.DEVICE],
    ['time', 'CLOCK' in info ? info.CLOCK || 'not set (5 settings → clock)' : ''],
    ['uptime', info.UPTIME != null ? dur(info.UPTIME) : ''],
    ['wifi', w.ap ? `access point · ${w.ip}` : [w.ssid, w.ip, w.rssi ? w.rssi + ' dBm' : ''].filter(Boolean).join(' · ')],
    ['flash', flash ? `${kb(flashFree)} free for apps, of ${kb(flash)}` : ''],
    ['ram', info.HEAP ? `${kb(info.HEAP)} free of ${kb(info.HEAP_TOTAL)}` : ''],
    ['sd card', sd.totalBytes ? `${sd.used} used of ${sd.total}` : 'none'],
    ['battery', info.BATTERY > 0 ? info.BATTERY + '%' + (info.CHARGING ? ', charging' : '') : ''],
  ].filter(r => r[1]);
  // fastfetch's layout: user@host, a rule, the facts, then the theme's colours.
  $('#ff').innerHTML = `<div class="who"><b>launcher</b>@<b>${esc(name)}</b></div><div class="sep">${'-'.repeat(9 + name.length)}</div>` +
    '<dl>' + rows.map(([k, v]) => `<dt>${k}</dt><dd>${esc(v)}</dd>`).join('') + '</dl>' +
    '<div class="swatch">' + ['ff3b6b', '00ff9c', 'e6ff5a', '20b8ff', 'ff2bd6', '00e5ff', 'b6f5d2', '3f7d64'].map(c => `<i style="background:#${c}"></i>`).join('') + '</div>';
  $('#res').innerHTML =
    (flash ? meter('flash', 1 - flashFree / flash, `${kb(flash - flashFree)} / ${kb(flash)}`) : '') +
    meter('sd card', sd.usedBytes / sd.totalBytes, sd.totalBytes ? `${sd.used} / ${sd.total}` : 'no card') +
    (info.HEAP_TOTAL ? meter('ram', 1 - info.HEAP / info.HEAP_TOTAL, `${kb(info.HEAP_TOTAL - info.HEAP)} / ${kb(info.HEAP_TOTAL)}`) : '') +
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
  loadParts(); // the install took flash
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
        (bin ? rowBtn('install', 'Install on the device') : '') + (bin && info && info.KEYS ? rowBtn('key', 'Bind to a key') : '') +
        (text ? rowBtn('edit', 'Edit') : '') + (e.dir ? '' : rowBtn('download', 'Download')) +
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
    if (act === 'key') {
      // Like the device's SD browser "Bind to key": that key in the main menu installs and starts it.
      const v = await ask({ title: `Bind ${e.name} to a key`, text: "Pressing that key in the Launcher's main menu then installs and starts it.", fields: [{ name: 'k', label: 'key', max: 1 }], ok: 'bind' });
      if (!v) return;
      st = await postJson('/settings', { bind: { key: v.k, path: e.path } });
      return toast(`'${v.k.toLowerCase()}' now starts ${e.name}`);
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
  try {
    pm = await getJson('/partitions');
    renderParts();
    if (info) renderOverview();
  } catch (e) { fail(e); }
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
  ['dev', false, 'Developer mode', 'More on the device: Boot Animation in Settings, Wipe Flash memory in the partition manager.'],
];
const rgb = c => `rgb(${(c >> 11 & 31) * 255 / 31 | 0},${(c >> 5 & 63) * 255 / 63 | 0},${(c & 31) * 255 / 31 | 0})`;

// Time zones for the device's clock: [label, POSIX TZ string (what the device runs on), the
// browser time zones that use it]. "Set from this computer" picks by the browser's zone.
const ZONES = [
  ['Honolulu (Hawaii)', 'HST10', 'Pacific/Honolulu'],
  ['Anchorage (Alaska)', 'AKST9AKDT,M3.2.0,M11.1.0', 'America/Anchorage'],
  ['Los Angeles (Pacific)', 'PST8PDT,M3.2.0,M11.1.0', 'America/Los_Angeles America/Vancouver America/Tijuana'],
  ['Phoenix (Arizona)', 'MST7', 'America/Phoenix'],
  ['Denver (Mountain)', 'MST7MDT,M3.2.0,M11.1.0', 'America/Denver America/Edmonton America/Boise'],
  ['Chicago (Central)', 'CST6CDT,M3.2.0,M11.1.0', 'America/Chicago America/Winnipeg'],
  ['Mexico City', 'CST6', 'America/Mexico_City'],
  ['New York (Eastern)', 'EST5EDT,M3.2.0,M11.1.0', 'America/New_York America/Toronto America/Detroit'],
  ['Halifax (Atlantic)', 'AST4ADT,M3.2.0,M11.1.0', 'America/Halifax'],
  ["St. John's (Newfoundland)", 'NST3:30NDT,M3.2.0,M11.1.0', 'America/St_Johns'],
  ['Sao Paulo', '<-03>3', 'America/Sao_Paulo'],
  ['Buenos Aires', '<-03>3', 'America/Argentina/Buenos_Aires'],
  ['UTC', 'UTC0', 'UTC Etc/UTC'],
  ['London, Dublin', 'GMT0BST,M3.5.0/1,M10.5.0', 'Europe/London Europe/Dublin'],
  ['Lisbon', 'WET0WEST,M3.5.0/1,M10.5.0', 'Europe/Lisbon'],
  ['Paris, Berlin, Madrid, Rome', 'CET-1CEST,M3.5.0,M10.5.0/3',
    'Europe/Paris Europe/Berlin Europe/Madrid Europe/Rome Europe/Amsterdam Europe/Brussels Europe/Vienna Europe/Zurich Europe/Stockholm Europe/Oslo Europe/Copenhagen Europe/Warsaw Europe/Prague Europe/Budapest'],
  ['Athens, Helsinki, Kyiv', 'EET-2EEST,M3.5.0/3,M10.5.0/4', 'Europe/Athens Europe/Helsinki Europe/Kyiv Europe/Kiev Europe/Bucharest Europe/Sofia'],
  ['Istanbul', '<+03>-3', 'Europe/Istanbul'],
  ['Moscow', 'MSK-3', 'Europe/Moscow'],
  ['Johannesburg', 'SAST-2', 'Africa/Johannesburg'],
  ['Nairobi', 'EAT-3', 'Africa/Nairobi'],
  ['Dubai', '<+04>-4', 'Asia/Dubai'],
  ['Tehran', '<+0330>-3:30', 'Asia/Tehran'],
  ['Karachi', 'PKT-5', 'Asia/Karachi'],
  ['India', 'IST-5:30', 'Asia/Kolkata Asia/Calcutta'],
  ['Kathmandu', '<+0545>-5:45', 'Asia/Kathmandu'],
  ['Dhaka', '<+06>-6', 'Asia/Dhaka'],
  ['Bangkok, Jakarta', '<+07>-7', 'Asia/Bangkok Asia/Jakarta Asia/Ho_Chi_Minh'],
  ['Singapore', '<+08>-8', 'Asia/Singapore Asia/Kuala_Lumpur'],
  ['Beijing, Shanghai', 'CST-8', 'Asia/Shanghai'],
  ['Perth', 'AWST-8', 'Australia/Perth'],
  ['Tokyo', 'JST-9', 'Asia/Tokyo'],
  ['Seoul', 'KST-9', 'Asia/Seoul'],
  ['Adelaide', 'ACST-9:30ACDT,M10.1.0,M4.1.0/3', 'Australia/Adelaide'],
  ['Brisbane', 'AEST-10', 'Australia/Brisbane'],
  ['Sydney, Melbourne', 'AEST-10AEDT,M10.1.0,M4.1.0/3', 'Australia/Sydney Australia/Melbourne Australia/Hobart'],
  ['Auckland', 'NZST-12NZDT,M9.5.0,M4.1.0/3', 'Pacific/Auckland'],
];
// The browser's zone, if listed; otherwise its current offset with no daylight saving.
function computerZone() {
  const iana = Intl.DateTimeFormat().resolvedOptions().timeZone || '';
  const listed = ZONES.find(z => z[2].split(' ').includes(iana));
  if (listed) return { tz: listed[1], fixed: false, iana };
  const east = -new Date().getTimezoneOffset(), a = Math.abs(east), h = a / 60 | 0, m = a % 60;
  if (!east) return { tz: 'UTC0', fixed: true, iana };
  const pad = n => String(n).padStart(2, '0');
  return { tz: `<${east > 0 ? '+' : '-'}${pad(h)}${m ? pad(m) : ''}>${east > 0 ? '-' : ''}${h}${m ? ':' + pad(m) : ''}`, fixed: true, iana };
}
async function syncClock() {
  const zone = computerZone();
  if (await saveSetting({ clock: { tz: zone.tz, time: Math.floor(Date.now() / 1000) } }) && zone.fixed) {
    toast(`${zone.iana || 'This computer\'s time zone'} isn't in the list, so a fixed offset with no daylight saving was used. Pick your zone from the list to get daylight saving.`);
  }
}
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

  // The device menu lists its mounting rotation twice (Default and its own kind); once here.
  const rots = (st.rotations || []).filter((r, i, all) => all.findIndex(x => x.value === r.value) === i);
  $('#s-rot').innerHTML = rots.map(r =>
    `<option value="${r.value}"${r.value === st.rotation ? ' selected' : ''}>${r.kind === 'Default' ? `default (${r.value})` : r.kind.toLowerCase() + ' ' + r.value}</option>`).join('');

  const host = $('#s-host');
  if (document.activeElement !== host) host.value = st.hostname || '';
  host.placeholder = st.hostAuto || '';
  $('#s-host-note').innerHTML = `Answers to <a href="http://${esc(st.hostNow)}.local/">http://${esc(st.hostNow)}.local</a>. Leave it empty for the automatic name, ${esc(st.hostAuto)}.`;

  const keys = st.shortcuts ? Object.entries(st.shortcuts) : null;
  $('#keyswin').hidden = !keys;
  if (keys) {
    $('#keys').innerHTML = keys.length
      ? keys.map(([k, path]) => `<li><span class="k">${esc(k)}</span><span>${esc(path)}</span><button class="ab del" data-unbind="${esc(k)}" title="Remove" aria-label="Remove the ${esc(k)} shortcut">${ico('trash')}</button></li>`).join('')
      : '<li class="empty">No shortcuts yet.</li>';
    $('#unbindall').hidden = !keys.length;
  }

  const g = st.grok;
  $('#grokwin').hidden = !g;
  if (g) {
    $('#g-duo').checked = g.duo;
    $('#g-muted').checked = g.muted;
    for (const [id, value] of [['#g-name', g.name], ['#g-persona', g.persona]]) if (document.activeElement !== $(id)) $(id).value = value;
    $('#g-voice').innerHTML = g.voices.map(v => `<option value="${esc(v)}"${v === g.voice ? ' selected' : ''}>${esc(v[0].toUpperCase() + v.slice(1))}</option>`).join('');
    $('#g-keystate').textContent = g.hasKey ? 'saved' : 'not set';
    $('#g-clear').hidden = !g.hasKey;
  }

  $('#a-saver').hidden = !st.saver;
  $('#a-charge').hidden = !st.charge;
  $('#a-anim').hidden = !st.dev;

  const c = st.clock;
  $('#clockwin').hidden = !c;
  if (c) {
    $('#c-now').textContent = c.local || 'not set';
    const listed = ZONES.some(z => z[1] === c.tz);
    $('#c-tz').innerHTML = ZONES.map(([label, tz]) => `<option value="${esc(tz)}"${tz === c.tz ? ' selected' : ''}>${esc(label)}</option>`).join('') +
      (listed ? '' : `<option value="${esc(c.tz)}" selected>custom: ${esc(c.tz)}</option>`) + '<option value="">other (type a POSIX time zone)</option>';
    $('#c-tzc').hidden = true;
    if (document.activeElement !== $('#c-date')) {
      const d = new Date(), pad = n => String(n).padStart(2, '0');
      $('#c-date').value = `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
      $('#c-time').value = `${pad(d.getHours())}:${pad(d.getMinutes())}`;
    }
  }
}
async function saveSetting(change) {
  try { st = await postJson('/settings', change); renderSettings(); toast('Saved.'); return true; } catch (e) { fail(e); loadSettings(); return false; }
}

// The Settings menu's device actions (actionHandler on the device).
async function deviceAction(what) {
  busy++; // formatting can take a while; nothing else is sent meanwhile
  try { toast(await post('/action', { do: what })); return true; } catch (e) { fail(e); return false; } finally { busy--; }
}
async function formatSd() {
  if (!await ask({ title: 'Format the SD card?', text: `Only a card the ${dev} can't read is formatted: a new card, or one formatted for a computer. A readable card is left as it is.`, ok: 'format', danger: true })) return;
  if (await deviceAction('formatsd')) { status(); if (filesLoaded) ls('/'); }
}
async function resetConfigs() {
  if (!await ask({ title: 'Reset configs and WiFi?', text: 'Every setting goes back to its default, saved WiFi networks are forgotten, the login becomes admin / launcher, the network name goes back to automatic and the xAI key is erased. The SD card\'s config file is backed up first.', ok: 'reset', danger: true })) return;
  if (await deviceAction('reset')) loadSettings();
}
async function turnOff() {
  if (!await ask({ title: `Turn off the ${dev}?`, text: 'This page goes offline. Turn it on with its power switch or G0 button.', ok: 'turn off', danger: true })) return;
  if (await deviceAction('off')) offline(`The ${dev} is off.`);
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
  screensaver: () => deviceAction('screensaver'), charge: () => deviceAction('charge'), bootanim: () => deviceAction('bootanim'),
  formatsd: formatSd, reset: resetConfigs, off: turnOff, clocksync: syncClock,
  unbindall: async () => { if (await ask({ title: 'Remove every shortcut?', ok: 'remove all', danger: true })) saveSetting({ unbindAll: true }); },
  xaiclear: async () => { if (await ask({ title: 'Remove the xAI key?', text: "Grok can't connect until a key is saved again.", ok: 'remove', danger: true })) saveSetting({ grok: { clearKey: true } }); },
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
$('#s-rot').addEventListener('change', e => saveSetting({ rotation: +e.target.value }));
$('#hostform').addEventListener('submit', e => {
  e.preventDefault();
  const name = $('#s-host').value.trim().toLowerCase();
  if (name && !/^[a-z0-9]([a-z0-9-]{0,30}[a-z0-9])?$/.test(name)) return toast('A name can only have letters, digits and hyphens, not at either end.', true);
  saveSetting({ hostname: name });
});
$('#keys').addEventListener('click', e => { const b = e.target.closest('[data-unbind]'); if (b) saveSetting({ unbind: b.dataset.unbind }); });
$('#c-tz').addEventListener('change', e => {
  if (e.target.value) return saveSetting({ clock: { tz: e.target.value } });
  $('#c-tzc').hidden = false; // "other": type one
  $('#c-tzc').focus();
});
$('#c-tzc').addEventListener('keydown', e => {
  if (e.key !== 'Enter') return;
  e.preventDefault();
  const tz = e.target.value.trim();
  if (!/^[A-Za-z0-9<>+\-:,./]{3,63}$/.test(tz)) return toast("That doesn't look like a POSIX time zone.", true);
  saveSetting({ clock: { tz } });
});
$('#clockform').addEventListener('submit', e => {
  e.preventDefault();
  saveSetting({ clock: { local: `${$('#c-date').value}T${$('#c-time').value}` } });
});
$('#g-duo').addEventListener('change', e => saveSetting({ grok: { duo: e.target.checked } }));
$('#g-muted').addEventListener('change', e => saveSetting({ grok: { muted: e.target.checked } }));
$('#grokform').addEventListener('submit', e => {
  e.preventDefault();
  saveSetting({ grok: { name: $('#g-name').value, persona: $('#g-persona').value, voice: $('#g-voice').value } });
});
$('#xaiform').addEventListener('submit', async e => {
  e.preventDefault();
  const key = $('#g-key').value.trim();
  if (!key) return toast('Paste the key first.', true);
  if (await saveSetting({ grok: { key } })) $('#g-key').value = '';
});
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

// ── 6 chat: Grok, with skills that use the Cardputer ─────────────────────────────────────
// Messages go to xAI's Responses API through the device (POST /chat), which adds the xAI key
// saved in Settings, so the key never reaches this page. xAI keeps each conversation for 30
// days, so a message carries only what is new (previous_response_id). Grok uses the device
// through SKILLS: reading runs at once, a change waits here for allow or deny, and the
// riskiest need a word typed. Passwords and keys never go to Grok. Chats are saved on the SD
// card, a file each in /chats, named by when they started and how they began.
const CHAT_DIR = '/chats';
const CHAT_MODEL = 'grok-4.7';  // xAI's recommended chat model
const CHAT_ROUNDS = 12;         // skill rounds for one message, so a confused reply can't go on forever
const USD_TICKS = 1e10;         // xAI gives the cost in ticks of a ten-billionth of a dollar
const CHAT_FILE = /^(\d{4})(\d\d)(\d\d)-(\d\d)(\d\d) (.+)\.json$/;
const CHAT_HELLO = ["What's the battery at, and which apps are installed?", 'Which .bin files on the SD card are not installed yet?', 'Turn the screen brightness down to 40%.'];
const CHAT_SWITCHES = { bootToApp: 'boot_to_launcher', autoConnect: 'auto_connect_wifi', onlyBins: 'show_all_files', noDotFiles: 'show_dotfiles', autoBackup: 'auto_backup', askSpiffs: 'ask_copy_spiffs', DDLB: 'deep_sleep_starts_launcher', dev: 'developer_mode' };
// Per-browser choices: the two toggles and the skills always allowed.
const prefs = {
  get(k, d) { try { const v = localStorage.getItem('chat.' + k); return v === null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem('chat.' + k, JSON.stringify(v)); } catch {} },
};
let chat = null, chatFiles = [], chatModels = null, chatGrok = {}, chatRunning = false, chatStop = null, chatPending = null, chatFrame = 0;
let chatSaving = Promise.resolve();
const newChat = () => ({ v: 1, file: '', created: Date.now(), model: chat?.model || CHAT_MODEL, last: null, pending: null, ticks: 0, tokens: 0, items: [] });
const chatPath = p => '/' + String(p ?? '').split('/').filter(s => s && s !== '.').join('/');
const jStr = description => ({ type: 'string', description });
const jInt = description => ({ type: 'integer', description });
const jObj = (properties = {}, required = []) => ({ type: 'object', properties, required });
class ChatError extends Error { constructor(status, message) { super(message); this.status = status; } }

// Settings as Grok sees them: plain names, no passwords, no keys.
function chatSettingsView(s) {
  const theme = THEMES.find(t => t[1] === s.FGCOLOR && t[2] === s.BGCOLOR), switches = {};
  for (const [k, inv] of TOGGLES) switches[CHAT_SWITCHES[k]] = inv ? !s[k] : !!s[k];
  return {
    brightness_percent: s.bright, dim_after_seconds: s.dimmerSet, boot_wait_seconds: s.bootTimer + 1,
    rotation: s.rotation, rotations: (s.rotations || []).map(r => r.value), network_name: s.hostname || s.hostAuto,
    theme: theme ? theme[0] : 'custom', themes: THEMES.map(t => t[0]), switches, key_shortcuts: s.shortcuts || {},
    wifi_networks: (s.wifi || []).map(n => ({ network: n.ssid, password_saved: !!n.pwd })),
    grok: s.grok ? { name: s.grok.name, personality: s.grok.persona, voice: s.grok.voice, voices: s.grok.voices, duo: s.grok.duo, start_muted: s.grok.muted, xai_key_saved: s.grok.hasKey } : null,
    clock: s.clock ? { local_time: s.clock.local, time_zone: s.clock.tz } : null,
  };
}
// Grok's change_settings arguments, as the device's /settings takes them.
function chatSettingsChange(a) {
  const c = {};
  if (a.brightness_percent != null) c.bright = Math.min(100, Math.max(5, Math.round(a.brightness_percent / 5) * 5));
  if (a.dim_after_seconds != null) c.dimmerSet = a.dim_after_seconds;
  if (a.boot_wait_seconds != null) c.bootTimer = Math.min(5, Math.max(1, a.boot_wait_seconds)) - 1;
  if (a.rotation != null) c.rotation = a.rotation;
  if (a.network_name != null) c.hostname = a.network_name;
  const t = THEMES.find(x => x[0] === a.theme);
  if (t) Object.assign(c, { FGCOLOR: t[1], BGCOLOR: t[2], ALCOLOR: t[3], odd: t[4], even: t[5] });
  for (const [k, inv] of TOGGLES) { const v = a.switches?.[CHAT_SWITCHES[k]]; if (typeof v === 'boolean') c[k] = inv ? !v : v; }
  if (a.time_zone) c.clock = { tz: a.time_zone };
  const g = {};
  for (const [from, to] of [['name', 'name'], ['personality', 'persona'], ['voice', 'voice'], ['duo', 'duo'], ['start_muted', 'muted']]) if (a.grok?.[from] != null) g[to] = a.grok[from];
  if (Object.keys(g).length) c.grok = g;
  return c;
}
function chatSettingsWhat(a) {
  const p = [], onOff = v => (v ? 'on' : 'off');
  if (a.brightness_percent != null) p.push(`brightness ${a.brightness_percent}%`);
  if (a.dim_after_seconds != null) p.push(a.dim_after_seconds ? `dim after ${a.dim_after_seconds} s` : 'never dim');
  if (a.boot_wait_seconds != null) p.push(`boot screen waits ${a.boot_wait_seconds} s`);
  if (a.rotation != null) p.push(`rotation ${a.rotation}`);
  if (a.network_name != null) p.push(a.network_name ? `network name ${a.network_name}` : 'automatic network name');
  if (a.theme) p.push(`theme ${a.theme}`);
  for (const [k, v] of Object.entries(a.switches || {})) p.push(`${k.replace(/_/g, ' ')} ${onOff(v)}`);
  if (a.time_zone) p.push(`time zone ${a.time_zone}`);
  for (const [k, v] of Object.entries(a.grok || {})) p.push(`Grok ${k.replace(/_/g, ' ')} ${typeof v === 'boolean' ? onOff(v) : v}`);
  return p.join(', ');
}
const chatPartWhat = a => a.action === 'create' ? `add a ${a.type || 'data'} partition ${a.label || ''}, ${kb(+a.size || 0)}`
  : a.action === 'discard' ? 'drop every staged change' : `${a.action} the partition at ${hex(+a.offset || 0)}${a.action === 'resize' ? ' to ' + kb(+a.size || 0) : ''}`;

// What Grok can do on the device. level: read runs at once; ask waits for allow or deny, and
// can be always allowed; confirm needs its word typed. run() returns a one-line summary for
// this page, the data Grok gets, and for skills that take the device offline, what to do
// once Grok has answered (then).
const SKILLS = {
  get_status: {
    level: 'read', about: "The Cardputer's battery, WiFi, SD card space, free memory, uptime, clock, Launcher version and installed apps.",
    async run() {
      const i = await getJson('/systeminfo'), w = i.WIFI || {}, sd = i.SD || {};
      return {
        summary: [i.BATTERY > 0 ? `battery ${i.BATTERY}%${i.CHARGING ? ', charging' : ''}` : '', w.ap ? 'on its own WiFi' : w.ssid ? `WiFi ${w.ssid}${w.rssi ? `, ${w.rssi} dBm` : ''}` : '', sd.totalBytes ? `SD card ${sd.free} free` : 'no SD card'].filter(Boolean).join(' · '),
        data: {
          device: i.DEVICE, launcher_version: i.VERSION, battery_percent: i.BATTERY > 0 ? i.BATTERY : null, charging: !!i.CHARGING,
          wifi: { network: w.ssid || null, signal_dbm: w.rssi || null, ip: w.ip || null, own_access_point: !!w.ap },
          sd_card: sd.totalBytes ? { size: sd.total, used: sd.used, free: sd.free } : null, free_memory_bytes: i.HEAP, uptime_seconds: i.UPTIME, clock: i.CLOCK ?? null,
          installed_apps: (i.APPS || []).map(a => ({ name: a.name, label: a.label })),
        },
      };
    },
  },
  list_files: {
    level: 'read', about: 'Lists a folder on the SD card: its folders, and its files with their sizes.', params: jObj({ path: jStr('The folder, like / or /apps') }, ['path']),
    async run(a) {
      const dir = chatPath(a.path), text = await api('/listfiles?folder=' + encodeURIComponent(dir)), entries = [];
      for (const line of text.split('\n')) {
        const t = line.slice(0, 2), at = line.lastIndexOf(':'), name = line.slice(3, at);
        if (t === 'Fo') entries.push({ name, folder: true });
        else if (t === 'Fi') entries.push({ name, size: line.slice(at + 1) });
      }
      const folders = entries.filter(x => x.folder).length, files = entries.length - folders;
      return { summary: `${dir} · ${folders} folder${folders === 1 ? '' : 's'}, ${files} file${files === 1 ? '' : 's'}`, data: { folder: dir, entries } };
    },
  },
  read_file: {
    level: 'read', about: 'Reads a text file on the SD card, up to 32 KB of it.', params: jObj({ path: jStr('The file') }, ['path']),
    async run(a) {
      const p = chatPath(a.path);
      let r;
      try { r = await fetch('/editfile?name=' + encodeURIComponent(p)); } catch { throw new Error(`The ${dev} did not answer.`); }
      if (!r.ok) throw new Error((await r.text()) || r.statusText);
      // Stops after 32 KB, so asking for a big app file doesn't bring all of it here.
      const reader = r.body.getReader(), parts = [];
      let size = 0;
      while (size <= MAX_EDIT) { const { value, done } = await reader.read(); if (done) break; parts.push(value); size += value.length; }
      reader.cancel().catch(() => {});
      const bytes = new Uint8Array(size);
      parts.reduce((at, part) => (bytes.set(part, at), at + part.length), 0);
      if (bytes.includes(0)) throw new Error('not a text file');
      const cut = size > MAX_EDIT;
      return { summary: `${p} · ${cut ? 'the first 32 KB' : kb(size)}`, data: { path: p, text: new TextDecoder().decode(bytes.subarray(0, MAX_EDIT)), cut } };
    },
  },
  write_file: {
    level: 'ask', verb: 'write a file', about: 'Creates or replaces a text file on the SD card, up to 32 KB.', params: jObj({ path: jStr('The file'), content: jStr('The whole text of the file') }, ['path', 'content']),
    what: a => `${chatPath(a.path)}, ${kb(new Blob([a.content ?? '']).size)}`,
    note: a => String(a.content ?? '').split('\n').slice(0, 4).join(' / ').slice(0, 200),
    async run(a) {
      const p = chatPath(a.path), c = String(a.content ?? '');
      if (new Blob([c]).size > MAX_EDIT) throw new Error('too big: 32 KB at most');
      const r = await api('/editfile?name=' + encodeURIComponent(p), { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: c });
      if (r !== 'OK') throw new Error(r || 'could not save');
      return { summary: 'saved ' + p };
    },
  },
  make_folder: {
    level: 'ask', verb: 'make a folder', about: 'Creates a folder on the SD card.', params: jObj({ path: jStr('The new folder') }, ['path']), what: a => chatPath(a.path),
    run: async a => ({ summary: await api(`/file?name=${encodeURIComponent(chatPath(a.path))}&action=create`) }),
  },
  rename: {
    level: 'ask', verb: 'rename', about: 'Renames a file or folder on the SD card. It stays in the same folder.', params: jObj({ path: jStr('What to rename'), new_name: jStr('Its new name, without a folder') }, ['path', 'new_name']),
    what: a => `${chatPath(a.path)} to ${a.new_name}`, run: async a => ({ summary: await post('/rename', { filePath: chatPath(a.path), fileName: String(a.new_name) }) }),
  },
  delete: {
    level: 'ask', verb: 'delete', about: 'Deletes a file, or a folder with everything in it, from the SD card.', params: jObj({ path: jStr('What to delete') }, ['path']),
    what: a => chatPath(a.path), note: () => 'This cannot be undone.', run: async a => ({ summary: await api(`/file?name=${encodeURIComponent(chatPath(a.path))}&action=delete`) }),
  },
  get_settings: {
    level: 'read', about: "The Launcher's settings: screen, theme, on/off switches, key shortcuts, saved WiFi networks (names only), Grok's voice settings and the clock.",
    run: async () => ({ summary: 'read the settings', data: chatSettingsView(await getJson('/settings')) }),
  },
  change_settings: {
    level: 'ask', verb: 'change settings', about: 'Changes Launcher settings. Give only the ones to change.',
    params: jObj({
      brightness_percent: { type: 'integer', minimum: 5, maximum: 100, description: 'Screen brightness, in steps of 5' },
      dim_after_seconds: { type: 'integer', enum: DIM_TIMES.map(d => d[0]), description: 'Dim the screen after this long without a key press; 0 for never' },
      boot_wait_seconds: { type: 'integer', minimum: 1, maximum: 5, description: 'How long the boot screen waits for a key' },
      rotation: jInt('Screen rotation: one of the values get_settings lists'),
      network_name: jStr('The name the portal answers to, like cardputer for http://cardputer.local; empty for automatic'),
      theme: { type: 'string', enum: THEMES.map(t => t[0]), description: 'Colour theme' },
      switches: { type: 'object', description: 'On/off settings to change', properties: Object.fromEntries(Object.values(CHAT_SWITCHES).map(n => [n, { type: 'boolean' }])) },
      time_zone: jStr('POSIX time zone, like PST8PDT,M3.2.0,M11.1.0'),
      grok: { type: 'object', description: "Grok's voice settings on the Cardputer", properties: { name: jStr('Its name'), personality: jStr('Its personality, in a few words'), voice: jStr('One of the voices get_settings lists'), duo: { type: 'boolean', description: 'Duo mode, for talking with another Grok' }, start_muted: { type: 'boolean' } } },
    }),
    what: chatSettingsWhat,
    async run(a) {
      const c = chatSettingsChange(a);
      if (!Object.keys(c).length) throw new Error('nothing to change');
      st = await postJson('/settings', c);
      return { summary: chatSettingsWhat(a), data: { ok: true, settings: chatSettingsView(st) } };
    },
  },
  set_shortcut: {
    level: 'ask', verb: 'set a key shortcut', about: "Makes a key in the Launcher's main menu install and start a .bin on the SD card.", params: jObj({ key: jStr('One key, like a'), path: jStr('A .bin file on the SD card') }, ['key', 'path']),
    what: a => `'${a.key}' installs and starts ${chatPath(a.path)}`,
    async run(a) { st = await postJson('/settings', { bind: { key: String(a.key), path: chatPath(a.path) } }); return { summary: `'${a.key}' set` }; },
  },
  remove_shortcut: {
    level: 'ask', verb: 'remove a key shortcut', about: 'Removes a key shortcut.', params: jObj({ key: jStr('The key') }, ['key']), what: a => `'${a.key}'`,
    async run(a) { st = await postJson('/settings', { unbind: String(a.key) }); return { summary: `'${a.key}' removed` }; },
  },
  forget_wifi: {
    level: 'ask', verb: 'forget a WiFi network', about: 'Forgets a saved WiFi network.', params: jObj({ network: jStr("The network's name") }, ['network']), what: a => a.network,
    run: async a => ({ summary: await post('/wifi', { forget: String(a.network) }) }),
  },
  add_wifi: {
    level: 'ask', verb: 'add a WiFi network', secret: 'its password, sent only to the Cardputer',
    about: 'Saves a WiFi network for the Cardputer to join. The user types its password on this page, so never ask for it.',
    params: jObj({ network: jStr("The network's name"), open: { type: 'boolean', description: 'True for a network without a password' } }, ['network']),
    what: a => a.network + (a.open ? ', without a password' : ''),
    run: async (a, x) => ({ summary: await post('/wifi', { ssid: String(a.network), pwd: a.open ? '' : x.secret || '' }) }),
  },
  install_app: {
    level: 'ask', verb: 'install an app', about: 'Installs a firmware .bin from the SD card into free flash space. The Cardputer shows the progress on its screen.', params: jObj({ path: jStr('The .bin file on the SD card') }, ['path']),
    what: a => chatPath(a.path), run: async a => ({ summary: await post('/UPDATE', { fileName: chatPath(a.path) }) }),
  },
  start_app: {
    level: 'ask', verb: 'start an app', about: 'Starts an installed app. The Cardputer leaves the Launcher, so this chat goes offline.', params: jObj({ label: jStr("The app's label, from get_status") }, ['label']),
    what: a => a.label, note: () => 'The Cardputer leaves the Launcher, and this chat goes offline.',
    run: async a => ({
      summary: 'starting ' + a.label,
      then: async () => { await api('/bootapp', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'label=' + encodeURIComponent(a.label) }); offline(`Starting ${a.label}.`); },
    }),
  },
  restart: {
    level: 'ask', verb: 'restart the Cardputer', about: 'Restarts the Cardputer into its menu, so this chat goes offline until Web UI is started on it again.',
    note: () => 'This chat goes offline until Web UI is started on the Cardputer again.',
    run: async () => ({ summary: 'restarting', then: async () => { await api('/reboot'); offline(`The ${dev} is restarting.`); } }),
  },
  turn_off: {
    level: 'ask', verb: 'turn the Cardputer off', about: 'Turns the Cardputer off, so this chat goes offline.', note: () => 'This chat goes offline. It turns on again with its power switch or G0 button.',
    run: async () => ({ summary: 'turning off', then: async () => { await post('/action', { do: 'off' }); offline(`The ${dev} is off.`); } }),
  },
  start_screensaver: {
    level: 'ask', verb: 'start the screensaver', about: 'Plays the Omarchy screensaver on the Cardputer until a key is pressed on it.',
    run: async () => ({ summary: await post('/action', { do: 'screensaver' }) }),
  },
  get_partitions: {
    level: 'read', about: "The Cardputer's flash partition table, with any changes staged but not yet written.",
    async run() { const t = await getJson('/partitions'); return { summary: `${(t.entries || []).length} partitions${t.dirty ? ', changes staged' : ''}`, data: t }; },
  },
  edit_partitions: {
    level: 'ask', verb: 'stage a partition change', about: 'Stages one change to the partition table. Nothing is written until apply_partitions. Offsets and sizes are in bytes.',
    params: jObj({
      action: { type: 'string', enum: ['create', 'delete', 'resize', 'format', 'discard'], description: 'discard drops every staged change' },
      offset: jInt('The partition, by its offset: for delete, resize and format'), size: jInt('Size in bytes: for create and resize'),
      label: jStr('Label: for create'), type: { type: 'string', enum: ['app', 'data'], description: 'For create' }, subtype: jStr('For create: ota for an app; fat, spiffs or littlefs for data'),
    }, ['action']),
    what: chatPartWhat, note: () => 'Nothing is written until the table is applied.',
    async run(a) {
      const f = { action: a.action };
      for (const k of ['offset', 'size', 'label', 'type', 'subtype']) if (a[k] != null) f[k] = a[k];
      await post('/partitions', f);
      return { summary: chatPartWhat(a) + ', staged' };
    },
  },
  apply_partitions: {
    level: 'confirm', word: 'apply', verb: 'write the partition table', about: 'Writes the staged partition table. Apps in removed or moved partitions are lost, and the Cardputer restarts, so this chat goes offline.',
    note: () => 'Apps in removed or moved partitions are lost. The Cardputer restarts, and this chat goes offline.',
    run: async () => ({ summary: 'writing', then: async () => { await post('/partitions', { action: 'apply' }); offline('Writing the partition table and restarting.'); } }),
  },
  format_sd: {
    level: 'confirm', word: 'format', verb: 'format the SD card', about: "Formats the SD card, only if the Cardputer can't read it: a new card, or one formatted for a computer. A card it can read is left as it is.",
    note: () => "Only a card the Cardputer can't read is formatted.",
    async run() { busy++; try { return { summary: await post('/action', { do: 'formatsd' }) }; } finally { busy--; } },
  },
  reset_settings: {
    level: 'confirm', word: 'reset', verb: 'reset every Launcher setting', about: 'Puts every Launcher setting back to its default, forgets the saved WiFi networks and erases the xAI key, so this chat stops working until a key is saved again.',
    note: () => 'Saved WiFi networks are forgotten and the xAI key is erased, so this chat stops working until a key is saved again.',
    run: async () => ({ summary: await post('/action', { do: 'reset' }) }),
  },
};

function chatInstructions() {
  const name = chatGrok.name || 'Grok', zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
  return [
    `You are ${name}, the Grok on the user's ${dev}: an M5Stack Cardputer, a pocket ESP32-S3 computer with a 240x135 screen, a keyboard and an SD card. It runs the Omarchy Launcher, which installs and starts apps (firmware .bin files) from its SD card. The user is chatting with you in the Launcher's web portal, in their browser.`,
    chatGrok.persona ? `Your personality: ${chatGrok.persona}` : '',
    `For the user it is ${new Date().toLocaleString(undefined, { dateStyle: 'full', timeStyle: 'short' })}${zone ? ` (${zone})` : ''}.`,
    prefs.get('skills', true)
      ? "You can check and control the Cardputer with your functions; use them instead of guessing. Each function that changes something is shown to the user, who allows or denies it. If one is denied, don't try it again: ask what they want instead. Starting an app, restarting, turning off or writing the partition table takes the Cardputer and this chat offline until Web UI is started on it again, so say that before you do it. Never ask for passwords or keys: when you add a WiFi network the user types its password, and the xAI key and the portal's login are changed in Settings."
      : "The user turned your device skills off for this chat, so you can't check or change the Cardputer.",
    'Answer briefly: this is a chat.',
  ].filter(Boolean).join('\n');
}
function chatTools() {
  const tools = prefs.get('web', true) ? [{ type: 'web_search' }] : [];
  if (prefs.get('skills', true)) for (const [name, s] of Object.entries(SKILLS)) tools.push({ type: 'function', name, description: s.about, parameters: s.params || jObj() });
  return tools;
}

// A safe subset of Markdown: everything is escaped first.
function chatMd(s) {
  const inline = t => esc(t)
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>')
    .replace(/(^|[\s(])\*([^*\s][^*]*)\*(?=[\s).,!?:;]|$)/g, '$1<i>$2</i>')
    .replace(/\[\[(\d+)\]\]\((https?:\/\/[^\s)]+)\)/g, '<a class="ch-cite" href="$2" target="_blank" rel="noopener noreferrer">[$1]</a>') // xAI's sources
    .replace(/\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)/g, '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>');
  const lines = String(s ?? '').split('\n'), out = [], list = /^\s*([-*+•]|\d+[.)])\s+/, fence = /^\s*```/, head = /^#{1,6}\s/;
  for (let i = 0; i < lines.length;) {
    const l = lines[i];
    if (fence.test(l)) {
      const code = [];
      for (i++; i < lines.length && !fence.test(lines[i]); i++) code.push(lines[i]);
      i++;
      out.push(`<pre><code>${esc(code.join('\n'))}</code></pre>`);
    } else if (list.test(l)) {
      const ordered = /^\s*\d/.test(l), items = [];
      for (; i < lines.length && list.test(lines[i]); i++) items.push(`<li>${inline(lines[i].replace(list, ''))}</li>`);
      out.push(ordered ? `<ol>${items.join('')}</ol>` : `<ul>${items.join('')}</ul>`);
    } else if (head.test(l)) {
      out.push(`<p><b>${inline(l.replace(/^#+\s*/, ''))}</b></p>`);
      i++;
    } else if (!l.trim()) {
      i++;
    } else {
      const para = [];
      for (; i < lines.length && lines[i].trim() && !fence.test(lines[i]) && !list.test(lines[i]) && !head.test(lines[i]); i++) para.push(inline(lines[i]));
      out.push(`<p>${para.join('<br>')}</p>`);
    }
  }
  return out.join('');
}

// ── The page ──
const chatWho = () => esc((chatGrok.name || 'Grok').toLowerCase());
const chatGrokBody = (it, i) => chatMd(it.text) + (it.live ? '<span class="ch-wait"></span>' : `<button class="ch-copy" type="button" data-copy="${i}">copy</button>`);
function chatAskHtml(it) {
  const sk = SKILLS[it.name] || {};
  const q = `<div class="ch-q">${esc(chatGrok.name || 'Grok')} wants to <b>${esc(sk.verb || it.name)}</b>${it.what ? ': ' + esc(it.what) : ''}</div>${it.note ? `<div class="ch-n">${esc(it.note)}</div>` : ''}`;
  if (it.verdict) return `<div class="ch-ask done"><div class="ch-g">?</div><div class="ch-w">${q}<div class="ch-v">${esc(it.verdict)}</div></div></div>`;
  const field = sk.level === 'confirm' ? `<label class="ch-type">type ${esc(sk.word)} to go ahead<input id="ch-typed" autocomplete="off" spellcheck="false"></label>`
    : sk.secret && !it.args?.open ? `<label class="ch-type">${esc(sk.secret)}<input id="ch-secret" type="password" autocomplete="off"></label>` : '';
  const always = sk.level === 'ask' && !sk.secret ? '<label><input type="checkbox" id="ch-always"> always allow this</label>' : '';
  return `<div class="ch-ask"><div class="ch-g">?</div><div class="ch-w">${q}${field}<div class="ch-act"><button class="ch-allow" id="ch-allow" type="button"${sk.level === 'confirm' ? ' disabled' : ''}>allow</button><button class="ch-deny" id="ch-deny" type="button">deny</button>${always}</div></div></div>`;
}
function chatItemHtml(it, i) {
  if (it.k === 'you') return `<div class="ch-msg you"><div class="ch-l">you</div><div class="ch-b">${chatMd(it.text)}</div></div>`;
  if (it.k === 'grok') return `<div class="ch-msg grok"${it.live ? ' id="ch-live"' : ''}><div class="ch-l">${chatWho()}</div><div class="ch-b">${chatGrokBody(it, i)}</div></div>`;
  if (it.k === 'tool') return `<div class="ch-tool${it.state ? ' ' + it.state : ''}"><span class="tn">${esc(it.name)}</span><span class="tr">${esc(it.result || '')}</span></div>`;
  if (it.k === 'ask') return chatAskHtml(it);
  return `<div class="ch-note${it.k === 'error' ? ' bad' : ''}">${esc(it.text)}</div>`;
}
const chatHelloHtml = () => `<div class="ch-hello"><p>Ask ${esc(chatGrok.name || 'Grok')} about your ${esc(dev)}, or to change something on it: anything that changes it waits for you here first. Every message is billed to your xAI account.</p>${CHAT_HELLO.map(t => `<button type="button" data-hello="${esc(t)}">${esc(t)}</button>`).join('')}</div>`;
function chatRender(toEnd) {
  const log = $('#ch-log'), atEnd = toEnd || log.scrollHeight - log.scrollTop - log.clientHeight < 80;
  $('#ch-inner').innerHTML = chat.items.length ? chat.items.map(chatItemHtml).join('') : chatHelloHtml();
  chatUsage();
  if (atEnd) log.scrollTop = log.scrollHeight;
}
// While a reply streams, only its own text is redrawn, once a frame.
function chatLive() {
  if (chatFrame) return;
  chatFrame = requestAnimationFrame(() => {
    chatFrame = 0;
    const live = chat.items.find(x => x.live), node = $('#ch-live .ch-b');
    if (!live || !node) return chatRender();
    const log = $('#ch-log'), atEnd = log.scrollHeight - log.scrollTop - log.clientHeight < 80;
    node.innerHTML = chatGrokBody(live);
    if (atEnd) log.scrollTop = log.scrollHeight;
  });
}
function chatUsage() {
  const usd = chat.ticks / USD_TICKS;
  $('#ch-use').textContent = chat.tokens ? `${(chat.tokens / 1000).toFixed(1)}k tokens · $${usd.toFixed(usd < 0.1 ? 3 : 2)}` : '';
}
function chatRenderRail() {
  const day = d => {
    const today = new Date().setHours(0, 0, 0, 0), n = Math.round((today - new Date(d).setHours(0, 0, 0, 0)) / 864e5);
    return n === 0 ? 'today' : n === 1 ? 'yesterday' : `${MONTHS[d.getMonth()]} ${d.getDate()}`;
  };
  let group = '', html = '';
  for (const f of chatFiles) {
    const g = day(f.date);
    if (g !== group) html += `<div class="ch-grp">${(group = g)}</div>`;
    html += `<a class="ch-item${f.file === chat?.file ? ' on' : ''}" href="#chat" data-chat="${esc(f.file)}">${esc(f.title)}<small>${f.date.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' })}</small></a>`;
  }
  $('#ch-list').innerHTML = html || '<p class="ch-empty">No saved chats yet.</p>';
}
const chatButtons = () => { $('#ch-send').innerHTML = chatRunning ? 'stop &#9632;' : 'send &#8629;'; };
function chatGrow() {
  const t = $('#ch-msg');
  t.style.height = 'auto';
  t.style.height = Math.min(t.scrollHeight, 160) + 'px';
}
function chatCopy(text) {
  const fallback = () => {
    const t = document.createElement('textarea');
    t.value = text;
    t.style.cssText = 'position:fixed;opacity:0';
    document.body.append(t);
    t.select();
    try { document.execCommand('copy') ? toast('Copied.') : toast('Could not copy.', true); } catch { toast('Could not copy.', true); }
    t.remove();
  };
  if (navigator.clipboard && window.isSecureContext) navigator.clipboard.writeText(text).then(() => toast('Copied.'), fallback);
  else fallback(); // The portal is plain http, where the clipboard API isn't offered.
}

// ── Saved chats ──
async function chatLoadFiles() {
  let text = '';
  try { text = await api('/listfiles?folder=' + encodeURIComponent(CHAT_DIR)); } catch {}
  chatFiles = [];
  for (const line of text.split('\n')) {
    if (!line.startsWith('Fi:')) continue;
    const file = line.slice(3, line.lastIndexOf(':')), m = file.match(CHAT_FILE);
    if (m) chatFiles.push({ file, title: m[6], date: new Date(+m[1], m[2] - 1, +m[3], +m[4], +m[5]) });
  }
  chatFiles.sort((a, b) => b.file.localeCompare(a.file));
  chatRenderRail();
}
const chatStamp = t => { const d = new Date(t), p = n => String(n).padStart(2, '0'); return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}`; };
// The chat's first message, cut at a word to about 40 characters, as a file name.
function chatTitle() {
  let t = (chat.items.find(x => x.k === 'you')?.text || 'chat').replace(/[\\/:*?"<>|\x00-\x1f]+/g, ' ').replace(/\s+/g, ' ').trim();
  if (t.length > 40) { t = t.slice(0, 41); const space = t.lastIndexOf(' '); t = space > 10 ? t.slice(0, space) : t.slice(0, 40); }
  return t.replace(/[. ]+$/, '') || 'chat';
}
// Saves one at a time; an upload replaces the chat's file.
function chatSave() {
  if (!chat.items.length) return chatSaving;
  if (!chat.file) chat.file = `${chatStamp(chat.created)} ${chatTitle()}.json`;
  const c = chat, name = c.file;
  chatSaving = chatSaving.then(async () => {
    const { file, ...data } = c;
    data.items = c.items.filter(x => !x.live && !(x.k === 'ask' && !x.verdict));
    await upload('/', { folder: CHAT_DIR, file: [new Blob([JSON.stringify(data)], { type: 'application/json' }), name] }, () => {});
    if (!chatFiles.some(f => f.file === name)) await chatLoadFiles();
  }).catch(e => toast('The chat could not be saved to the SD card: ' + e.message, true));
  return chatSaving;
}
async function chatOpenFile(file) {
  if (chatRunning) return toast('Wait for Grok to finish, or stop it.', true);
  try {
    const data = JSON.parse(await api(`/file?name=${encodeURIComponent(CHAT_DIR + '/' + file)}&action=download`));
    chat = { ...newChat(), ...data, file };
    chatLoadModels();
    chatRender(true);
    chatRenderRail();
  } catch (e) { fail(e); }
}
async function chatLoadModels() {
  let list = chatModels;
  if (!list) {
    try {
      list = JSON.parse(await api('/chatmodels')).data.map(m => m.id).filter(id => /^grok-/.test(id) && !/imagine|voice|tts|stt/.test(id)).sort().reverse();
      if (list.length) chatModels = list;
    } catch {}
    if (!list?.length) list = [CHAT_MODEL];
  }
  if (!list.includes(chat.model)) chat.model = list.includes(CHAT_MODEL) ? CHAT_MODEL : list[0];
  $('#ch-model').innerHTML = list.map(m => `<option${m === chat.model ? ' selected' : ''}>${esc(m)}</option>`).join('');
}
function chatShow() {
  if (!chat) chat = newChat();
  chatRender(true);
  chatLoadFiles();
  chatLoadModels();
  getJson('/settings').then(s => { chatGrok = s.grok || {}; $('#ch-who').textContent = chatGrok.name || 'Grok'; if (!chatRunning) chatRender(); }).catch(() => {});
  if (!matchMedia('(hover: none)').matches) $('#ch-msg').focus();
}

// ── Talking to Grok ──
function chatErrorText(t) {
  try { const j = JSON.parse(t), e = j.error; return (typeof e === 'string' ? e : e?.message) || j.message || j.code || t; } catch { return t || 'xAI refused the request.'; }
}
// One request to xAI, its reply streaming in. Returns the finished response.
async function chatRound(input) {
  // xAI keeps a chat's instructions from its first request, and refuses them again alongside
  // previous_response_id.
  const body = { model: chat.model, stream: true, input, tools: chatTools() };
  if (chat.last) body.previous_response_id = chat.last;
  else body.instructions = chatInstructions();
  const live = { k: 'grok', text: '', live: true }, searches = {};
  chat.items.push(live);
  chatRender(true);
  const ctl = new AbortController();
  chatStop = ctl;
  busy++; // the device answers one request at a time, so the status poll waits
  let done = null;
  try {
    let r;
    try { r = await fetch('/chat', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal: ctl.signal }); }
    catch (e) { if (e.name === 'AbortError') throw e; throw new Error(`The ${dev} did not answer.`); }
    if (r.status === 401) { location.reload(); throw new Error('Logged out.'); }
    if (!r.ok) throw new ChatError(r.status, chatErrorText(await r.text()));
    const reader = r.body.getReader(), dec = new TextDecoder();
    let buf = '';
    for (;;) {
      const { value, done: end } = await reader.read();
      if (end) break;
      buf += dec.decode(value, { stream: true });
      for (let nl; (nl = buf.indexOf('\n')) >= 0;) {
        const line = buf.slice(0, nl).trim();
        buf = buf.slice(nl + 1);
        if (!line.startsWith('data:')) continue;
        let ev;
        try { ev = JSON.parse(line.slice(5)); } catch { continue; }
        const item = ev.item || {};
        if (ev.type === 'response.output_text.delta') {
          live.text += ev.delta || '';
          chatLive();
        } else if (ev.type === 'response.output_item.added' && item.type === 'web_search_call') {
          searches[item.id] = { k: 'tool', name: 'web search', result: 'searching', state: 'wait' };
          chat.items.splice(chat.items.indexOf(live), 0, searches[item.id]);
          chatRender();
        } else if (ev.type === 'response.output_item.done' && searches[item.id]) {
          Object.assign(searches[item.id], { result: item.action?.query || item.action?.url || 'done', state: '' });
          chatRender();
        } else if (ev.type === 'response.completed' || ev.type === 'response.incomplete') {
          done = ev.response;
        } else if (ev.type === 'response.failed' || ev.type === 'error') {
          throw new Error(ev.response?.error?.message || ev.error?.message || ev.message || 'xAI stopped the reply.');
        }
      }
    }
  } finally {
    busy--;
    chatStop = null;
    delete live.live;
    if (!live.text) chat.items.splice(chat.items.indexOf(live), 1);
    chatRender();
  }
  if (!done) throw new Error('The reply stopped before it finished.');
  chat.last = done.id;
  chat.tokens += done.usage?.total_tokens || 0;
  chat.ticks += done.usage?.cost_in_usd_ticks || 0;
  if (done.status === 'incomplete') chat.items.push({ k: 'note', text: `The reply was cut short (${done.incomplete_details?.reason || 'it ran out of room'}).` });
  chatUsage();
  return done;
}
// Runs one skill Grok asked for, asking first if it changes anything.
async function chatSkill(call) {
  const sk = SKILLS[call.name];
  if (!sk) return { result: { error: 'There is no such skill.' } };
  let args;
  try { args = JSON.parse(call.arguments || '{}') || {}; } catch { return { result: { error: 'The arguments were not valid JSON.' } }; }
  let x = {};
  if (sk.level !== 'read') {
    const it = { k: 'ask', name: call.name, what: sk.what?.(args) || '', note: sk.note?.(args) || '' };
    if (sk.level === 'ask' && !sk.secret && prefs.get('always', []).includes(call.name)) it.verdict = 'allowed: you always allow this';
    chat.items.push(it);
    if (!it.verdict) {
      it.args = args; // for the ask's own field; not saved
      chatRender(true);
      x = await new Promise(resolve => { chatPending = { resolve, sk }; });
      chatPending = null;
      delete it.args;
      it.verdict = x.ok ? 'you allowed it' : 'you denied it';
      if (x.ok && x.always) prefs.set('always', [...new Set([...prefs.get('always', []), call.name])]);
      if (!x.ok) { chatRender(); return { result: { denied: true, note: "The user denied this. Don't try it again unless they ask." } }; }
    }
  }
  const line = { k: 'tool', name: call.name, result: '', state: 'wait' };
  chat.items.push(line);
  chatRender(true);
  try {
    const out = (await sk.run(args, x)) || {};
    Object.assign(line, { result: out.summary || 'done', state: '' });
    return { result: out.data ?? { ok: true, result: out.summary || 'done' }, then: out.then };
  } catch (e) {
    Object.assign(line, { result: e.message, state: 'bad' });
    return { result: { error: e.message } };
  } finally {
    chatRender();
  }
}
const chatTranscript = () => chat.items.filter(x => (x.k === 'you' || x.k === 'grok') && x.text).map(x => ({ role: x.k === 'you' ? 'user' : 'assistant', content: x.text }));
// A message, and the rounds of skills it leads to.
async function chatSend(text) {
  if (chatRunning) return;
  chatRunning = true;
  chatButtons();
  chat.items.push({ k: 'you', text });
  chatSave();
  // Skill requests left unanswered (the reply was stopped) are answered first, as xAI expects.
  let input = [...(chat.pending || []).map(id => ({ type: 'function_call_output', call_id: id, output: '{"stopped":"The user stopped this before it ran."}' })), { role: 'user', content: text }];
  chat.pending = null;
  try {
    for (let round = 1; ; round++) {
      let done;
      try {
        done = await chatRound(input);
      } catch (e) {
        // xAI keeps a chat for 30 days; after that it is sent again in full.
        if (!(e instanceof ChatError) || !chat.last || !input.some(i => i.role === 'user') || !/previous|not found|expired/i.test(e.message)) throw e;
        chat.last = null;
        chat.items.push({ k: 'note', text: 'xAI had forgotten this chat, so it was sent again in full.' });
        done = await chatRound(chatTranscript());
      }
      const calls = (done.output || []).filter(o => o.type === 'function_call');
      chat.pending = calls.length ? calls.map(c => c.call_id) : null;
      chatSave();
      if (!calls.length) break;
      if (round >= CHAT_ROUNDS) { chat.items.push({ k: 'note', text: `Stopped after ${CHAT_ROUNDS} rounds of skills for one message.` }); break; }
      input = [];
      let then = null;
      for (const call of calls) {
        const r = await chatSkill(call);
        input.push({ type: 'function_call_output', call_id: call.call_id, output: JSON.stringify(r.result) });
        then = r.then || then;
      }
      if (then) {
        // It takes the device and this page offline: Grok answers first, then it happens.
        try { await chatRound(input); chat.pending = null; } catch {}
        await chatSave();
        await then();
        return;
      }
    }
  } catch (e) {
    chat.items.push(e.name === 'AbortError' ? { k: 'note', text: 'stopped' } : { k: 'error', text: e.message });
    chatSave();
  } finally {
    chatRunning = false;
    chatPending = null;
    chatButtons();
    chatRender();
  }
}

$('#ch-box').addEventListener('submit', e => {
  e.preventDefault();
  if (chatRunning) { chatPending?.resolve({ ok: false }); chatStop?.abort(); return; }
  const t = $('#ch-msg'), text = t.value.trim();
  if (!text) return;
  t.value = '';
  chatGrow();
  chatSend(text);
});
$('#ch-msg').addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) { e.preventDefault(); if (!chatRunning) $('#ch-box').requestSubmit(); }
});
$('#ch-msg').addEventListener('input', chatGrow);
$('#ch-inner').addEventListener('click', e => {
  const b = e.target.closest('button');
  if (!b) return;
  if (b.dataset.hello) { $('#ch-msg').value = b.dataset.hello; chatGrow(); $('#ch-msg').focus(); return; }
  if (b.dataset.copy) return chatCopy(chat.items[+b.dataset.copy]?.text || '');
  if (!chatPending) return;
  if (b.id === 'ch-allow') chatPending.resolve({ ok: true, always: !!$('#ch-always')?.checked, secret: $('#ch-secret')?.value || '' });
  else if (b.id === 'ch-deny') chatPending.resolve({ ok: false });
});
$('#ch-inner').addEventListener('input', e => {
  if (e.target.id === 'ch-typed' && chatPending) $('#ch-allow').disabled = e.target.value.trim().toLowerCase() !== chatPending.sk.word;
});
$('#ch-new').addEventListener('click', () => {
  if (chatRunning) return toast('Wait for Grok to finish, or stop it.', true);
  chat = newChat();
  chatRender(true);
  chatRenderRail();
  $('#ch-rail').classList.remove('open');
  $('#ch-msg').focus();
});
$('#ch-list').addEventListener('click', e => {
  const a = e.target.closest('[data-chat]');
  if (!a) return;
  e.preventDefault();
  $('#ch-rail').classList.remove('open');
  chatOpenFile(a.dataset.chat);
});
$('#ch-model').addEventListener('change', e => { chat.model = e.target.value; if (chat.items.length) chatSave(); });
for (const [id, key] of [['#ch-skills', 'skills'], ['#ch-web', 'web']]) {
  const b = $(id);
  b.setAttribute('aria-pressed', String(prefs.get(key, true)));
  b.addEventListener('click', () => { const on = b.getAttribute('aria-pressed') !== 'true'; b.setAttribute('aria-pressed', String(on)); prefs.set(key, on); });
}
$('#ch-chats').addEventListener('click', () => {
  const open = $('#ch-rail').classList.toggle('open');
  $('#ch-chats').setAttribute('aria-expanded', String(open));
});

tick();
setInterval(tick, 10000);
route();
status();
if (page !== 'partitions') loadParts(); // route() already loads it there; the overview shows flash use
setInterval(() => { if (!document.hidden) status(); }, 30000);
