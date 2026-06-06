#include "diag_web.hpp"

#include "diag_probe.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "silent_powmr/inverter_client.h"
#include "silent_powmr/inverter_status.h"
#include "silent_powmr/registers.h"
#include "wifi_prov.hpp"

namespace diag {
namespace {
constexpr char kTag[] = "web";

// Optional HTTP Basic Auth. Leave empty to disable (this page is read-only, so
// it cannot change anything on the inverter regardless). Set both to require a
// login before the page/API can be viewed.
constexpr char kAuthUser[] = "";
constexpr char kAuthPass[] = "";

// OTA (firmware replacement) is privileged and is ALWAYS behind auth, even when
// the read-only page above is left open. CHANGE THESE before relying on it.
constexpr char kOtaUser[] = "admin";
constexpr char kOtaPass[] = "powmr-ota";  // default; overridden from NVS

// The control/OTA password is loaded from NVS (default = kOtaPass) and can be
// changed at runtime via /api/setpw.
char s_ctrl_pw[33] = "powmr-ota";

void load_ctrl_pw() {
  nvs_handle_t h;
  if (nvs_open("spowmr", NVS_READONLY, &h) != ESP_OK) return;
  size_t len = sizeof(s_ctrl_pw);
  nvs_get_str(h, "ctrlpw", s_ctrl_pw, &len);  // unchanged if key absent
  nvs_close(h);
}

bool save_ctrl_pw(const char* pw) {
  nvs_handle_t h;
  if (nvs_open("spowmr", NVS_READWRITE, &h) != ESP_OK) return false;
  const esp_err_t e = nvs_set_str(h, "ctrlpw", pw);
  nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}

silent_powmr::InverterClient* s_client = nullptr;
void reboot_task(void*);  // defined below

namespace pm = silent_powmr::powmr;

void b64encode(const uint8_t* data, size_t len, char* out, size_t cap) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < len && o + 4 < cap; i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < len) n |= data[i + 2];
    out[o++] = tbl[(n >> 18) & 63];
    out[o++] = tbl[(n >> 12) & 63];
    out[o++] = (i + 1 < len) ? tbl[(n >> 6) & 63] : '=';
    out[o++] = (i + 2 < len) ? tbl[n & 63] : '=';
  }
  out[o] = '\0';
}

bool auth_enabled() { return kAuthUser[0] != '\0'; }

bool check_auth(httpd_req_t* req) {
  if (!auth_enabled()) return true;
  char hdr[160];
  if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
    return false;
  }
  char cred[96];
  std::snprintf(cred, sizeof(cred), "%s:%s", kAuthUser, kAuthPass);
  char b64[160];
  b64encode(reinterpret_cast<const uint8_t*>(cred), std::strlen(cred), b64, sizeof(b64));
  char expect[176];
  std::snprintf(expect, sizeof(expect), "Basic %s", b64);
  return std::strcmp(hdr, expect) == 0;
}

esp_err_t deny(httpd_req_t* req) {
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"silent_powmr\"");
  httpd_resp_sendstr(req, "auth required");
  return ESP_OK;
}

// Separate, always-enforced credentials for firmware update.
bool check_ota_auth(httpd_req_t* req) {
  char hdr[160];
  if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
    return false;
  }
  char cred[96];
  std::snprintf(cred, sizeof(cred), "%s:%s", kOtaUser, s_ctrl_pw);
  char b64[160];
  b64encode(reinterpret_cast<const uint8_t*>(cred), std::strlen(cred), b64, sizeof(b64));
  char expect[176];
  std::snprintf(expect, sizeof(expect), "Basic %s", b64);
  return std::strcmp(hdr, expect) == 0;
}

void urldecode(char* s) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  char* d = s;
  while (*s) {
    if (*s == '+') {
      *d++ = ' ';
      ++s;
    } else if (*s == '%' && s[1] && s[2]) {
      *d++ = static_cast<char>(hex(s[1]) * 16 + hex(s[2]));
      s += 3;
    } else {
      *d++ = *s++;
    }
  }
  *d = '\0';
}

