# Portable, hardware-independent core logic (C++17, no exceptions/RTTI required,
# no heap in hot paths). Built as a static lib for host tests and reused verbatim
# by the ESP32 firmware as an IDF component.
add_library(silent_powmr_core STATIC
  src/crc16.cpp
  src/modbus.cpp
  src/registers.cpp
  src/quiet_controller.cpp
  src/dp_map.cpp
  src/inverter_client.cpp
  src/control_cycle.cpp
)

target_include_directories(silent_powmr_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(silent_powmr_core PUBLIC cxx_std_17)
