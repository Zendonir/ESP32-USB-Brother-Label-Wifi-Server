'use strict';

const I18N = {
  en: {
    title: 'Brother TD-2020A Print Server', phase: 'Phase 4 · Windows printing (port 9100)',
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
    restart: 'Restart', ap_only: '(setup Wi-Fi only)', raw_hint: '(test print with driver file)',
    connected: 'Connected', disconnected: 'Not connected', ready: 'Ready (interface claimed)',
    not_ready: 'Connected, no usable printer interface', none: 'none', yes: 'yes', no: 'no',
    printer_class: 'USB Printer Class', vendor_cand: 'Vendor-specific candidate (not printer class)',
    no_if: 'No interface with bulk OUT endpoint', claimed: 'Claimed', port_status: 'Port status',
    paper_empty: 'paper empty', selected: 'selected', no_error: 'no error', not_requested: 'not requested',
    active: 'active', inactive: 'inactive', saved: 'Saved. The device is connecting – watch the serial console ' +
      'or your router for the new IP address.', restarting: 'Restarting…', confirm_restart: 'Restart the device?',
    storage_note: 'Interface %d is Mass Storage – the printer may be in a storage/editor mode.',
    ps_title: 'Printer settings', ps_read: 'Read from printer', ps_save: 'Save',
    ps_hint: 'Stored in the printer. Only settings with a documented Brother command – power, density, sensors ' +
      'and the paper size stay in the Brother tool.', ps_saved: 'Saved in the printer.',
    ps_noanswer: 'no answer',
    label_status: 'Labels & printer status', query_status: 'Query status', st_ok: 'Ready',
    st_error: 'Error', st_none: 'Not queried yet', media: 'Media', size: 'Size', errors: 'Errors',
    phase: 'Phase', model: 'Model', age: 'Age', in_use: 'printer in use',
    m_none: 'no media', m_cont: 'continuous tape', m_die: 'die-cut labels',
    raw_title: 'Test print with driver file (.prn)',
    raw_help: 'A file created by the original Brother driver with "print to file". Sent unchanged.',
    raw_send: 'Send to printer', raw_on: '', raw_off: 'Disabled on the serial console ("raw_send on" enables it again).',
    sending: 'Sending…',
    tab_jobs: 'Jobs', queue: 'Queue', windows_port: 'Windows port', jobs_title: 'Print jobs',
    j_state: 'State', j_source: 'Source', j_size: 'Size', j_age: 'Age', j_msg: 'Message',
    cancel: 'Cancel', retry: 'Retry', no_jobs: 'No jobs yet.',
    q_summary: '%q waiting · %p printed · %f failed',
    st_queued: 'queued', st_waiting: 'waiting', st_printing: 'printing', st_printed: 'printed',
    st_failed: 'failed', st_unknown: 'unconfirmed', st_cancelled: 'cancelled', st_incomplete: 'incomplete',
    scan: 'Search networks', scanning: 'Searching…', scan_none: 'No networks found.', open_net: 'open',
    lan_only: '(home network only)', ota_title: 'Firmware update', ota_running: 'Running:',
    ota_help: 'Choose brother_printserver.bin from the GitHub release (not the -merged.bin). Running print jobs ' +
      'are lost when the device restarts.',
    ota_locked: 'Firmware updates are only possible from your home network, not over the open setup Wi-Fi.',
    ota_install: 'Install', ota_releases: 'Latest version on GitHub', ota_uploading: 'Uploading… %p %',
    ota_checking: 'Checking and installing…', ota_done: 'Version %v installed. The device restarts – reloading in a moment.',
    ota_confirm: 'Install this firmware and restart the device?',
  },
  de: {
    title: 'Brother TD-2020A Printserver', phase: 'Phase 4 · Windows-Druck (Port 9100)',
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
    restart: 'Neustart', ap_only: '(nur über Setup-WLAN)', raw_hint: '(Testdruck mit Treiberdatei)',
    connected: 'Verbunden', disconnected: 'Nicht verbunden', ready: 'Bereit (Interface belegt)',
    not_ready: 'Verbunden, keine nutzbare Druckerschnittstelle', none: 'keine', yes: 'ja', no: 'nein',
    printer_class: 'USB Printer Class', vendor_cand: 'Herstellerspezifischer Kandidat (keine Printer Class)',
    no_if: 'Kein Interface mit Bulk-OUT-Endpoint', claimed: 'Belegt', port_status: 'Port-Status',
    paper_empty: 'Papier leer', selected: 'ausgewählt', no_error: 'kein Fehler', not_requested: 'nicht abgefragt',
    active: 'aktiv', inactive: 'inaktiv', saved: 'Gespeichert. Das Gerät verbindet sich – die neue IP-Adresse ' +
      'steht in der seriellen Konsole bzw. im Router.', restarting: 'Neustart…', confirm_restart: 'Gerät neu starten?',
    storage_note: 'Interface %d ist Massenspeicher – der Drucker ist evtl. in einem Speicher-/Editor-Modus.',
    ps_title: 'Druckereinstellungen', ps_read: 'Vom Drucker lesen', ps_save: 'Speichern',
    ps_hint: 'Werden im Drucker gespeichert. Nur Einstellungen mit dokumentiertem Brother-Befehl – Stromsparen, ' +
      'Druckdichte, Sensoren und das Papierformat bleiben im Brother-Tool.', ps_saved: 'Im Drucker gespeichert.',
    ps_noanswer: 'keine Antwort',
    label_status: 'Etiketten & Druckerstatus', query_status: 'Status abfragen', st_ok: 'Bereit',
    st_error: 'Fehler', st_none: 'Noch nicht abgefragt', media: 'Medium', size: 'Größe', errors: 'Fehler',
    phase: 'Phase', model: 'Modell', age: 'Alter', in_use: 'Drucker belegt',
    m_none: 'kein Medium', m_cont: 'Endlosband', m_die: 'Etiketten (gestanzt)',
    raw_title: 'Testdruck mit Treiberdatei (.prn)',
    raw_help: 'Datei, die der originale Brother-Treiber mit „Ausgabe in Datei“ erzeugt hat. Wird unverändert gesendet.',
    raw_send: 'An Drucker senden', raw_on: '', raw_off: 'Über die serielle Konsole abgeschaltet („raw_send on“ schaltet wieder ein).',
    sending: 'Wird gesendet…',
    tab_jobs: 'Aufträge', queue: 'Warteschlange', windows_port: 'Windows-Port', jobs_title: 'Druckaufträge',
    j_state: 'Status', j_source: 'Quelle', j_size: 'Größe', j_age: 'Alter', j_msg: 'Meldung',
    cancel: 'Abbrechen', retry: 'Wiederholen', no_jobs: 'Noch keine Aufträge.',
    q_summary: '%q wartend · %p gedruckt · %f fehlgeschlagen',
    st_queued: 'wartet', st_waiting: 'wartet auf Drucker', st_printing: 'druckt', st_printed: 'gedruckt',
    st_failed: 'fehlgeschlagen', st_unknown: 'unbestätigt', st_cancelled: 'abgebrochen', st_incomplete: 'unvollständig',
    scan: 'Netzwerke suchen', scanning: 'Suche…', scan_none: 'Keine Netzwerke gefunden.', open_net: 'offen',
    lan_only: '(nur aus dem Heimnetz)', ota_title: 'Firmware-Update', ota_running: 'Läuft:',
    ota_help: 'Datei brother_printserver.bin aus dem GitHub-Release wählen (nicht die -merged.bin). Laufende ' +
      'Druckaufträge gehen beim Neustart verloren.',
    ota_locked: 'Firmware-Updates gehen nur aus dem Heimnetz, nicht über das offene Setup-WLAN.',
    ota_install: 'Installieren', ota_releases: 'Neueste Version auf GitHub', ota_uploading: 'Wird übertragen… %p %',
    ota_checking: 'Wird geprüft und installiert…', ota_done: 'Version %v installiert. Das Gerät startet neu – die Seite lädt gleich neu.',
    ota_confirm: 'Diese Firmware installieren und das Gerät neu starten?',
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
  const q = s.queue;
  $('d-queue').textContent = t('q_summary').replace('%q', q.queued).replace('%p', q.total_printed).replace('%f', q.total_failed);
  $('d-rawport').textContent = `${w.ip || w.ap_ip}:${q.raw_port} (RAW)` + (q.raw_allowed !== 'any' ? ` · ${q.raw_allowed}` : '');
  $('f-version').textContent = `${s.firmware.name} ${s.firmware.version} · ${s.firmware.build}`;

  $('wifi-locked').classList.toggle('hidden', w.via_setup_ap);
  $('scan-box').classList.toggle('hidden', !w.via_setup_ap);
  // Captive portal: whoever arrives over the setup Wi-Fi wants to set it up.
  if (w.via_setup_ap && !portalShown) {
    portalShown = true;
    showTab('wifi');
    scanNetworks();
  }
  const ota = s.ota || {};
  $('ota-card').classList.toggle('hidden', !ota.available);
  $('ota-locked').classList.toggle('hidden', ota.allowed !== false);
  if (!otaBusy) $('ota-form').querySelectorAll('input,button').forEach((e) => { e.disabled = !ota.allowed; });
  $('ota-version').textContent = `${s.firmware.version} (${ota.running || '–'})`;
  $('wifi-form').querySelectorAll('input,button').forEach((e) => { e.disabled = !w.via_setup_ap; });
  $('restart').disabled = !w.via_setup_ap;
  $('api-raw').classList.toggle('hidden', !s.features.raw_send_available);
  $('raw-card').classList.toggle('hidden', !s.features.raw_send_available);
  $('raw-state').textContent = s.features.raw_send ? t('raw_on') : t('raw_off');
  $('raw-form').querySelectorAll('input,button').forEach((e) => { e.disabled = !s.features.raw_send; });
  renderBrother(s.brother);
}

const ERR_TEXT = {
  de: { no_media: 'kein Medium', end_of_media: 'Medienende', wrong_media: 'falsches Medium',
    communication_error: 'Kommunikationsfehler', cover_open: 'Deckel offen', cannot_feed: 'Transport nicht möglich',
    system_error: 'Systemfehler' },
  en: { no_media: 'no media', end_of_media: 'end of media', wrong_media: 'wrong media',
    communication_error: 'communication error', cover_open: 'cover open', cannot_feed: 'cannot feed',
    system_error: 'system error' },
};

function renderBrother(b) {
  const el = $('b-state');
  if (!b) { el.textContent = t('st_none'); el.className = 'big'; dl($('b-details'), []); return; }
  const st = b.status;
  const errs = st.errors.map((e) => ERR_TEXT[lang][e] || e);
  el.textContent = st.has_error ? `${t('st_error')}: ${errs.join(', ')}` : t('st_ok');
  el.className = 'big ' + (st.has_error ? 'err' : 'ok');
  const media = { none: t('m_none'), continuous_tape: t('m_cont'), die_cut_labels: t('m_die') }[st.media_type] || st.media_type;
  const size = st.media_type === 'none' ? '–' : st.media_length_mm ? `${st.media_width_mm} × ${st.media_length_mm} mm` : `${st.media_width_mm} mm`;
  dl($('b-details'), [
    [t('model'), esc(st.model)], [t('media'), esc(media)], [t('size'), esc(size)],
    [t('phase'), esc(st.phase) + (st.printer_in_use ? ` (${t('in_use')})` : '')],
    [t('age'), `${Math.round(b.age_ms / 1000)} s`],
  ]);
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

const STATE_CLASS = { printed: 'ok', failed: 'err', incomplete: 'err', unknown: 'warn', waiting: 'warn', cancelled: '' };

async function loadJobs() {
  try {
    const r = await api('/api/jobs');
    const st = r.stats;
    $('jobs-stats').textContent = t('q_summary').replace('%q', st.queued).replace('%p', st.total_printed).replace('%f', st.total_failed);
    const rows = r.jobs.map((j) => {
      const age = Math.max(0, st.uptime_s - j.created_s);
      const btn = j.can_cancel ? `<button class="secondary" data-act="cancel" data-id="${j.id}">${t('cancel')}</button>`
        : j.can_retry ? `<button class="secondary" data-act="retry" data-id="${j.id}">${t('retry')}</button>` : '';
      return `<tr><td>${j.id}</td><td class="${STATE_CLASS[j.state] || ''}">${esc(t('st_' + j.state))}</td>` +
        `<td>${j.source === 'raw9100' ? 'Port 9100' : 'HTTP'}</td><td>${esc(j.client)}</td>` +
        `<td>${(j.size / 1024).toFixed(1)} KiB</td><td>${fmtUptime(age)}</td>` +
        `<td>${j.name ? `<b>${esc(j.name)}</b><br>` : ''}${esc(j.message)}</td><td>${btn}</td></tr>`;
    });
    $('jobs-body').innerHTML = rows.join('') || `<tr><td colspan="8">${t('no_jobs')}</td></tr>`;
  } catch (e) { /* shown via status */ }
}

$('jobs-body').addEventListener('click', async (ev) => {
  const b = ev.target.closest('button[data-act]');
  if (!b) return;
  try {
    await api(b.dataset.act === 'cancel' ? `/api/jobs/${b.dataset.id}` : `/api/jobs/${b.dataset.id}/retry`,
      { method: b.dataset.act === 'cancel' ? 'DELETE' : 'POST' });
  } catch (e) { alert(e.message); }
  loadJobs();
});

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
  if (name === 'jobs') loadJobs();
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
// Druckereinstellungen (statische Brother-Befehle, /api/printer/settings).
// Gespeichert wird nur, was sich gegenueber dem gelesenen Stand geaendert hat.
const PS_NAMES_DE = {
  command_mode: 'Befehlsmodus', print_priority: 'Druckpriorität', rotate: 'Druck um 180° drehen',
  stop_position: 'Stoppposition', recovery: 'Nach Fehler erneut drucken',
};
const PS_LABELS_DE = {
  Speed: 'Geschwindigkeit', Quality: 'Qualität', 'No rotation': 'nicht drehen', '180 degrees': '180°',
  'Tear bar': 'Abreißkante', Head: 'Druckkopf', Off: 'aus', On: 'ein',
};
let psLast = null;
function renderSettings(r) {
  psLast = r.settings;
  $('ps-list').innerHTML = r.settings.map((s) => {
    const opts = s.options.map((o) => `<option value="${o.value}" ${o.value === s.value ? 'selected' : ''}>` +
      `${esc(lang === 'de' ? (PS_LABELS_DE[o.label] || o.label) : o.label)}</option>`).join('');
    const none = s.value < 0 ? `<option value="" selected>– ${esc(t('ps_noanswer'))} –</option>` : '';
    return `<dt>${esc(lang === 'de' ? (PS_NAMES_DE[s.key] || s.name) : s.name)}</dt>` +
      `<dd><select data-ps="${esc(s.key)}">${none}${opts}</select></dd>`;
  }).join('');
}
async function loadSettings() {
  $('ps-msg').textContent = '';
  try { renderSettings(await api('/api/printer/settings')); } catch (e) { $('ps-msg').textContent = e.message; }
}
$('ps-read').addEventListener('click', loadSettings);
$('ps-save').addEventListener('click', async () => {
  const changed = [...document.querySelectorAll('select[data-ps]')].filter((el) => {
    const old = (psLast || []).find((s) => s.key === el.dataset.ps);
    return el.value !== '' && (!old || String(old.value) !== el.value);
  });
  if (!changed.length) return;
  const body = changed.map((el) => `${encodeURIComponent(el.dataset.ps)}=${encodeURIComponent(el.value)}`).join('&');
  try {
    renderSettings(await api('/api/printer/settings', { method: 'POST', body,
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' } }));
    $('ps-msg').textContent = t('ps_saved');
  } catch (e) { $('ps-msg').textContent = e.message; }
});

$('bq').addEventListener('click', async () => {
  try { const r = await api('/api/printer/status'); renderBrother(r.printer); }
  catch (e) { alert(e.message); }
});
$('raw-form').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const f = $('raw-file').files[0];
  if (!f) return;
  const out = $('raw-result');
  out.classList.remove('hidden');
  out.textContent = t('sending');
  try {
    const r = await fetch('/api/diag/raw', { method: 'POST', body: f, headers: { 'Content-Type': 'application/octet-stream' } });
    out.textContent = JSON.stringify(await r.json(), null, 2);
  } catch (e) { out.textContent = e.message; }
});
let portalShown = false;
let otaBusy = false;

async function scanNetworks() {
  const list = $('scan-list');
  $('scan-btn').disabled = true;
  list.innerHTML = `<li class="hint">${esc(t('scanning'))}</li>`;
  try {
    const r = await api('/api/wifi/scan');
    list.innerHTML = r.networks.length ? r.networks.map((n) =>
      `<li><button type="button" class="net" data-ssid="${esc(n.ssid)}">${esc(n.ssid)}` +
      `<span class="hint">${n.rssi} dBm${n.open ? ' · ' + esc(t('open_net')) : ''}</span></button></li>`).join('')
      : `<li class="hint">${esc(t('scan_none'))}</li>`;
  } catch (e) { list.innerHTML = `<li class="hint">${esc(e.message)}</li>`; }
  $('scan-btn').disabled = false;
}
$('scan-btn').addEventListener('click', scanNetworks);
$('scan-list').addEventListener('click', (ev) => {
  const b = ev.target.closest('button[data-ssid]');
  if (!b) return;
  $('wifi-ssid').value = b.dataset.ssid;
  $('wifi-form').elements.password.focus();
});

// Upload with XMLHttpRequest - fetch() has no upload progress.
$('ota-form').addEventListener('submit', (ev) => {
  ev.preventDefault();
  const f = $('ota-file').files[0];
  if (!f || !confirm(t('ota_confirm'))) return;
  const bar = $('ota-progress'), msg = $('ota-msg');
  otaBusy = true;
  $('ota-form').querySelectorAll('input,button').forEach((e) => { e.disabled = true; });
  bar.classList.remove('hidden');
  bar.value = 0;
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/system/ota');
  xhr.setRequestHeader('Content-Type', 'application/octet-stream');
  xhr.upload.onprogress = (e) => {
    if (!e.lengthComputable) return;
    bar.value = Math.round(e.loaded * 100 / e.total);
    msg.textContent = bar.value < 100 ? t('ota_uploading').replace('%p', bar.value) : t('ota_checking');
  };
  const fertig = (text) => {
    otaBusy = false;
    msg.textContent = text;
    bar.classList.add('hidden');
    if (lastStatus) renderStatus(lastStatus);
  };
  xhr.onload = () => {
    let body = {};
    try { body = JSON.parse(xhr.responseText); } catch (e) { /* ignore */ }
    if (xhr.status === 200 && body.ok) {
      msg.textContent = t('ota_done').replace('%v', body.version || '');
      setTimeout(() => location.reload(), 15000);
    } else {
      fertig(body.message || xhr.statusText || String(xhr.status));
    }
  };
  xhr.onerror = () => fertig(t('conn_err'));
  xhr.send(f);
});

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
  if (activeTab === 'jobs') loadJobs();
  if (activeTab === 'log' && $('log-auto').checked) loadLog();
}, 3000);
