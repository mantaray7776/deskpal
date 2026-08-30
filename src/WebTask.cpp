// ─────────────────────────────────────────────────────────────────────────────
//  WebTask.cpp  —  Wi-Fi, WebSocket, HTTP dashboard
//  Dashboard redesign: Session Timer (hero) + Distraction Score (hero) + stats
//  No compass. Minimal dark aesthetic matching TFT layout logic.
// ─────────────────────────────────────────────────────────────────────────────
#include "Config.h"
#include "NavState.h"
#include "Suggestion.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>

// ── Cardinal helper ───────────────────────────────────────────────────────────
static String heading_to_cardinal(float h) {
    static const char* d[] = {"N","NE","E","SE","S","SW","W","NW"};
    return d[(int)((h + 22.5f) / 45.0f) & 7];
}

static WebServer        s_http(80);
static WebSocketsServer s_ws(WS_PORT);
static DNSServer        s_dns;
const byte              DNS_PORT = 53;

// ─────────────────────────────────────────────────────────────────────────────
//  Embedded dashboard HTML
//  Design:
//    • Dark background (#07090d) single teal accent (#1D9E75)
//    • Hero 1: Session ring + time remaining + controls
//    • Hero 2: Distraction score + colour-coded bar + status text
//    • Stat grid: motion, EM, tilt, impacts
//    • Sparkline: vibration last 60 s
// ─────────────────────────────────────────────────────────────────────────────
static const char DASHBOARD_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskMon</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#07090d;--surface:#0d1520;--surface2:#111e2c;
  --accent:#1D9E75;--accent-dim:#0f4a36;
  --text:#b4c0d0;--dim:#445566;--white:#e8eef6;
  --warn:#d4a030;--danger:#c44040;
  --radius:8px;--radius-lg:12px;
  --border:1px solid #1a2535;
}
html,body{height:100%;background:var(--bg);color:var(--text);font-family:system-ui,sans-serif}
body{padding:16px;max-width:680px;margin:0 auto;display:flex;flex-direction:column;gap:12px}

/* header */
.hdr{display:flex;justify-content:space-between;align-items:center;padding:10px 14px;
     background:var(--surface);border-radius:var(--radius);border:var(--border)}
.hdr-title{font-size:13px;font-weight:500;color:var(--white);display:flex;align-items:center;gap:6px}
.hdr-title svg{width:14px;height:14px;fill:var(--accent)}
.conn{display:flex;align-items:center;gap:5px;font-size:11px;color:var(--dim)}
.conn.ok{color:var(--accent)}
.conn-dot{width:6px;height:6px;border-radius:50%;background:currentColor;flex-shrink:0}

/* hero session */
.session-hero{background:var(--surface);border-radius:var(--radius-lg);border:var(--border);
              padding:16px;display:flex;align-items:center;gap:16px}
.ring-wrap{position:relative;width:88px;height:88px;flex-shrink:0}
.ring-wrap svg{position:absolute;inset:0;width:100%;height:100%}
.ring-center{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px}
.ring-pct{font-size:15px;font-weight:500;color:var(--white);font-variant-numeric:tabular-nums}
.ring-lbl{font-size:9px;color:var(--dim)}
.sess-info{flex:1;min-width:0}
.sess-phase{font-size:11px;color:var(--dim);margin-bottom:4px}
.sess-time{font-size:32px;font-weight:500;color:var(--white);font-variant-numeric:tabular-nums;line-height:1;margin-bottom:4px;letter-spacing:.02em}
.sess-meta{font-size:11px;color:var(--dim)}
.sess-meta b{color:var(--text)}
.pomo-dots{display:flex;gap:5px;margin-top:8px}
.pomo-dot{width:7px;height:7px;border-radius:50%;background:var(--accent-dim)}
.pomo-dot.done{background:var(--accent)}
.sess-btns{display:flex;flex-direction:column;gap:6px}
.btn{background:var(--surface2);border:var(--border);color:var(--text);
     border-radius:var(--radius);padding:7px 14px;font-size:12px;cursor:pointer;white-space:nowrap;
     transition:background .15s}