// ---------------- STA mode: read-only diagnostics ----------------

const char kDiagHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>silent_powmr diag</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#11151c;color:#e8eef5}
header{padding:14px 16px;background:#0c0f14;font-weight:600}
.wrap{padding:12px;max-width:680px;margin:0 auto}
.meta{opacity:.7;font-size:.85rem;margin-bottom:10px}
.grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-bottom:12px}
.card{background:#1b212b;border-radius:10px;padding:10px}
.k{font-size:.7rem;opacity:.6;text-transform:uppercase}
.v{font-size:1.2rem;font-weight:600;margin-top:2px}
table{width:100%;border-collapse:collapse;font-size:.85rem}
th,td{padding:4px 6px;text-align:right;border-bottom:1px solid #232b36}
th:nth-child(2),td:nth-child(2){text-align:left}
.le{color:#7ec8ff;font-weight:600}
input{width:64px;background:#0c0f14;color:#e8eef5;border:1px solid #2b3340;border-radius:6px;padding:4px}
button{background:#2f6df0;color:#fff;border:0;border-radius:6px;padding:6px 12px}
small{opacity:.6}
</style></head><body>
<header>silent_powmr <small>read-only diagnostics</small> <a href="/update" style="float:right;color:#7ec8ff;font-size:.85rem">firmware &#9654;</a></header>
<div class="wrap">
  <div class="meta" id="meta">connecting…</div>
  <div class="grid">
    <div class="card"><div class="k">Battery</div><div class="v"><span id="bv">--</span> V</div><small>voltage</small></div>
    <div class="card"><div class="k">Load</div><div class="v"><span id="lw">--</span> W</div><small><span id="lva">--</span> VA</small></div>
    <div class="card"><div class="k">Grid</div><div class="v" id="grid">--</div></div>
    <div class="card"><div class="k">Batt flow</div><div class="v"><span id="bw">--</span>W</div><small><span id="ba">--</span> A</small></div>
    <div class="card"><div class="k">Temp</div><div class="v"><span id="temp">--</span>&deg;</div></div>
    <div class="card"><div class="k">Status</div><div class="v" id="invstate" style="font-size:1.1rem">--</div></div>
  </div>
  <div class="card" style="margin-bottom:12px">
    <div class="k">Charger source priority — reg 5017 (the fan lever)</div>
    <div class="v"><span class="le" id="r17be">--</span> <small>· raw 0x<span id="r17hex">--</span></small></div>
    <small>Settings (5000+) are big-endian. Observed on this unit: <b>0 = CSO</b> (charge), <b>2 = OSO</b> (Solar-only, fans off).</small>
  </div>
  <div class="card" style="margin-bottom:12px">
    <div class="k">Manual controls</div>
    <div class="row" style="align-items:center;gap:8px">
      <span style="min-width:110px">Charging mode</span>
      <select id="cmode"><option value="0">CSO</option><option value="1">SNU</option><option value="2">OSO</option></select>
      <button type="button" onclick="setReg(5017,+g('cmode').value,'charging mode')">Set</button>
      <small>now <b id="cmodeNow">--</b></small>
    </div>
    <div class="row" style="align-items:center;gap:8px;margin-top:6px">
      <span style="min-width:110px">Max total charge</span>
      <select id="cmax"></select>
      <button type="button" onclick="setReg(5022,+g('cmax').value,'max charge current')">Set</button>
      <small>now <b id="cmaxNow">--</b> A</small>
    </div>
    <div class="row" style="margin-top:6px;align-items:center;gap:8px">
      <input id="pw" type="password" placeholder="control password" style="flex:1;width:auto" oninput="localStorage.setItem('pw',this.value)">
      <small id="setmsg"></small>
    </div>
    <div class="row" style="margin-top:6px;align-items:center;gap:8px">
      <input id="npw" type="password" placeholder="new password (4-32)" style="flex:1;width:auto">
      <button type="button" onclick="changePw()">Change password</button>
    </div>
  </div>
  <div class="card">
    <div class="k">Raw registers</div>
    <form onsubmit="loadRegs();return false" style="margin:6px 0">
      start <input id="start" value="4502"> count <input id="count" value="13">
      <button>read</button>
    </form>
    <div class="row" style="margin-bottom:6px">
      <button type="button" onclick="preset(4502,13)">Telemetry</button>
      <button type="button" onclick="preset(4533,13)">Settings</button>
      <button type="button" onclick="preset(4546,13)">Charge/Status</button>
    </div>
    <table><thead><tr><th>addr</th><th>name</th><th>hex</th><th class="le">value</th></tr></thead>
    <tbody id="rows"></tbody></table>
    <small>Decoded &times;scale (reads little-endian; 5000+ settings big-endian). Max ~14 regs/read.</small>
  </div>
</div>
<script>
const REG={
4502:['Grid voltage',0.1,'V'],4503:['Grid freq',0.1,'Hz'],4504:['PV voltage',0.1,'V'],4505:['PV power',1,'W'],
4506:['Battery voltage',0.1,'V'],4507:['Battery SoC',1,'%'],4508:['Charge current',1,'A'],4509:['Discharge current',1,'A'],
4510:['Load voltage',0.1,'V'],4511:['Load freq',0.1,'Hz'],4512:['Load power',1,'W'],4513:['Load VA',1,'VA'],4514:['Load percent',1,'%'],
4530:['Error code',1,''],4535:['Setting flags',0,'bits'],4536:['Charger src prio',0,'',{0:'CSO',2:'OSO'}],
4537:['Output src prio',0,'',{0:'Utility',1:'Solar',2:'SBU'}],4538:['AC in range',0,'',{0:'Appliance',1:'UPS'}],
4540:['Target out freq',0,'',{0:'50Hz',1:'60Hz'}],4541:['Max total charge A',1,'A'],4542:['Target out V',1,'V'],4543:['Max utility charge A',1,'A'],
4544:['Back-to-utility V',0.1,'V'],4545:['Back-to-battery V',0.1,'V'],4546:['Bulk charge V',0.1,'V'],4547:['Float charge V',0.1,'V'],
4548:['Low cutoff V',0.1,'V'],4549:['Equalize V',0.1,'V'],4550:['Equalize time',1,'min'],
4553:['Status flags',0,'bits'],4554:['Status flags2',0,'bits'],4555:['Charger status',0,'',{0:'Off',1:'Idle',2:'Charging'}],4557:['Temperature',1,'C'],
5017:['Charger src prio (set)',0,'',{0:'CSO',1:'SNU',2:'OSO'}],5018:['Output src prio (set)',0,'',{0:'Utility',1:'Solar',2:'SBU'}]};
function decodeReg(a,le,be){
  const raw=(a>=5000)?be:le; const m=REG[a];
  if(!m) return ['',String(raw)];
  const en=m[3];
  if(en&&en[raw]!==undefined) return [m[0], raw+' ('+en[raw]+')'];
  if(m[1]===0) return [m[0], (m[2]==='bits'?'0x'+raw.toString(16):String(raw))];
  return [m[0], (raw*m[1]).toFixed(m[1]<1?1:0)+(m[2]?' '+m[2]:'')];
}
function g(id){return document.getElementById(id);}
function invStatus(d){
  if((d.error||0)!==0) return 'Fault ('+d.error+')';
  const w=d.batt_w||0;
  if(w>10) return 'Charging';
  if(w<-10) return 'On battery';
  return d.grid?'On grid':'Standby';
}
async function diag(){
  try{
    const s=await(await fetch('/api/diag')).json();
    g('meta').textContent='host: '+s.host+'   ip: '+s.ip+'   fw: '+(s.fw||'?')+'   '+(s.online?'inverter online':'NO inverter reply');
    const d=s.decoded||{};
    g('bv').textContent=(d.battery_v||0).toFixed(1);
    g('lw').textContent=d.load_w; g('lva').textContent=(d.load_va||0);
    g('grid').textContent=d.grid?'present':'lost';
    g('bw').textContent=d.batt_w; g('ba').textContent=(d.batt_a||0).toFixed(1);
    g('temp').textContent=d.temp; g('invstate').textContent=invStatus(d);
    g('r17be').textContent=s.r5017.be; g('r17hex').textContent=s.r5017.hex;
    const cm={0:'CSO',1:'SNU',2:'OSO',3:'?'}[s.r5017.be]||'?';
    g('cmodeNow').textContent=s.r5017.be+' ('+cm+')';
    g('cmaxNow').textContent=(s.max_charge||0);
  }catch(e){ g('meta').textContent='no data'; }
}
function preset(s,c){g('start').value=s;g('count').value=c;loadRegs();}
async function loadRegs(){
  const st=g('start').value, ct=g('count').value;
  const r=await(await fetch('/api/regs?start='+st+'&count='+ct)).json();
  if(!r.ok){ g('rows').innerHTML='<tr><td colspan=4>read failed &mdash; try count &le; 14, or a known window</td></tr>'; return false; }
  g('rows').innerHTML=r.regs.map(x=>{const d=decodeReg(x.a,x.le,x.be); return '<tr><td>'+x.a+'</td><td>'+d[0]+'</td><td>0x'+x.hex+'</td><td class=le>'+d[1]+'</td></tr>';}).join('');
  return false;
}
function setReg(reg,val,label){
  if(!confirm('Set '+label+' to '+val+'?')) return;
  g('setmsg').textContent='setting...';
  fetch('/api/set?reg='+reg+'&value='+val+'&pw='+encodeURIComponent(g('pw').value),{method:'POST'})
    .then(r=>r.json()).then(j=>{g('setmsg').textContent=j.ok?'set OK':('failed: '+(j.err||''));})
    .catch(()=>{g('setmsg').textContent='error';}).finally(()=>setTimeout(diag,800));
}
function changePw(){
  const cur=g('pw').value, np=g('npw').value;
  if(np.length<4){g('setmsg').textContent='new password too short (min 4)';return;}
  if(!confirm('Change control password?'))return;
  g('setmsg').textContent='changing...';
  fetch('/api/setpw?old='+encodeURIComponent(cur)+'&new='+encodeURIComponent(np),{method:'POST'})
    .then(r=>r.json()).then(j=>{ if(j.ok){ g('pw').value=np; localStorage.setItem('pw',np); g('npw').value=''; g('setmsg').textContent='password changed'; } else { g('setmsg').textContent='failed: '+(j.err||''); } })
    .catch(()=>{g('setmsg').textContent='error';});
}
(function(){let o='';for(let a=10;a<=80;a+=10)o+='<option>'+a+'</option>';g('cmax').innerHTML=o;g('pw').value=localStorage.getItem('pw')||'';})();
diag(); loadRegs(); setInterval(diag,3000);
</script>
</body></html>)HTML";

esp_err_t h_root_diag(httpd_req_t* req) {
  if (!check_auth(req)) return deny(req);
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kDiagHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t h_api_diag(httpd_req_t* req) {
  if (!check_auth(req)) return deny(req);
  silent_powmr::InverterStatus s;
  const bool online = s_client->poll(s);  // reads the two valid windows + decodes

  uint8_t r17[2] = {0, 0}, r18[2] = {0, 0};
  const bool ok17 =
      s_client->read_holding_raw(pm::reg::kChargerSourcePriority, 1, r17, 2) == 2;
  s_client->read_holding_raw(pm::reg::kOutputSourcePriority, 1, r18, 2);
  const uint16_t be17 = (r17[0] << 8) | r17[1], le17 = (r17[1] << 8) | r17[0];
  const uint16_t le18 = (r18[1] << 8) | r18[0];

  uint8_t r41[2] = {0, 0};
  s_client->read_holding_raw(4541, 1, r41, 2);  // Max Total Charging Current (read, LE)
  const uint16_t max_charge = static_cast<uint16_t>((r41[1] << 8) | r41[0]);

  char ip[16] = "?";
  wifi_ip(ip, sizeof(ip));

  char buf[800];
  const int n = std::snprintf(
      buf, sizeof(buf),
      "{\"online\":%s,\"host\":\"%s\",\"ip\":\"%s\","
      "\"decoded\":{\"battery_v\":%.1f,\"soc\":%d,\"grid\":%s,\"load_w\":%d,"
      "\"load_a\":%.1f,\"load_va\":%d,\"batt_w\":%d,\"batt_a\":%.1f,\"temp\":%.0f,"
      "\"charger_status\":%u,\"error\":%u},"
      "\"r5017\":{\"ok\":%s,\"hex\":\"%02X%02X\",\"be\":%u,\"le\":%u},"
      "\"r5018\":{\"le\":%u},\"max_charge\":%u,\"fw\":\"%s\"}",
      online ? "true" : "false", wifi_hostname(), ip, s.battery_voltage,
      s.battery_soc, s.grid_present ? "true" : "false", s.load_power,
      s.load_current, s.load_va, static_cast<int>(s.battery_power), s.battery_current,
      s.temperature, s.charger_status, s.error_code, ok17 ? "true" : "false",
      r17[0], r17[1], be17, le17, le18, max_charge, esp_app_get_description()->version);

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, n);
}

esp_err_t h_api_regs(httpd_req_t* req) {
  if (!check_auth(req)) return deny(req);
  int start = pm::kPollBase, count = pm::kPollCount;
  char q[64], t[16];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "start", t, sizeof(t)) == ESP_OK) start = atoi(t);
    if (httpd_query_key_value(q, "count", t, sizeof(t)) == ESP_OK) count = atoi(t);
  }
  if (count < 1) count = 1;
  if (count > 64) count = 64;
  if (start < 0 || start > 65535) start = pm::kPollBase;

  uint8_t raw[128];
  const size_t got = s_client->read_holding_raw(
      static_cast<uint16_t>(start), static_cast<uint16_t>(count), raw, sizeof(raw));
  httpd_resp_set_type(req, "application/json");
  if (got == 0) return httpd_resp_sendstr(req, "{\"ok\":false}");

  httpd_resp_sendstr_chunk(req, "{\"ok\":true,\"regs\":[");
  char line[96];
  for (int i = 0; i < count; ++i) {
    const uint16_t be = (raw[i * 2] << 8) | raw[i * 2 + 1];
    const uint16_t le = (raw[i * 2 + 1] << 8) | raw[i * 2];
    std::snprintf(line, sizeof(line),
                  "%s{\"a\":%d,\"hex\":\"%04X\",\"be\":%u,\"le\":%u}",
                  i ? "," : "", start + i, be, be, le);
    httpd_resp_sendstr_chunk(req, line);
  }
  httpd_resp_sendstr_chunk(req, "]}");
  httpd_resp_sendstr_chunk(req, nullptr);
  return ESP_OK;
}

// ---------------- Probe: arbitrary read + full sweep (no serial needed) -------

esp_err_t h_api_raw(httpd_req_t* req) {
  if (!check_auth(req)) return deny(req);
  int slave = 5, start = 4501, count = 45, baud = 2400, swap = 0;
  char q[96], t[16];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "slave", t, sizeof(t)) == ESP_OK) slave = atoi(t);
    if (httpd_query_key_value(q, "start", t, sizeof(t)) == ESP_OK) start = atoi(t);
    if (httpd_query_key_value(q, "count", t, sizeof(t)) == ESP_OK) count = atoi(t);
    if (httpd_query_key_value(q, "baud", t, sizeof(t)) == ESP_OK) baud = atoi(t);
    if (httpd_query_key_value(q, "swap", t, sizeof(t)) == ESP_OK) swap = atoi(t);
  }
  if (count < 1) count = 1;
  if (count > 125) count = 125;
  uint8_t resp[300];
  const size_t n = diag_probe_read_raw(
      static_cast<uint8_t>(slave), static_cast<uint16_t>(start),
      static_cast<uint16_t>(count), baud, swap != 0, resp, sizeof(resp));
  httpd_resp_set_type(req, "application/json");
  if (n == 0) return httpd_resp_sendstr(req, "{\"ok\":false,\"n\":0}");
  char hx[700];
  size_t o = 0;
  for (size_t i = 0; i < n && o + 3 < sizeof(hx); ++i)
    o += std::snprintf(hx + o, sizeof(hx) - o, "%02X", resp[i]);
  hx[o] = '\0';
  char out[800];
  const int m = std::snprintf(
      out, sizeof(out),
      "{\"ok\":true,\"slave\":%d,\"start\":%d,\"count\":%d,\"baud\":%d,"
      "\"swap\":%d,\"n\":%u,\"hex\":\"%s\"}",
      slave, start, count, baud, swap, (unsigned)n, hx);
  return httpd_resp_send(req, out, m);
}

