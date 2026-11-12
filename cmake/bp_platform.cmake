# Copyright (c) 2026 RB-Pirate contributors. SPDX-License-Identifier: MIT
# Validate before importing the SDK: never silently substitute another MCU.
if(NOT DEFINED PICO_PLATFORM AND NOT "$ENV{PICO_PLATFORM}" STREQUAL "")
    set(PICO_PLATFORM "$ENV{PICO_PLATFORM}")
endif()
if(NOT DEFINED PICO_BOARD AND NOT "$ENV{PICO_BOARD}" STREQUAL "")
    set(PICO_BOARD "$ENV{PICO_BOARD}")
endif()
foreach(bp_platform_option IN ITEMS PICO_PLATFORM BP_PICO_PLATFORM)
    if(DEFINED ${bp_platform_option} AND
       NOT "${${bp_platform_option}}" MATCHES "^(rp2350|rp2350-arm-s)$")
        message(FATAL_ERROR
            "Unsupported ${bp_platform_option}='${${bp_platform_option}}'. "
            "This firmware supports only RP2350 Arm Secure on board rb-pirate. "
            "Use -DPICO_PLATFORM=rp2350 -DPICO_BOARD=rb-pirate. "
            "RP2040 and other boards require a separate port.")
    endif()
endforeach()
if(DEFINED PICO_BOARD AND NOT PICO_BOARD STREQUAL "rb-pirate")
    message(FATAL_ERROR
        "Unsupported PICO_BOARD='${PICO_BOARD}'. Only rb-pirate is supported; "
        "use -DPICO_PLATFORM=rp2350 -DPICO_BOARD=rb-pirate.")
endif()
if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM "rp2350" CACHE STRING "Supported RP2350 SDK platform")
endif()
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "rb-pirate" CACHE STRING "Supported board")
endif()
unset(bp_platform_option)
