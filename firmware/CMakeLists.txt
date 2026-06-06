# TuyaOpen application build file for silent_powmr.
#
# Follows the TuyaOpen app convention (see apps/tuya_cloud/switch_demo): the
# build provides ${EXAMPLE_LIB}; we attach our sources and includes to it.
# Crucially we compile the portable core/ sources directly — the very same code
# the host unit tests cover — so the on-device control logic is already proven.

# Firmware glue (UART, Tuya, web) auto-discovered from src/.
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/src APP_SRCS)

# Portable, hardware-independent core (reused verbatim from the repo root).
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/../core/src CORE_SRCS)
list(APPEND APP_SRCS ${CORE_SRCS})

set(APP_INC
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/../core/include
)

target_sources(${EXAMPLE_LIB} PRIVATE ${APP_SRCS})
target_include_directories(${EXAMPLE_LIB} PRIVATE ${APP_INC})