esp_err_t h_api_sweep(httpd_req_t* req) {
  if (!check_auth(req)) return deny(req);
  static char buf[3200];
  const size_t n = diag_probe_sweep_json(buf, sizeof(buf));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, static_cast<ssize_t>(n));
}

// Manual control write (password-gated, whitelisted): charger source priority
// (5017, 0..3) and max total charge current (5022, 10..80).
esp_err_t h_api_set(httpd_req_t* req) {
  int reg = 0, value = -1;
  bool pw_ok = false;
  char q[96], t[32];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "reg", t, sizeof(t)) == ESP_OK) reg = atoi(t);
    if (httpd_query_key_value(q, "value", t, sizeof(t)) == ESP_OK) value = atoi(t);
    if (httpd_query_key_value(q, "pw", t, sizeof(t)) == ESP_OK)
      pw_ok = std::strcmp(t, s_ctrl_pw) == 0;
  }
  httpd_resp_set_type(req, "application/json");
  if (!pw_ok) {
    httpd_resp_set_status(req, "403 Forbidden");
    return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"bad password\"}");
  }
  const bool allowed = (reg == 5017 && value >= 0 && value <= 3) ||
                       (reg == 5022 && value >= 10 && value <= 80);
  if (!allowed) return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"not allowed\"}");
  char dbg[64] = "";
  const bool ok = diag_probe_write_reg(5, static_cast<uint16_t>(reg),
                                       static_cast<uint16_t>(value), 2400, false,
                                       dbg, sizeof(dbg));
  char out[128];
  std::snprintf(out, sizeof(out), "{\"ok\":%s,\"resp\":\"%s\"}",
                ok ? "true" : "false", dbg);
  return httpd_resp_sendstr(req, out);
}

