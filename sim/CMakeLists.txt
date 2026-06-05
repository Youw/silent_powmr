# Host-only simulated inverter (uses std::map; not built into firmware).
add_library(silent_powmr_sim STATIC
  src/sim_inverter.cpp
)

target_include_directories(silent_powmr_sim PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(silent_powmr_sim PUBLIC silent_powmr_core)
target_compile_features(silent_powmr_sim PUBLIC cxx_std_17)
