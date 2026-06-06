// Firmware entry point. TuyaOpen's ESP32 platform calls tuya_app_main(); we
// spin up our init on a task so we don't block the platform thread.
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inverter_app.hpp"
#include "nvs_flash.h"
#include "tuya_glue.hpp"
#include "web_server.hpp"

namespace {
constexpr char kTag[] = "main";

void app_init_task(void*) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  esp_netif_init();
  esp_event_loop_create_default();

  ESP_LOGI(kTag, "starting silent_powmr");
  silent_powmr::g_app.begin();        // autonomous inverter control loop
  silent_powmr::start_web_server();   // local mobile-friendly status page
  silent_powmr::tuya_glue_start();    // SmartLife connectivity + DPs

  vTaskDelete(nullptr);
}
}  // namespace

extern "C" void tuya_app_main(void) {
  xTaskCreate(app_init_task, "spowmr_init", 8192, nullptr, 5, nullptr);
}