// Change the control/OTA password (requires the current one), stored in NVS.
esp_err_t h_api_setpw(httpd_req_t* req) {
  char q[160], oldpw[64] = "", newpw[64] = "";
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "old", oldpw, sizeof(oldpw)) == ESP_OK) urldecode(oldpw);
    if (httpd_query_key_value(q, "new", newpw, sizeof(newpw)) == ESP_OK) urldecode(newpw);
  }
  httpd_resp_set_type(req, "application/json");
  if (std::strcmp(oldpw, s_ctrl_pw) != 0) {
    httpd_resp_set_status(req, "403 Forbidden");
    return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"wrong current password\"}");
  }
  const size_t nlen = std::strlen(newpw);
  if (nlen < 4 || nlen > 32)
    return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"new must be 4-32 chars\"}");
  if (!save_ctrl_pw(newpw))
    return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"save failed\"}");
  std::strncpy(s_ctrl_pw, newpw, sizeof(s_ctrl_pw) - 1);
  s_ctrl_pw[sizeof(s_ctrl_pw) - 1] = '\0';
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---------------- Firmware update (OTA) — always authenticated ----------------

const char kUpdateHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>silent_powmr update</title>
<style>
body{font-family:system-ui,sans-serif;background:#11151c;color:#e8eef5;margin:0;padding:18px}
.box{max-width:420px;margin:auto}input,button{width:100%;box-sizing:border-box;padding:10px;margin:6px 0}
button{background:#2f6df0;color:#fff;border:0;border-radius:8px}progress{width:100%}small{opacity:.65}a{color:#7ec8ff}
</style></head><body><div class="box">
<h2>Firmware update</h2>
<p><small>Running: <b id="fw">?</b></small></p>
<input type="file" id="f" accept=".bin">
<button onclick="up()">Upload &amp; flash</button>
<progress id="p" value="0" max="100"></progress>
<p id="st"></p>
<p><a href="/">&larr; diagnostics</a></p>
<script>
fetch('/api/diag').then(r=>r.json()).then(s=>{document.getElementById('fw').textContent=s.fw||'?';}).catch(()=>{});
function up(){
  const f=document.getElementById('f').files[0];
  const st=document.getElementById('st'), p=document.getElementById('p');
  if(!f){st.textContent='choose a .bin first';return;}
  const x=new XMLHttpRequest(); x.open('POST','/api/ota');
  x.upload.onprogress=e=>{if(e.lengthComputable){p.value=Math.round(e.loaded/e.total*100);st.textContent=p.value+'%';}};
  x.onload=()=>{st.textContent=(x.status===200)?'OK - rebooting into new firmware...':('failed: '+x.responseText);};
  x.onerror=()=>{st.textContent='upload error';};
  x.send(f);
}
</script></div></body></html>)HTML";

esp_err_t h_update_page(httpd_req_t* req) {
  if (!check_ota_auth(req)) return deny(req);
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kUpdateHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t h_api_ota(httpd_req_t* req) {
  if (!check_ota_auth(req)) return deny(req);
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (part == nullptr) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
    return ESP_OK;
  }
  esp_ota_handle_t handle = 0;
  if (esp_ota_begin(part, OTA_SIZE_UNKNOWN, &handle) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
    return ESP_OK;
  }
  char buf[1460];
  int remaining = static_cast<int>(req->content_len);
  bool ok = true;
  while (remaining > 0) {
    const int to_read = remaining < static_cast<int>(sizeof(buf))
                            ? remaining
                            : static_cast<int>(sizeof(buf));
    const int received = httpd_req_recv(req, buf, to_read);
    if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (received <= 0) { ok = false; break; }
    if (esp_ota_write(handle, buf, received) != ESP_OK) { ok = false; break; }
    remaining -= received;
  }
  if (!ok || esp_ota_end(handle) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ota write failed");
    return ESP_OK;
  }
  if (esp_ota_set_boot_partition(part) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set boot failed");
    return ESP_OK;
  }
  ESP_LOGI(kTag, "OTA received; rebooting into new image");
  httpd_resp_sendstr(req, "ok");
  xTaskCreate(reboot_task, "reboot", 2048, nullptr, 5, nullptr);
  return ESP_OK;
}

// ---------------- AP mode: Wi-Fi provisioning portal ----------------

const char kProvHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>silent_powmr setup</title>
<style>
body{font-family:system-ui,sans-serif;background:#11151c;color:#e8eef5;margin:0;padding:18px}
h2{margin:.2em 0}.box{max-width:420px;margin:auto}
input,button{width:100%;box-sizing:border-box;padding:10px;margin:6px 0;border-radius:8px;border:1px solid #2b3340;background:#0c0f14;color:#e8eef5}
button{background:#2f6df0;color:#fff;border:0;font-size:1rem}small{opacity:.65}
</style></head><body><div class="box">
<h2>silent_powmr Wi-Fi setup</h2>
<p><small>After saving, find <b>silent-powmr-diag</b> in your router's DHCP leases and open its IP.</small></p>
<form onsubmit="save();return false">
  <input id="ssid" list="nets" placeholder="Wi-Fi network (SSID)" autocomplete="off">
  <datalist id="nets"></datalist>
  <input id="pass" type="password" placeholder="Wi-Fi password">
  <button>Save &amp; connect</button>
</form>
<p><small id="st">scanning…</small></p>
<script>
async function scan(){
  try{ const a=await(await fetch('/api/scan')).json();
    document.getElementById('nets').innerHTML=a.map(n=>'<option value="'+n.ssid+'">'+n.ssid+' ('+n.rssi+'dBm)</option>').join('');
    document.getElementById('st').textContent=a.length+' networks found';
  }catch(e){ document.getElementById('st').textContent='scan failed'; }
}
async function save(){
  const ssid=document.getElementById('ssid').value, pass=document.getElementById('pass').value;
  await fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass)});
  document.body.innerHTML='<div class="box"><h2>Saved — rebooting…</h2><p>Find <b>silent-powmr-diag</b> in your router DHCP leases, then open its IP.</p></div>';
  return false;
}
scan();
</script></div></body></html>)HTML";

