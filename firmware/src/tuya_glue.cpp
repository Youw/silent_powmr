// SmartLife (Tuya) integration. The DP <-> state mapping itself lives in the
// host-tested core (silent_powmr::dp); this file is only the SDK plumbing.
//
// NOTE: The exact TuyaOpen type/field/enum names (tuya_iot_config_t fields,
// dp_obj_t value union members, event union member for received DPs) can vary
// between TuyaOpen versions. They are written here to match the switch_demo
// reference; verify against the TuyaOpen tag pinned in firmware/README.md when
// you first build on the toolchain.
#include "tuya_glue.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inverter_app.hpp"
#include "silent_powmr/dp_map.h"
#include "tuya_config.h"
#include "tuya_iot.h"
#include "tuya_iot_dp.h"

namespace silent_powmr {
namespace {
constexpr char kTag[] = "tuya";
tuya_iot_client_t s_client;
volatile bool s_connected = false;

void report_now() {
  if (!s_connected) return;
  const AppSnapshot s = g_app.snapshot();
  const QuietControllerConfig c = g_app.config();

  dp::DpValue dps[16];
  size_t n = dp::build_telemetry(s.status, s.mode, dps, 13);
  n += dp::build_config_report(c, dps + n, 16 - n);

  dp_obj_t objs[16];
  for (size_t i = 0; i < n; ++i) {
    objs[i].id = dps[i].id;
    objs[i].time_stamp = 0;
    switch (dps[i].type) {
      case dp::DpType::kBool:
        objs[i].type = PROP_BOOL;
        objs[i].value.dp_bool = dps[i].value != 0;
        break;
      case dp::DpType::kEnum:
        objs[i].type = PROP_ENUM;
        objs[i].value.dp_enum = static_cast<uint32_t>(dps[i].value);
        break;
      case dp::DpType::kValue:
      default:
        objs[i].type = PROP_VALUE;
        objs[i].value.dp_value = dps[i].value;
        break;
    }
  }
  tuya_iot_dp_obj_report(&s_client, s_client.activate.devid, objs,
                         static_cast<uint32_t>(n), 0);
}

void on_dp_receive(dp_obj_recv_t* recv) {
  for (uint32_t i = 0; i < recv->dpscnt; ++i) {
    const dp_obj_t* d = &recv->dps[i];
    int32_t v = 0;
    switch (d->type) {
      case PROP_BOOL: v = d->value.dp_bool ? 1 : 0; break;
      case PROP_ENUM: v = static_cast<int32_t>(d->value.dp_enum); break;
      case PROP_VALUE: v = d->value.dp_value; break;
      default: continue;
    }
    g_app.apply_control_dp(d->id, v);
  }
  report_now();  // echo the new state back to the app
}

void user_event_handler_on(tuya_iot_client_t* /*client*/,
                           tuya_event_msg_t* event) {
  switch (event->id) {
    case TUYA_EVENT_MQTT_CONNECTED:
      s_connected = true;
      ESP_LOGI(kTag, "cloud connected");
      report_now();
      break;
    case TUYA_EVENT_MQTT_DISCONNECT:
      s_connected = false;
      break;
    case TUYA_EVENT_DP_RECEIVE_OBJ:
      on_dp_receive(&event->value.dpobj);
      break;
    default:
      break;
  }
}

void tuya_task(void*) {
  tuya_iot_config_t cfg = {};
  cfg.software_ver = "1.0.0";
  cfg.productkey = TUYA_PRODUCT_ID;
  cfg.uuid = TUYA_OPENSDK_UUID;
  cfg.authkey = TUYA_OPENSDK_AUTHKEY;
  cfg.event_handler = user_event_handler_on;

  tuya_iot_init(&s_client, &cfg);
  tuya_iot_start(&s_client);

  uint32_t last_report = 0;
  for (;;) {
    tuya_iot_yield(&s_client);
    const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_report > 10000) {
      last_report = now;
      report_now();
    }
  }
}
}  // namespace

void tuya_glue_start() {
  xTaskCreate(tuya_task, "tuya", 8192, nullptr, 4, nullptr);
}

}  // namespace silent_powmr