.btn:hover{background:#1a2e42}
.btn.primary{background:var(--accent-dim);color:var(--accent);border-color:var(--accent-dim)}
.btn.primary:hover{background:#1a5c40}

/* hero score */
.score-hero{background:var(--surface);border-radius:var(--radius-lg);border:var(--border);padding:16px}
.score-top{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:10px}
.score-lbl{font-size:12px;color:var(--dim)}
.score-num{font-size:36px;font-weight:500;color:var(--white);font-variant-numeric:tabular-nums;line-height:1}
.score-track{height:6px;background:var(--surface2);border-radius:3px;overflow:hidden;margin-bottom:8px}
.score-fill{height:100%;border-radius:3px;transition:width .4s,background .4s}
.score-hint{font-size:11px;color:var(--dim)}

/* stat grid */
.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}
.tile{background:var(--surface);border-radius:var(--radius);border:var(--border);padding:10px 12px}
.tile-lbl{font-size:10px;color:var(--dim);margin-bottom:6px}
.tile-val{font-size:18px;font-weight:500;color:var(--white);line-height:1}
.tile-val.ok{color:var(--accent)}
.tile-val.warn{color:var(--warn)}
.tile-val.danger{color:var(--danger)}
.tile-sub{font-size:10px;color:var(--dim);margin-top:3px}

/* sparkline */
.spark-wrap{background:var(--surface);border-radius:var(--radius);border:var(--border);padding:12px}
.spark-hdr{font-size:10px;color:var(--dim);margin-bottom:8px}
canvas{display:block;width:100%;height:52px;border-radius:4px}

/* suggestion banner */
.sugg-banner{background:var(--surface);border:1px solid var(--accent-dim);border-radius:var(--radius-lg);
             padding:12px 14px;display:flex;justify-content:space-between;align-items:center;gap:10px}
.sugg-text{font-size:13px;color:var(--white)}
.sugg-btns{display:flex;gap:6px;flex-shrink:0}

/* widget cards (mail / calendar / todo / phone) */
.widgets{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.wcard{background:var(--surface);border-radius:var(--radius-lg);border:var(--border);padding:12px}
.wcard-top{display:flex;justify-content:space-between;align-items:center;margin-bottom:6px}
.wcard-badge{background:var(--accent-dim);color:var(--accent);border-radius:10px;padding:1px 8px;font-size:11px;font-weight:600}
.wcard-list{display:flex;flex-direction:column}
.wcard-item{padding:4px 0;border-top:1px solid #1a2535;font-size:11px;color:var(--text);
            overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.wcard-item:first-child{border-top:none}
.wcard-item .sub{color:var(--dim);display:block;font-size:10px}
.wcard-item.overdue{color:var(--warn)}
.wcard-empty{font-size:11px;color:var(--dim)}

/* auto-focus toggle row */
.auto-row{display:flex;justify-content:space-between;align-items:center;background:var(--surface);
          border:var(--border);border-radius:var(--radius);padding:10px 14px;font-size:12px;color:var(--text)}
.switch{position:relative;display:inline-block;width:34px;height:18px;flex-shrink:0}
.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;cursor:pointer;inset:0;background:var(--surface2);border-radius:9px;transition:.2s}
.slider:before{content:"";position:absolute;height:14px;width:14px;left:2px;top:2px;background:var(--dim);
               border-radius:50%;transition:.2s}
input:checked+.slider{background:var(--accent-dim)}
input:checked+.slider:before{transform:translateX(16px);background:var(--accent)}

/* footer */
.ftr{font-size:10px;color:var(--dim);text-align:center}
</style>
</head>
<body>

<!-- header -->
<div class="hdr">
  <div class="hdr-title">
    <svg viewBox="0 0 16 16"><rect x="1" y="3" width="14" height="9" rx="1.5" fill="none"
      stroke="var(--accent)" stroke-width="1.4"/><path d="M5 12v2M11 12v2M3 14h10"
      stroke="var(--accent)" stroke-width="1.2" stroke-linecap="round"/></svg>
    DeskMon
  </div>
  <div class="conn" id="conn"><span class="conn-dot"></span><span id="conn-txt">disconnected</span></div>
</div>

<!-- suggestion banner (hidden unless active) -->
<div class="sugg-banner" id="sugg-banner" style="display:none">
  <div class="sugg-text" id="sugg-text"></div>
  <div class="sugg-btns">
    <button class="btn primary" onclick="send({cmd:'suggestion_accept'})">✓ accept</button>
    <button class="btn" onclick="send({cmd:'suggestion_dismiss'})">✕ dismiss</button>
  </div>
</div>

<!-- session hero -->
<div class="session-hero">
  <div class="ring-wrap">
    <svg viewBox="0 0 88 88">
      <circle cx="44" cy="44" r="36" fill="none" stroke="#1a2535" stroke-width="6"/>
      <circle cx="44" cy="44" r="36" fill="none" stroke="var(--accent)" stroke-width="6"
        stroke-linecap="round" stroke-dasharray="226"
        id="ring-path" stroke-dashoffset="226" transform="rotate(-90 44 44)"/>
    </svg>
    <div class="ring-center">
      <span class="ring-pct" id="ring-pct">0%</span>
      <span class="ring-lbl">done</span>
    </div>
  </div>
  <div class="sess-info">
    <div class="sess-phase" id="sess-phase">session paused</div>
    <div class="sess-time" id="sess-time">--:--</div>
    <div class="sess-meta">target <b id="sess-target">25 min</b> · <span id="sess-elapsed">0:00</span> elapsed</div>
    <div class="pomo-dots" id="pomo-dots"></div>
  </div>
  <div class="sess-btns">
    <button class="btn primary" id="btn-toggle" onclick="toggleSession()">▶ start</button>
    <button class="btn" onclick="send({cmd:'session_reset'})">↺ reset</button>
  </div>
</div>

<!-- distraction score hero -->
<div class="score-hero">
  <div class="score-top">
    <span class="score-lbl">distraction score</span>
    <span class="score-num" id="score-num">--</span>
  </div>
  <div class="score-track"><div class="score-fill" id="score-fill" style="width:0%"></div></div>
  <div class="score-hint" id="score-hint">waiting for data…</div>
</div>

<!-- assistant widgets -->
<div class="widgets">
  <div class="wcard" id="mail-card">
    <div class="wcard-top"><span class="score-lbl">mail</span><span class="wcard-badge" id="mail-badge">--</span></div>
    <div class="wcard-list" id="mail-list"></div>
  </div>
  <div class="wcard" id="cal-card">
    <div class="wcard-top"><span class="score-lbl">next up</span></div>
    <div class="wcard-list" id="cal-list"><span class="wcard-empty">--</span></div>
  </div>
  <div class="wcard" id="todo-card">
    <div class="wcard-top"><span class="score-lbl">todo</span><span class="wcard-badge" id="todo-badge">--</span></div>
    <div class="wcard-list" id="todo-list"></div>
  </div>
  <div class="wcard" id="phone-card">
    <div class="wcard-top"><span class="score-lbl">phone</span><span class="wcard-badge" id="phone-badge">--</span></div>
    <div class="wcard-list" id="phone-list"></div>
  </div>
</div>

<div class="auto-row">
  <span>auto-start focus sessions</span>
  <label class="switch">
    <input type="checkbox" id="auto-focus-toggle" onchange="send({cmd:'set_auto_focus', value:this.checked})">
    <span class="slider"></span>
  </label>
</div>

<!-- stat grid -->
<div class="grid">
  <div class="tile">
    <div class="tile-lbl">motion</div>
    <div class="tile-val" id="t-motion">--</div>
    <div class="tile-sub" id="t-vib">vib --</div>
  </div>
  <div class="tile">
    <div class="tile-lbl">EM field</div>
    <div class="tile-val" id="t-em">--</div>
    <div class="tile-sub" id="t-emv">var --</div>
  </div>
  <div class="tile">
    <div class="tile-lbl">tilt</div>
    <div class="tile-val" id="t-tilt">--</div>
    <div class="tile-sub" id="t-roll">roll --</div>
  </div>
  <div class="tile">
    <div class="tile-lbl">impacts</div>
    <div class="tile-val" id="t-impact">--</div>
    <div class="tile-sub" id="t-em-spk">EM spk --</div>
  </div>
</div>

<!-- sparkline -->
<div class="spark-wrap">
  <div class="spark-hdr">vibration — last 60 s</div>
  <canvas id="spark"></canvas>
</div>

<div class="ftr" id="ftr">deskmon.local (Wi-Fi) · <span id="ftr-ap">192.168.4.1</span> (direct) · ws:<span id="ws-port">81</span></div>

<script>
const CIRC = 2 * Math.PI * 36;
const vibBuf = new Array(60).fill(0);
let sessionActive = false;

const ws = new WebSocket('ws://' + location.hostname + ':81');

ws.onopen = () => {
  const c = document.getElementById('conn');
  c.className = 'conn ok';
  document.getElementById('conn-txt').textContent = location.hostname;
};
ws.onclose = () => {
  const c = document.getElementById('conn');
  c.className = 'conn';
  document.getElementById('conn-txt').textContent = 'disconnected';
};

ws.onmessage = e => {
  let d;
  try { d = JSON.parse(e.data); } catch { return; }
  if (d.type !== 'state') return;
  const s = d.state;

  // session
  sessionActive = s.session_active;
  const remain = Math.max(0, s.session_duration - s.elapsed_sec);
  const pct    = s.session_duration > 0
                   ? Math.min(100, (s.elapsed_sec / s.session_duration) * 100) : 0;

  document.getElementById('sess-time').textContent  = fmtMMSS(remain);
  document.getElementById('ring-pct').textContent   = Math.round(pct) + '%';
  document.getElementById('ring-path').style.strokeDashoffset =
    (CIRC * (1 - pct / 100)).toFixed(1);
  document.getElementById('sess-phase').textContent =
    s.session_active ? 'focus session · pomodoro ' + s.pomodoro_count : 'session paused';
  document.getElementById('sess-elapsed').textContent = fmtMMSS(s.elapsed_sec);
  document.getElementById('sess-target').textContent  =
    Math.round(s.session_duration / 60) + ' min';
  document.getElementById('btn-toggle').textContent =
    s.session_active ? '⏸ pause' : '▶ start';

  // pomo dots (4-cycle)
  const dotEl = document.getElementById('pomo-dots');
  dotEl.innerHTML = '';
  const inCycle = s.pomodoro_count % 4;
  for (let i = 0; i < 4; i++) {
    const d2 = document.createElement('div');
    d2.className = 'pomo-dot' + (i < inCycle ? ' done' : '');
    dotEl.appendChild(d2);
  }

  // distraction score
  const sc = s.distraction_score;
  document.getElementById('score-num').textContent = sc;
  const col = sc > 80 ? '#c44040' : sc > 60 ? '#d4a030' : '#1D9E75';
  const fill = document.getElementById('score-fill');
  fill.style.width   = sc + '%';
  fill.style.background = col;
  const hints = ['calm — no significant noise','mild distraction detected',
                 'noisy environment','high interference — consider a break'];
  document.getElementById('score-hint').textContent =
    hints[sc > 80 ? 3 : sc > 60 ? 2 : sc > 30 ? 1 : 0];

  // stat tiles
  const mv = document.getElementById('t-motion');
  mv.textContent = s.moving ? 'moving' : 'still';
  mv.className   = 'tile-val ' + (s.moving ? 'warn' : 'ok');
  document.getElementById('t-vib').textContent = 'vib ' + s.vib_rms.toFixed(3);

  const em = document.getElementById('t-em');
  const em_noisy = s.em_var > 8;
  em.textContent = em_noisy ? 'noisy' : 'stable';
  em.className   = 'tile-val ' + (em_noisy ? 'warn' : 'ok');
  document.getElementById('t-emv').textContent = 'var ' + s.em_var.toFixed(1) + ' µT';

  document.getElementById('t-tilt').textContent  = Math.abs(s.pitch).toFixed(1) + '°';
  document.getElementById('t-roll').textContent  = 'roll ' + s.roll.toFixed(1) + '°';
  document.getElementById('t-impact').textContent = s.impact_count ?? '--';
  document.getElementById('t-em-spk').textContent = 'EM spk ' + (s.em_spike_count ?? '--');

  // suggestion banner
  const sg = document.getElementById('sugg-banner');
  if (s.suggestion_active) {
    sg.style.display = 'flex';
    document.getElementById('sugg-text').textContent = s.suggestion_text;
  } else {
    sg.style.display = 'none';
  }
  document.getElementById('auto-focus-toggle').checked = !!s.auto_focus_enabled;

  // mail widget
  document.getElementById('mail-badge').textContent = s.mail_enabled ? (s.mail_unread ?? 0) : '--';
  const mailList = document.getElementById('mail-list');
  mailList.innerHTML = '';
  if (!s.mail_enabled) {
    mailList.innerHTML = '<span class="wcard-empty">not configured</span>';
  } else if (!s.mail_connected) {
    mailList.innerHTML = '<span class="wcard-empty">sync failed</span>';
  } else if (!(s.mail_items || []).length) {
    mailList.innerHTML = '<span class="wcard-empty">inbox zero</span>';
  } else {
    (s.mail_items || []).slice(0, 3).forEach(it => {
      const row = document.createElement('div');
      row.className = 'wcard-item';
      row.innerHTML = escapeHtml(it.subject) + '<span class="sub">' + escapeHtml(it.from) + '</span>';
      mailList.appendChild(row);
    });
  }

  // calendar widget
  const calList = document.getElementById('cal-list');
  calList.innerHTML = '';
  if (!s.cal_enabled) {
    calList.innerHTML = '<span class="wcard-empty">not configured</span>';
  } else if (!s.cal_has_next) {
    calList.innerHTML = '<span class="wcard-empty">nothing upcoming</span>';
  } else {
    const mins = Math.max(0, Math.round(s.cal_start - (Date.now() / 1000)) / 60);
    const row = document.createElement('div');
    row.className = 'wcard-item';
    row.innerHTML = escapeHtml(s.cal_summary) + '<span class="sub">in ' + Math.round(mins) + ' min</span>';
    calList.appendChild(row);
  }

  // todo widget
  document.getElementById('todo-badge').textContent = s.todo_enabled ? (s.todo_open ?? 0) : '--';
  const todoList = document.getElementById('todo-list');
  todoList.innerHTML = '';
  if (!s.todo_enabled) {
    todoList.innerHTML = '<span class="wcard-empty">not configured</span>';
  } else if (!(s.todo_items || []).length) {
    todoList.innerHTML = '<span class="wcard-empty">all clear</span>';
  } else {
    (s.todo_items || []).slice(0, 3).forEach(it => {
      const row = document.createElement('div');
      row.className = 'wcard-item' + (it.overdue ? ' overdue' : '');
      row.textContent = it.content;
      todoList.appendChild(row);
    });
  }

  // phone widget
  document.getElementById('phone-badge').textContent = s.phone_enabled ? (s.phone_unread ?? 0) : '--';
  const phoneList = document.getElementById('phone-list');
  phoneList.innerHTML = '';
  if (!s.phone_enabled) {
    phoneList.innerHTML = '<span class="wcard-empty">no companion app connected</span>';
  } else if (!(s.phone_items || []).length) {
    phoneList.innerHTML = '<span class="wcard-empty">nothing recent</span>';
  } else {
    (s.phone_items || []).slice(0, 3).forEach(it => {
      const row = document.createElement('div');
      row.className = 'wcard-item';
      row.innerHTML = escapeHtml(it.app + ': ' + it.title) + '<span class="sub">' + escapeHtml(it.text) + '</span>';
      phoneList.appendChild(row);
    });
  }

  // footer addresses
  if (s.wifi_ap_ip) document.getElementById('ftr-ap').textContent = s.wifi_ap_ip;

  // sparkline
  vibBuf.shift(); vibBuf.push(s.vib_rms);
  drawSparkline();
};

function escapeHtml(str) {
  const d = document.createElement('div');
  d.textContent = str ?? '';
  return d.innerHTML;
}

function drawSparkline() {
  const el  = document.getElementById('spark');
  const dpr = window.devicePixelRatio || 1;
  el.width  = el.offsetWidth  * dpr;
  el.height = el.offsetHeight * dpr;
  const cx  = el.getContext('2d');
  cx.scale(dpr, dpr);
  const W = el.offsetWidth, H = el.offsetHeight;
  cx.clearRect(0, 0, W, H);
  const mx = Math.max(...vibBuf, 0.01);
  cx.strokeStyle = '#1D9E75';
  cx.lineWidth   = 1.5;
  cx.lineJoin    = 'round';
  cx.beginPath();
  vibBuf.forEach((v, i) => {
    const x = (i / (vibBuf.length - 1)) * W;
    const y = H - (v / mx) * (H - 4) - 2;
    i === 0 ? cx.moveTo(x, y) : cx.lineTo(x, y);
  });
  cx.stroke();
}

function fmtMMSS(s) {
  const m = Math.floor(s / 60), ss = s % 60;
  return String(m).padStart(2,'0') + ':' + String(ss).padStart(2,'0');
}

function send(obj) { ws.send(JSON.stringify(obj)); }

function toggleSession() {
  send({ cmd: sessionActive ? 'session_stop' : 'session_start' });
}
</script>
</body>
</html>
)html";

// ── JSON payload ──────────────────────────────────────────────────────────────
static String build_state_json(const NavState& st) {
    JsonDocument doc;
    doc["type"] = "state";
    JsonObject s = doc["state"].to<JsonObject>();

    s["heading"]         = (int)st.orient.heading;
    s["pitch"]           = roundf(st.orient.pitch * 10) / 10.0f;
    s["roll"]            = roundf(st.orient.roll  * 10) / 10.0f;
    s["moving"]          = st.motion.moving;
    s["vib_rms"]         = st.motion.vibration_rms;
    s["em_var"]          = st.motion.em_variance;
    s["impact_count"]    = st.motion.impact_count;
    s["em_spike_count"]  = st.motion.em_spike_count;
    s["session_active"]  = st.session.active;
    s["elapsed_sec"]     = st.session.elapsed_sec;
    s["session_duration"]= st.session.session_duration;
    s["pomodoro_count"]  = st.session.pomodoro_count;
    s["distraction_score"]= st.session.distraction_score;
    s["ws_clients"]      = st.wifi.ws_clients;
    s["wifi_connected"]  = st.wifi.connected;
    s["wifi_ip"]         = st.wifi.ip;
    s["wifi_ap_ip"]      = st.wifi.ap_ip;

    // mail
    s["mail_enabled"]    = st.mail.enabled;
    s["mail_connected"]  = st.mail.connected;
    s["mail_unread"]     = st.mail.unread_count;
    JsonArray mitems = s["mail_items"].to<JsonArray>();
    for (uint8_t i = 0; i < st.mail.item_count; i++) {
        JsonObject it = mitems.add<JsonObject>();
        it["from"]    = st.mail.items[i].from;
        it["subject"] = st.mail.items[i].subject;
    }

    // suggestion
    s["suggestion_active"]  = st.suggestion.active;
    s["suggestion_text"]    = st.suggestion.text;
    s["suggestion_type"]    = (int)st.suggestion.type;
    s["auto_focus_enabled"] = st.suggestion.auto_focus_enabled;

    // calendar
    s["cal_enabled"]   = st.calendar.enabled;
    s["cal_connected"] = st.calendar.connected;
    s["cal_has_next"]  = st.calendar.has_next_event;
    s["cal_summary"]   = st.calendar.next_summary;
    s["cal_start"]     = st.calendar.next_start_epoch;

    // todo
    s["todo_enabled"]   = st.todo.enabled;
    s["todo_connected"] = st.todo.connected;
    s["todo_open"]      = st.todo.open_count;
    JsonArray titems = s["todo_items"].to<JsonArray>();
    for (uint8_t i = 0; i < st.todo.item_count; i++) {
        JsonObject it = titems.add<JsonObject>();
        it["content"] = st.todo.items[i].content;
        it["overdue"] = st.todo.items[i].overdue;
    }

    // phone notifications
    s["phone_enabled"] = st.phone.enabled;
    s["phone_unread"]  = st.phone.unread_count;
    JsonArray pitems = s["phone_items"].to<JsonArray>();
    for (uint8_t i = 0; i < st.phone.item_count; i++) {
        JsonObject it = pitems.add<JsonObject>();
        it["app"]   = st.phone.items[i].app;
        it["title"] = st.phone.items[i].title;
        it["text"]  = st.phone.items[i].text;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// ── WebSocket command handler ──────────────────────────────────────────────────
static void handle_ws_command(const String& payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, "session_start") == 0) {
        WITH_STATE([&]{
            g_state.session.active      = true;
            g_state.session.elapsed_sec = 0;
        });
        nav_log_event(EventType::SESSION_START);
    } else if (strcmp(cmd, "session_stop") == 0) {
        WITH_STATE([&]{ g_state.session.active = false; });
        nav_log_event(EventType::SESSION_END);
    } else if (strcmp(cmd, "session_reset") == 0) {
        WITH_STATE([&]{
            g_state.session.active      = false;
            g_state.session.elapsed_sec = 0;
        });
    } else if (strcmp(cmd, "set_duration") == 0) {
        uint32_t dur = doc["value"] | SESSION_DEFAULT_SEC;
        if (dur >= 60 && dur <= 90*60) {
            WITH_STATE([&]{ g_state.session.session_duration = dur; });
        }
    } else if (strcmp(cmd, "suggestion_accept") == 0) {
        suggestion_accept();
    } else if (strcmp(cmd, "suggestion_dismiss") == 0) {
        suggestion_dismiss();
    } else if (strcmp(cmd, "set_auto_focus") == 0) {
        bool en = doc["value"] | false;
        WITH_STATE([&]{ g_state.suggestion.auto_focus_enabled = en; });
    } else if (strcmp(cmd, "mail_check_now") == 0 || strcmp(cmd, "phone_clear") == 0) {
        if (strcmp(cmd, "phone_clear") == 0) {
            WITH_STATE([&]{ g_state.phone.unread_count = 0; });
        }
        // mail_check_now: MailTask polls on its own interval; a manual nudge
        // isn't wired up (would need a cross-task "poll now" flag) — left as
        // a no-op placeholder so the web command doesn't error out if sent.
    }
}

static void on_ws_event(uint8_t num, WStype_t type,
                        uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
        WITH_STATE([&]{ g_state.wifi.ws_clients++; });
    } else if (type == WStype_DISCONNECTED) {
        WITH_STATE([&]{ if (g_state.wifi.ws_clients) g_state.wifi.ws_clients--; });
    } else if (type == WStype_TEXT) {
        handle_ws_command(String((char*)payload));
    }
}

