'use strict';

const I18N = {
  en: {
    title: 'Brother TD-2020A Print Server', phase: 'Phase 2 · USB diagnostics',
    tab_dash: 'Overview', tab_usb: 'USB diagnostics', tab_wifi: 'Wi-Fi', tab_log: 'Log', tab_sys: 'System',
    conn_err: 'No connection to the device.', printer: 'Printer', product: 'Product', usb_class: 'USB class',
    connects: 'Connects', last_error: 'Last error', wifi: 'Wi-Fi', ip: 'IP address', hostname: 'Hostname',
    setup_ap: 'Setup AP', system: 'System', uptime: 'Uptime', reset: 'Last reset', ram: 'RAM free (min)',
    phase_hint: 'This firmware is the diagnostic build (phase 2): it detects the printer and reads all USB ' +
      'descriptors. Printing (port 9100, REST printing, queue) follows in the next phases.',
    usb_refresh: 'Query status again', dl_json: 'Download JSON', dl_text: 'Download text',
    usb_none: 'No USB device detected since boot. Check cabling, OTG adapter and the 5 V VBUS supply (see README).',
    device: 'Device', analysis: 'Analysis', interfaces: 'Interfaces & endpoints', full_report: 'Full report',
    wifi_setup: 'Wi-Fi setup',
    wifi_locked: 'For security reasons Wi-Fi settings can only be changed while connected to the device\'s setup ' +
      'Wi-Fi (or via the serial console: wifi_set).',
    password: 'Password', save_connect: 'Save & connect', auto_refresh: 'Auto refresh', refresh: 'Refresh',
    restart_hint: 'In this phase restart is only allowed via the setup Wi-Fi or the serial console.',
    restart: 'Restart', ap_only: '(setup Wi-Fi only)', raw_hint: '(development feature, enabled)',
    connected: 'Connected', disconnected: 'Not connected', ready: 'Ready (interface claimed)',
    not_ready: 'Connected, no usable printer interface', none: 'none', yes: 'yes', no: 'no',
    printer_class: 'USB Printer Class', vendor_cand: 'Vendor-specific candidate (not printer class)',
    no_if: 'No interface with bulk OUT endpoint', claimed: 'Claimed', port_status: 'Port status',
    paper_empty: 'paper empty', selected: 'selected', no_error: 'no error', not_requested: 'not requested',
    active: 'active', inactive: 'inactive', saved: 'Saved. The device is connecting – watch the serial console ' +
      'or your router for the new IP address.', restarting: 'Restarting…', confirm_restart: 'Restart the device?',
    storage_note: 'Interface %d is Mass Storage – the printer may be in a storage/editor mode.',
  },
  de: {
    title: 'Brother TD-2020A Printserver', phase: 'Phase 2 · USB-Diagnose',
    tab_dash: 'Übersicht', tab_usb: 'USB-Diagnose', tab_wifi: 'WLAN', tab_log: 'Log', tab_sys: 'System',
    conn_err: 'Keine Verbindung zum Gerät.', printer: 'Drucker', product: 'Produkt', usb_class: 'USB-Klasse',
    connects: 'Verbindungen', last_error: 'Letzter Fehler', wifi: 'WLAN', ip: 'IP-Adresse', hostname: 'Hostname',
    setup_ap: 'Setup-AP', system: 'System', uptime: 'Laufzeit', reset: 'Letzter Reset', ram: 'RAM frei (min)',
    phase_hint: 'Diese Firmware ist der Diagnosestand (Phase 2): Sie erkennt den Drucker und liest alle ' +
      'USB-Deskriptoren aus. Druckfunktionen (Port 9100, REST-Druck, Warteschlange) folgen in den nächsten Phasen.',
    usb_refresh: 'Status neu abfragen', dl_json: 'JSON herunterladen', dl_text: 'Text herunterladen',
    usb_none: 'Seit dem Start wurde kein USB-Gerät erkannt. Prüfen Sie Verkabelung, OTG-Adapter und die ' +
      '5-V-VBUS-Versorgung (siehe README).',
    device: 'Gerät', analysis: 'Analyse', interfaces: 'Interfaces & Endpoints', full_report: 'Vollständiger Bericht',
    wifi_setup: 'WLAN einrichten',
    wifi_locked: 'Aus Sicherheitsgründen können die WLAN-Daten nur geändert werden, wenn Sie mit dem Setup-WLAN ' +
      'des Geräts verbunden sind (oder über die serielle Konsole: wifi_set).',
    password: 'Passwort', save_connect: 'Speichern & verbinden', auto_refresh: 'Automatisch aktualisieren',
    refresh: 'Aktualisieren',
    restart_hint: 'Neustart ist in dieser Phase nur über das Setup-WLAN oder die serielle Konsole erlaubt.',
    restart: 'Neustart', ap_only: '(nur über Setup-WLAN)', raw_hint: '(Entwicklungsfunktion, aktiviert)',
    connected: 'Verbunden', disconnected: 'Nicht verbunden', ready: 'Bereit (Interface belegt)',
    not_ready: 'Verbunden, keine nutzbare Druckerschnittstelle', none: 'keine', yes: 'ja', no: 'nein',
    printer_class: 'USB Printer Class', vendor_cand: 'Herstellerspezifischer Kandidat (keine Printer Class)',
    no_if: 'Kein Interface mit Bulk-OUT-Endpoint', claimed: 'Belegt', port_status: 'Port-Status',
    paper_empty: 'Papier leer', selected: 'ausgewählt', no_error: 'kein Fehler', not_requested: 'nicht abgefragt',
    active: 'aktiv', inactive: 'inaktiv', saved: 'Gespeichert. Das Gerät verbindet sich – die neue IP-Adresse ' +
      'steht in der seriellen Konsole bzw. im Router.', restarting: 'Neustart…', confirm_restart: 'Gerät neu starten?',
    storage_note: 'Interface %d ist Massenspeicher – der Drucker ist evtl. in einem Speicher-/Editor-Modus.',
  },
};

