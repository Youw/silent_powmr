#pragma once
namespace silent_powmr {
// Initialize the Tuya IoT client, start cloud connectivity, and run the DP
// report/receive loop on its own task.
void tuya_glue_start();
}  // namespace silent_powmr
