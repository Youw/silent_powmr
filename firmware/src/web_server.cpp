#include "web_server.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_http_server.h"
#include "esp_log.h"
#include "inverter_app.hpp"
#include "silent_powmr/dp_map.h"

namespace silent_powmr {
namespace {
constexpr char kTag[] = "web";

// Mobile-friendly single page; it polls /api/status and renders.
const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>silent_powmr</title>
<style>
:root{color-scheme:dark light}
body{font-family:system-ui,sans-serif;margin:0;background:#11151c;color:#e8eef5}
header{padding:16px;font-weight:600;font-size:1.2rem;background:#0c0f14}
.wrap{padding:12px;max-width:560px;margin:0 auto}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.card{background:#1b212b;border-radius:12px;padding:14px}
.card .k{font-size:.75rem;opacity:.6;text-transform:uppercase;letter-spacing:.04em}
.card .v{font-size:1.5rem;font-weight:600;margin-top:4px}
.full{grid-column:1/3}
.pill{display:inline-block;padding:3px 10px;border-radius:999px;font-size:.85rem}
.ok{background:#13391f;color:#7ee2a0}.bad{background:#3a1414;color:#ef9a9a}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}
button{flex:1;min-width:120px;padding:10px;border:0;border-radius:10px;background:#2b3340;color:#e8eef5;font-size:.95rem}
button.active{background:#2f6df0}
small{opacity:.55}
</style></head><body>
<header>silent_powmr <span id="conn" class="pill bad">offline</span></header>
<div class="wrap">
  <div class="grid">
    <div class="card"><div class="k">Battery</div><div class="v"><span id="soc">--</span>%</div><small><span id="bv">--</span> V</small></div>
    <div class="card"><div class="k">Mode</div><div class="v" id="mode">--</div><small>charger priority</small></div>
    <div class="card"><div class="k">Load</div><div class="v"><span id="lw">--</span> W</div><small><span id="la">--</span> A</small></div>
    <div class="card"><div class="k">Grid</div><div class="v" id="grid">--</div></div>
    <div class="card"><div class="k">Battery flow</div><div class="v"><span id="bw">--</span> W</div><small><span id="ba">--</span> A</small></div>
    <div class="card"><div class="k">Temp / Fault</div><div class="v"><span id="temp">--</span>&deg;</div><small>fault <span id="fault">--</span></small></div>
    <div class="card full"><div class="k">Automation</div>
      <div class="row">
        <button id="autoOn"  onclick="ctl(120,1)">Auto ON</button>
        <button id="autoOff" onclick="ctl(120,0)">Auto OFF</button>
      </div>
      <div class="row">
        <button id="mCharge" onclick="ctl(121,0)">Manual: Charge</button>
        <button id="mQuiet"  onclick="ctl(121,1)">Manual: Quiet</button>
      </div>
      <small id="updated"></small>
    </div>
  </div>
</div>
<script>
async function ctl(id,val){ await fetch('/api/control?id='+id+'&val='+val,{method:'POST'}); refresh(); }
function set(id,v){ document.getElementById(id).textContent=v; }
async function refresh(){
  try{
    const r=await fetch('/api/status'); const s=await r.json();
    const c=document.getElementById('conn');
    c.textContent=s.online?('online '+s.age_s+'s'):'offline';
    c.className='pill '+(s.online?'ok':'bad');
    set('soc',s.soc); set('bv',s.battery_v.toFixed(1));
    set('mode',s.mode); set('lw',s.load_w); set('la',s.load_a.toFixed(1));
    set('grid',s.grid?'present':'lost');
    set('bw',s.batt_w); set('ba',s.batt_a.toFixed(1));
    set('temp',s.temp); set('fault',s.fault);
    document.getElementById('autoOn').className='button'+(s.auto?' active':'');
    document.getElementById('autoOff').className='button'+(s.auto?'':' active');
    document.getElementById('mCharge').className='button'+(s.manual==='charge'?' active':'');
    document.getElementById('mQuiet').className='button'+(s.manual==='quiet'?' active':'');
    set('updated','recharge below '+s.recharge+'%');
  }catch(e){ document.getElementById('conn').textContent='no data'; }
}
refresh(); setInterval(refresh,2000);
</script>
</body></html>)HTML";

esp_err_t root_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t status_handler(httpd_req_t* req) {
  const AppSnapshot s = g_app.snapshot();
  const QuietControllerConfig c = g_app.config();
  const InverterStatus& st = s.status;

  char buf[640];
  const int n = std::snprintf(
      buf, sizeof(buf),
      "{\"online\":%s,\"age_s\":%lu,\"mode\":\"%s\",\"write_ok\":%s,"
      "\"battery_v\":%.1f,\"soc\":%d,\"grid\":%s,"
      "\"load_w\":%d,\"load_a\":%.1f,\"batt_w\":%d,\"batt_a\":%.1f,"
      "\"temp\":%.0f,\"fault\":%u,"
      "\"auto\":%s,\"manual\":\"%s\",\"recharge\":%d}",
      s.online ? "true" : "false", (unsigned long)(s.age_ms / 1000),
      to_string(s.mode), s.last_write_ok ? "true" : "false",
      st.battery_voltage, st.battery_soc, st.grid_present ? "true" : "false",
      st.load_power, st.load_current, (int)st.battery_power, st.battery_current,
      st.temperature, st.error_code, c.auto_enabled ? "true" : "false",
      to_string(c.manual_mode), c.recharge_soc);

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, n);
}

esp_err_t control_handler(httpd_req_t* req) {
  char query[64];
  int id = -1, val = 0;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char tmp[16];
    if (httpd_query_key_value(query, "id", tmp, sizeof(tmp)) == ESP_OK) id = atoi(tmp);
    if (httpd_query_key_value(query, "val", tmp, sizeof(tmp)) == ESP_OK) val = atoi(tmp);
  }
  if (id >= 0) {
    g_app.apply_control_dp(static_cast<uint8_t>(id), val);
  }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

}  // namespace

void start_web_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(kTag, "failed to start http server");
    return;
  }
  const httpd_uri_t root = {"/", HTTP_GET, root_handler, nullptr};
  const httpd_uri_t status = {"/api/status", HTTP_GET, status_handler, nullptr};
  const httpd_uri_t control = {"/api/control", HTTP_POST, control_handler, nullptr};
  httpd_register_uri_handler(server, &root);
  httpd_register_uri_handler(server, &status);
  httpd_register_uri_handler(server, &control);
  ESP_LOGI(kTag, "web server started");
}

}  // namespace silent_powmr