let lang = 'de';
try { lang = localStorage.getItem('lang') || (navigator.language || 'de').slice(0, 2); } catch (e) { /* ignore */ }
if (!I18N[lang]) lang = 'en';
const t = (k) => I18N[lang][k] ?? I18N.en[k] ?? k;
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const hex = (v, w = 2) => '0x' + Number(v).toString(16).padStart(w, '0');

let lastStatus = null;
let lastUsb = null;
let activeTab = 'dash';

function applyLang() {
  document.documentElement.lang = lang;
  document.querySelectorAll('[data-i18n]').forEach((el) => { el.textContent = t(el.dataset.i18n); });
  $('lang').textContent = lang === 'de' ? 'EN' : 'DE';
  if (lastStatus) renderStatus(lastStatus);
  if (lastUsb) renderUsb(lastUsb);
}

async function api(path, opts) {
  const r = await fetch(path, opts);
  const type = r.headers.get('content-type') || '';
  const body = type.includes('json') ? await r.json() : await r.text();
  if (!r.ok) throw Object.assign(new Error(body.message || r.statusText), { body });
  return body;
}

function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return (d ? d + 'd ' : '') + h + 'h ' + m + 'm ' + (s % 60) + 's';
}
const kb = (b) => (b / 1024).toFixed(0) + ' KiB';

function dl(el, rows) {
  el.innerHTML = rows.map(([k, v]) => `<dt>${esc(k)}</dt><dd>${v}</dd>`).join('');
}