esp_err_t h_root_prov(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kProvHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t h_api_scan(httpd_req_t* req) {
  static char buf[1024];
  const int n = wifi_scan_json(buf, sizeof(buf));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, n);
}

void reboot_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(1200));
  esp_restart();
}

esp_err_t h_api_connect(httpd_req_t* req) {
  char body[256];
  const int len = httpd_req_recv(req, body, sizeof(body) - 1);
  if (len <= 0) {
    httpd_resp_send_500(req);
    return ESP_OK;
  }
  body[len] = '\0';
  char ssid[33] = {}, pass[65] = {};
  httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
  httpd_query_key_value(body, "pass", pass, sizeof(pass));
  urldecode(ssid);
  urldecode(pass);
  wifi_save_creds(ssid, pass);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr(req, "saved, rebooting");
  xTaskCreate(reboot_task, "reboot", 2048, nullptr, 5, nullptr);
  return ESP_OK;
}

httpd_uri_t make(const char* uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t*)) {
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = m;
  u.handler = h;
  return u;
}

}  // namespace

void start_web(WifiMode mode, silent_powmr::InverterClient* client) {
  s_client = client;
  load_ctrl_pw();
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.max_uri_handlers = 12;
  config.lru_purge_enable = true;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(kTag, "httpd start failed");
    return;
  }
  if (mode == WifiMode::kAccessPoint) {
    const httpd_uri_t root = make("/", HTTP_GET, h_root_prov);
    const httpd_uri_t scan = make("/api/scan", HTTP_GET, h_api_scan);
    const httpd_uri_t conn = make("/api/connect", HTTP_POST, h_api_connect);
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &scan);
    httpd_register_uri_handler(server, &conn);
    ESP_LOGI(kTag, "provisioning portal ready");
  } else {
    const httpd_uri_t root = make("/", HTTP_GET, h_root_diag);
    const httpd_uri_t diag = make("/api/diag", HTTP_GET, h_api_diag);
    const httpd_uri_t regs = make("/api/regs", HTTP_GET, h_api_regs);
    const httpd_uri_t upd = make("/update", HTTP_GET, h_update_page);
    const httpd_uri_t ota = make("/api/ota", HTTP_POST, h_api_ota);
    const httpd_uri_t raw = make("/api/raw", HTTP_GET, h_api_raw);
    const httpd_uri_t swp = make("/api/sweep", HTTP_GET, h_api_sweep);
    const httpd_uri_t set = make("/api/set", HTTP_POST, h_api_set);
    const httpd_uri_t setpw = make("/api/setpw", HTTP_POST, h_api_setpw);
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &diag);
    httpd_register_uri_handler(server, &regs);
    httpd_register_uri_handler(server, &upd);
    httpd_register_uri_handler(server, &ota);
    httpd_register_uri_handler(server, &raw);
    httpd_register_uri_handler(server, &swp);
    httpd_register_uri_handler(server, &set);
    httpd_register_uri_handler(server, &setpw);
    ESP_LOGI(kTag, "diagnostics page ready (firmware update at /update)");
  }
}

}  // namespace diag
