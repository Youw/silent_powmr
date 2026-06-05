add_executable(silent_powmr_tests
  test_main.cpp
  test_crc16.cpp
  test_modbus.cpp
  test_registers.cpp
  test_quiet_controller.cpp
  test_dp_map.cpp
  test_integration.cpp
)

target_link_libraries(silent_powmr_tests PRIVATE silent_powmr_core silent_powmr_sim)
target_include_directories(silent_powmr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME unit COMMAND silent_powmr_tests)