function renderStatus(s) {
  lastStatus = s;
  const p = s.printer, w = s.wifi;
  const pe = $('d-printer');
  pe.textContent = p.connected ? (p.ready ? t('ready') : t('not_ready')) : t('disconnected');
  pe.className = 'big ' + (p.ready ? 'ok' : p.connected ? 'warn' : 'err');
  $('d-vidpid').textContent = p.connected ? `${p.vid} / ${p.pid}` : '–';
  $('d-product').textContent = p.connected ? `${p.manufacturer} ${p.product}`.trim() || '–' : '–';
  $('d-class').textContent = p.connected ? (p.is_printer_class ? t('printer_class') : '–') : '–';
  $('d-connects').textContent = p.connect_count;
  $('d-lasterr').textContent = p.last_error || '–';

  const we = $('d-wifi');
  we.textContent = w.sta_connected ? t('connected') : t('disconnected');
  we.className = 'big ' + (w.sta_connected ? 'ok' : 'warn');
  $('d-ssid').textContent = w.ssid || '–';
  $('d-ip').textContent = w.ip || '–';
  $('d-rssi').textContent = w.sta_connected ? w.rssi + ' dBm' : '–';
  $('d-host').textContent = w.hostname + '.local';
  $('d-ap').textContent = w.ap_active ? `${t('active')} (${w.ap_ssid}, ${w.ap_clients})` : t('inactive');

  $('d-version').textContent = `${s.firmware.name} ${s.firmware.version}`;
  $('d-idf').textContent = s.firmware.idf;
  $('d-uptime').textContent = fmtUptime(s.uptime_s);
  $('d-reset').textContent = s.reset_reason;
  $('d-ram').textContent = `${kb(s.memory.internal_free)} (${kb(s.memory.internal_min_free)})`;
  $('d-psram').textContent = `${kb(s.memory.psram_free)} / ${kb(s.memory.psram_total)}`;
  $('f-version').textContent = `${s.firmware.name} ${s.firmware.version} · ${s.firmware.build}`;

  $('wifi-locked').classList.toggle('hidden', w.via_setup_ap);
  $('wifi-form').querySelectorAll('input,button').forEach((e) => { e.disabled = !w.via_setup_ap; });
  $('restart').disabled = !w.via_setup_ap;
  $('api-raw').classList.toggle('hidden', !s.features.raw_send);
}

function renderUsb(u) {
  lastUsb = u;
  $('usb-none').classList.toggle('hidden', u.valid);
  $('usb-content').classList.toggle('hidden', !u.valid);
  if (!u.valid) return;
  const d = u.device, s = u.strings, p = u.printer_interface;
  dl($('usb-device'), [
    ['Status', u.connected ? `<span class="ok">${t('connected')}</span>` : `<span class="err">${t('disconnected')}</span>`],
    ['idVendor', esc(d.vid_hex)], ['idProduct', esc(d.pid_hex)],
    ['Manufacturer', esc(s.manufacturer ?? '–')], ['Product', esc(s.product ?? '–')], ['Serial', esc(s.serial ?? '–')],
    ['bcdUSB / bcdDevice', `${esc(d.bcdUSB)} / ${esc(d.bcdDevice)}`],
    ['Speed', esc(u.speed)], ['bDeviceClass', `${hex(d.bDeviceClass)} (${esc(d.class_name)})`],
    ['bMaxPacketSize0', d.bMaxPacketSize0], ['bNumConfigurations', d.bNumConfigurations],
    ['LANGID', hex(s.lang_id, 4)],
  ]);
  const rows = [];
  if (!p.found) {
    rows.push(['Interface', `<span class="err">${t('no_if')}</span>`]);
  } else {
    rows.push(['Interface', `${p.interface_number} / alt ${p.alternate_setting}`]);
    rows.push(['Typ', p.is_printer_class ? `<span class="ok">${t('printer_class')}</span> (${esc(p.protocol_name)})`
      : `<span class="warn">${t('vendor_cand')}</span>`]);
    rows.push(['Class/Sub/Proto', `${hex(p.class)}/${hex(p.subclass)}/${hex(p.protocol)}`]);
    rows.push(['Bulk OUT', `${hex(p.ep_out)} (MPS ${p.ep_out_mps})`]);
    rows.push(['Bulk IN', p.ep_in ? `${hex(p.ep_in)} (MPS ${p.ep_in_mps})` : t('none')]);
  }
  rows.push([t('claimed'), p.claimed ? t('yes') : `${t('no')} ${esc(p.claim_error ?? '')}`]);
  const ps = u.port_status;
  rows.push([t('port_status'), !ps.attempted ? t('not_requested') : ps.error ? `<span class="err">${esc(ps.error)}</span>`
    : `${hex(ps.raw)} – ${t('paper_empty')}: ${ps.paper_empty ? t('yes') : t('no')}, ${t('selected')}: ` +
      `${ps.selected ? t('yes') : t('no')}, ${t('no_error')}: ${ps.no_error ? t('yes') : t('no')}`]);
  (u.configuration.interfaces || []).filter((i) => i.bInterfaceClass === 8)
    .forEach((i) => rows.push(['Hinweis', `<span class="warn">${esc(t('storage_note').replace('%d', i.bInterfaceNumber))}</span>`]));
  if (u.configuration.chain_error) rows.push(['Descriptor', `<span class="err">${esc(u.configuration.chain_error)}</span>`]);
  dl($('usb-analysis'), rows);

  const c = u.configuration;
  let html = `<p class="hint">bConfigurationValue ${c.bConfigurationValue} · wTotalLength ${c.wTotalLength} · ` +
    `${c.self_powered ? 'self-powered' : 'bus-powered'} · MaxPower ${c.max_power_ma} mA</p>`;
  for (const i of c.interfaces || []) {
    html += `<div class="ifhead">Interface ${i.bInterfaceNumber} alt ${i.bAlternateSetting} – ` +
      `${hex(i.bInterfaceClass)}/${hex(i.bInterfaceSubClass)}/${hex(i.bInterfaceProtocol)} ${esc(i.class_name)}` +
      `${i.interface_string ? ' – "' + esc(i.interface_string) + '"' : ''}</div>`;
    html += '<table><tr><th>Endpoint</th><th>Dir</th><th>Type</th><th>wMaxPacketSize</th><th>bInterval</th></tr>';
    for (const e of i.endpoints) {
      html += `<tr><td>${esc(e.address_hex)}</td><td>${esc(e.direction)}</td><td>${esc(e.type)}</td>` +
        `<td>${e.wMaxPacketSize}</td><td>${e.bInterval}</td></tr>`;
    }
    html += '</table>';
    for (const x of i.extra_descriptors) {
      html += `<p class="hint">${esc(x.type_name)}: <code>${esc(x.raw)}</code></p>`;
    }
  }
  $('usb-ifs').innerHTML = html;

  const id = u.ieee1284;
  $('usb-devid').textContent = !id.attempted ? t('not_requested') : id.error ? id.error : id.id || '(leer/empty)';
  dl($('usb-devid-fields'), Object.entries(id.fields || {}).map(([k, v]) => [k, esc(v)]));
}