// ── Wi-Fi init ────────────────────────────────────────────────────────────────
// Runs the softAP ("DeskMon-Setup") and the home-Wi-Fi station connection at
// the same time (WIFI_AP_STA), instead of only falling back to AP when STA
// fails. This means:
//   - You can always walk up and connect straight to "DeskMon-Setup" —
//     phones/laptops auto-show a captive-portal sign-in prompt for it
//     (handled by the DNS wildcard redirect below), landing on the dashboard
//     at 192.168.4.1 with zero typing.
//   - Once STA joins your home network, the dashboard is ALSO reachable at
//     http://deskmon.local from any device on that network — the nicer,
//     "local host" way to reach it day-to-day.
//   - Both stay up together; nothing has to choose one over the other.
//
// Note: ESP32-Arduino's MDNS only binds to the STA interface, so
// deskmon.local only resolves once STA is connected. AP-side clients always
// use the raw IP (192.4.168.1 by default) — the captive portal makes that
// automatic anyway.
static bool s_mdns_started = false;

static void wifi_init() {
    WiFi.mode(WIFI_AP_STA);

    // Always-on direct-connect AP + captive portal.
    WiFi.softAP("DeskMon-Setup", "deskmon123");
    s_dns.start(DNS_PORT, "*", WiFi.softAPIP());
    WITH_STATE([&]{
        strlcpy(g_state.wifi.ap_ip, WiFi.softAPIP().toString().c_str(), 16);
    });
    log_i("DeskMon-Setup AP up at http://%s", WiFi.softAPIP().toString().c_str());

    // Kick off the home Wi-Fi join in the background — non-blocking, WebTask's
    // loop finishes bringing it up (and starts mDNS) once it succeeds.
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// Called every loop iteration; cheap no-op once STA is connected and steady.
static void wifi_poll_sta() {
    static uint32_t last_check_ms = 0;
    uint32_t now = millis();
    if (now - last_check_ms < 2000) return; // check twice a second at most
    last_check_ms = now;

    bool connected = (WiFi.status() == WL_CONNECTED);

    if (connected && !s_mdns_started) {
        MDNS.begin(MDNS_HOSTNAME);
        s_mdns_started = true;
        log_i("Wi-Fi OK — also reachable at http://%s.local (http://%s)",
              MDNS_HOSTNAME, WiFi.localIP().toString().c_str());
    }
    if (!connected && s_mdns_started) {
        // STA dropped — mDNS responder keeps running harmlessly; Arduino core
        // has no clean mdns_end(), so just let it be and reconnect below.
    }
    if (!connected) {
        WiFi.reconnect(); // non-blocking retry
    }

    WITH_STATE([&]{
        g_state.wifi.connected = connected;
        g_state.wifi.rssi      = connected ? (int8_t)WiFi.RSSI() : 0;
        strlcpy(g_state.wifi.ip, connected ? WiFi.localIP().toString().c_str() : "", 16);
    });
}

// ── WebTask ───────────────────────────────────────────────────────────────────
void WebTask(void* pvParams) {
    wifi_init();

    s_http.on("/", HTTP_GET, []{
        s_http.send_P(200, "text/html", DASHBOARD_HTML);
    });

    // Android companion app posts here: {"app":"Gmail","title":"...","text":"..."}
    // Same-LAN only — no cloud relay, no auth beyond being on the network the
    // AP/STA is already scoped to. Good enough for a desk gadget at home;
    // don't expose this port past your router if that's a concern.
    s_http.on("/notify", HTTP_POST, []{
        JsonDocument doc;
        if (deserializeJson(doc, s_http.arg("plain")) != DeserializationError::Ok) {
            s_http.send(400, "text/plain", "bad json");
            return;
        }
        const char* app   = doc["app"]   | "app";
        const char* title = doc["title"] | "";
        const char* text  = doc["text"]  | "";

        WITH_STATE([&]{
            // Shift existing items down, insert newest at [0] — item_count
            // caps at the ring buffer size so this never overflows.
            uint8_t n = g_state.phone.item_count;
            uint8_t max = PHONE_NOTIF_MAX;
            for (uint8_t i = (n < max ? n : max - 1); i > 0; i--) {
                g_state.phone.items[i] = g_state.phone.items[i - 1];
            }
            strlcpy(g_state.phone.items[0].app,   app,   sizeof(g_state.phone.items[0].app));
            strlcpy(g_state.phone.items[0].title, title, sizeof(g_state.phone.items[0].title));
            strlcpy(g_state.phone.items[0].text,  text,  sizeof(g_state.phone.items[0].text));
            g_state.phone.items[0].received_ms = millis();
            if (n < max) g_state.phone.item_count = n + 1;

            g_state.phone.enabled          = true;
            g_state.phone.unread_count++;
            g_state.phone.last_received_ms = millis();
        });
        nav_log_event(EventType::PHONE_NOTIF_RECEIVED);

        char msg[100];
        snprintf(msg, sizeof(msg), "%.20s: %.30s", app, title);
        suggestion_offer(SuggestionType::PHONE_NOTIF, msg, 15);

        s_http.send(200, "text/plain", "ok");
    });

    s_http.onNotFound([]{
        // Covers two cases with one redirect: (1) OS captive-portal probes
        // from phones connected to DeskMon-Setup, which expect exactly this
        // kind of redirect to trigger the "sign in to network" popup, and
        // (2) any stray not-found path from a normal browser, which just
        // bounces to the dashboard instead of a bare 404.
        s_http.sendHeader("Location", "/", true);
        s_http.send(302, "text/plain", "");
    });

    s_http.begin();
    s_ws.begin();
    s_ws.onEvent(on_ws_event);

    TickType_t last_bcast = xTaskGetTickCount();

    while (true) {
        s_dns.processNextRequest();   // AP is always up, so always service it
        wifi_poll_sta();              // non-blocking STA connect/reconnect + mDNS
        s_http.handleClient();
        s_ws.loop();

                if (xTaskGetTickCount() - last_bcast >= pdMS_TO_TICKS(100)) {
            last_bcast = xTaskGetTickCount();
            NavState st;
            state_snapshot(st);
            if (st.wifi.ws_clients > 0) {
                String payload = build_state_json(st);
                s_ws.broadcastTXT(payload);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