async function loadStatus() {
  try {
    renderStatus(await api('/api/status'));
    $('conn-err').classList.add('hidden');
  } catch (e) {
    $('conn-err').classList.remove('hidden');
  }
}

async function loadUsb() {
  try {
    renderUsb(await api('/api/usb'));
    $('usb-text').textContent = await api('/api/usb/text');
  } catch (e) { /* shown via status */ }
}

async function loadLog() {
  try {
    const pre = $('log');
    const atBottom = pre.scrollTop + pre.clientHeight >= pre.scrollHeight - 20;
    pre.textContent = await api('/api/log');
    if (atBottom) pre.scrollTop = pre.scrollHeight;
  } catch (e) { /* ignore */ }
}

function showTab(name) {
  activeTab = name;
  document.querySelectorAll('.tab').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  document.querySelectorAll('.panel').forEach((p) => p.classList.toggle('hidden', p.id !== 'tab-' + name));
  if (name === 'usb') loadUsb();
  if (name === 'log') loadLog();
}

document.querySelectorAll('.tab').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.tab)));
$('lang').addEventListener('click', () => {
  lang = lang === 'de' ? 'en' : 'de';
  try { localStorage.setItem('lang', lang); } catch (e) { /* ignore */ }
  applyLang();
});
$('usb-refresh').addEventListener('click', async () => {
  try { renderUsb(await api('/api/usb/refresh', { method: 'POST' })); $('usb-text').textContent = await api('/api/usb/text'); }
  catch (e) { alert(e.message); }
});
$('log-refresh').addEventListener('click', loadLog);
$('wifi-form').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const body = new URLSearchParams(new FormData(ev.target));
  try {
    await api('/api/wifi', { method: 'POST', body, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    $('wifi-msg').textContent = t('saved');
    ev.target.reset();
  } catch (e) { $('wifi-msg').textContent = e.message; }
});
$('restart').addEventListener('click', async () => {
  if (!confirm(t('confirm_restart'))) return;
  try { await api('/api/system/restart', { method: 'POST' }); $('sys-msg').textContent = t('restarting'); }
  catch (e) { $('sys-msg').textContent = e.message; }
});

applyLang();
loadStatus();
setInterval(() => {
  loadStatus();
  if (activeTab === 'usb') loadUsb();
  if (activeTab === 'log' && $('log-auto').checked) loadLog();
}, 3000);
