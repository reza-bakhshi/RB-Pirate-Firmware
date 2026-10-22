
# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()  

#  set all the warnings to help reduce bugs
# set(BP_PENDANTIC 1)


# build time in UTC ISO 8601
FILE (WRITE ${CMAKE_BINARY_DIR}/timestamp.cmake "STRING(TIMESTAMP TIMEZ UTC)\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(WRITE timestamp.h \"#ifndef TIMESTAMP_H\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(APPEND timestamp.h \"#define TIMESTAMP_H\\n\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(APPEND timestamp.h \"#define _TIMEZ_ \\\"\${TIMEZ}\\\"\\n\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(APPEND timestamp.h \"#endif // TIMESTAMP_H\\n\")\n")
ADD_CUSTOM_TARGET (
    timestamp
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/timestamp.cmake
    ADD_DEPENDENCIES ${CMAKE_BINARY_DIR}/timestamp.cmake)

# Add executable. Default name is the project name, version 0.1
set(buspirate_common
        # main loop and printf
        pirate.c
        pirate.h
        printf-4.0.0/printf.c
        printf-4.0.0/printf.h
        modes.c
        modes.h
        system_config.h
        system_config.c
        system_monitor.h
        system_monitor.c

        #flatcc test
        bpio_builder.h
        bpio_reader.h
        bpio_verifier.h
        monster_verifier.h 
        flatbuffers_common_builder.h 
        flatbuffers_common_reader.h
        monster_builder.h
        monster_reader.h
        builder.c
        emitter.c 
        json_parser.c 
        json_printer.c 
        refmap.c 
        verifier.c 
        flatcc/flatcc_portable.h
        flatcc/flatcc_rtconfig.h
        flatcc/flatcc_unaligned.h
        flatcc/flatcc_flatbuffers.h
        flatcc/flatcc_verifier.h
        flatcc/flatcc_prologue.h
        flatcc/flatcc.h
        flatcc/flatcc_json_printer.h
        flatcc/flatcc_assert.h
        flatcc/flatcc_json_parser.h
        flatcc/flatcc_version.h
        flatcc/flatcc_endian.h
        flatcc/flatcc_emitter.h
        flatcc/flatcc_accessors.h
        flatcc/flatcc_refmap.h
        flatcc/flatcc_types.h
        flatcc/flatcc_builder.h
        flatcc/flatcc_alloc.h
        flatcc/flatcc_identifier.h
        flatcc/flatcc_epilogue.h
        flatcc/flatcc_iov.h
        flatcc/support/elapsed.h
        flatcc/support/cdump.h
        flatcc/support/hexdump.h
        flatcc/support/readfile.h
        flatcc/reflection/reflection_verifier.h
        flatcc/reflection/reflection_reader.h
        flatcc/reflection/reflection_builder.h
        flatcc/reflection/flatbuffers_common_builder.h
        flatcc/reflection/flatbuffers_common_reader.h
        flatcc/portable/portable.h
        flatcc/portable/pinttypes.h
        flatcc/portable/pdiagnostic_push.h
        flatcc/portable/pinline.h
        flatcc/portable/pendian_detect.h
        flatcc/portable/pversion.h
        flatcc/portable/pdiagnostic_pop.h
        flatcc/portable/pstdalign.h
        flatcc/portable/pstatic_assert_scope.h
        flatcc/portable/punaligned.h
        flatcc/portable/grisu3_math.h
        flatcc/portable/paligned_alloc.h
        flatcc/portable/pprintfp.h
        flatcc/portable/pattributes.h
        flatcc/portable/pmemaccess.h
        flatcc/portable/pstdint.h
        flatcc/portable/pstdbool.h
        flatcc/portable/pwarnings.h
        flatcc/portable/portable_basic.h
        flatcc/portable/pprintint.h
        flatcc/portable/pparseint.h
        flatcc/portable/grisu3_parse.h
        flatcc/portable/grisu3_print.h
        flatcc/portable/prestrict.h
        flatcc/portable/pbase64.h
        flatcc/portable/pparsefp.h
        flatcc/portable/pdiagnostic.h
        flatcc/portable/pendian.h
        flatcc/portable/pcrt.h
        flatcc/portable/pstatic_assert.h
        flatcc/portable/include/linux/endian.h
        flatcc/portable/include/std/inttypes.h
        flatcc/portable/include/std/stdbool.h
        flatcc/portable/include/std/stdint.h
        flatcc/portable/include/std/stdalign.h
        lib/nanocobs/cobs.c
        lib/nanocobs/cobs.h


        # pirate lib
        pirate/psu.h
        pirate/psu.c
        pirate/pullup.h
        pirate/pullup.c
        pirate/mcu.h
        pirate/mcu.c
        pirate/lsb.c
        pirate/lsb.h
        pirate/bio.h
        pirate/bio.c
        pirate/storage.h
        pirate/storage.c
        pirate/button.h
        pirate/button.c
        pirate/mem.c
        pirate/mem.h
        pirate/amux.h
        pirate/amux.c
        pirate/rgb.h
        pirate/rgb.c
        pirate/hardfault.c
        pirate/intercore_helpers.c
        pirate/file.h 
        pirate/file.c

        # libraries
        lib/jep106/jep106.c
        lib/jep106/jep106.h

        # commands
        commands.h
        commands.c
        commands/global/w_psu.h
        commands/global/w_psu.c
        commands/global/p_pullups.h
        commands/global/p_pullups.c
        commands/global/cmd_mcu.h
        commands/global/cmd_mcu.c
        commands/global/l_bitorder.h
        commands/global/l_bitorder.c
        commands/global/cmd_convert.h
        commands/global/cmd_convert.c
        commands/global/pause.h
        commands/global/pause.c
        commands/global/h_help.h
        commands/global/h_help.c
        commands/global/cmd_selftest.h
        commands/global/cmd_selftest.c
        commands/global/a_auxio.h
        commands/global/a_auxio.c
        commands/global/dummy.c
        commands/global/dummy.h
        commands/global/hash.c
        commands/global/hash.h
        commands/global/menu_demo.c
        commands/global/menu_demo.h
        commands/global/txtest.c
        commands/global/txtest.h
        commands/global/disk.c
        commands/global/disk.h
        commands/global/v_adc.c
        commands/global/v_adc.h
        commands/global/i_info.c
        commands/global/i_info.h
        commands/global/pwm.c
        commands/global/pwm.h
        commands/global/freq.c
        commands/global/freq.h
        commands/global/macro.c
        commands/global/macro.h
        commands/global/script.c
        commands/global/script.h
        commands/global/edit.c
        commands/global/edit.h
        lib/kilo/kilo_build.c
        lib/kilo/kilo_compat.c
        lib/kilo/kilo_compat.h
        commands/global/hexedit.c
        commands/global/hexedit.h
        lib/hx/hx_build.c
        lib/hx/hx_compat.h
        lib/vt100_keys/vt100_keys.c
        lib/vt100_keys/vt100_keys.h
        lib/vt100_menu/vt100_menu.c
        lib/vt100_menu/vt100_menu.h
        ui/ui_cmd_menu.c
        ui/ui_cmd_menu.h
        commands/global/button_scr.c
        commands/global/button_scr.h
        commands/global/smps.c
        commands/global/smps.h
        commands/global/cls.h
        commands/global/cls.c  
        commands/global/cmd_binmode.h
        commands/global/cmd_binmode.c
        commands/global/logic.h
        commands/global/logic.c
        commands/global/bug.c
        commands/global/bug.h
        commands/global/dump.c
        commands/global/dump.h
        commands/global/ovrclk.c 
        commands/global/ovrclk.h   
        commands/global/hex.c     
        commands/global/hex.h
        commands/global/game_engine.c
        commands/global/game_engine.h
        commands/global/life.c
        commands/global/life.h
        commands/global/snake.c
        commands/global/snake.h
        commands/global/game2048.c
        commands/global/game2048.h
        commands/global/tictactoe.c
        commands/global/tictactoe.h
        commands/global/hangman.c
        commands/global/hangman.h
        commands/global/mines.c
        commands/global/mines.h
        commands/global/bricks.c
        commands/global/bricks.h
        commands/global/drop4.c
        commands/global/drop4.h
        commands/global/fleet.c
        commands/global/fleet.h
        commands/global/worm.c
        commands/global/worm.h
        commands/global/pcbrun.c
        commands/global/pcbrun.h
        commands/global/invaders.c
        commands/global/invaders.h
        commands/global/sigride.c
        commands/global/sigride.h
        commands/global/stkover.c
        commands/global/stkover.h
        commands/global/crossflash.c
        commands/global/crossflash.h
        commands/global/cryptocrack.c
        commands/global/cryptocrack.h
        commands/global/wiretrace.c
        commands/global/wiretrace.h
        commands/global/logicgates.c
        commands/global/logicgates.h
        commands/global/rogueprobe.c
        commands/global/rogueprobe.h
        commands/eeprom/eeprom_base.h
        commands/eeprom/eeprom_base.c
        
        # HiZ
        mode/hiz.h
        mode/hiz.c

        # 1Wire
        mode/hw1wire.h
        mode/hw1wire.c
        pirate/hw1wire_pio.h
        pirate/hw1wire_pio.c
        commands/1wire/scan.h
        commands/1wire/scan.c
        commands/1wire/demos.h
        commands/1wire/demos.c
        pirate/onewire_library.h
        pirate/onewire_library.c
        commands/eeprom/eeprom_1wire.h
        commands/eeprom/eeprom_1wire.c
        commands/global/jep106_lookup.c
        commands/global/jep106_lookup.h

        # UART
        mode/hwuart.h
        mode/hwuart.c
        pirate/hwuart_pio.h
        pirate/hwuart_pio.c
        commands/uart/nmea.c
        commands/uart/nmea.h
        commands/uart/bridge.h
        commands/uart/bridge.c
        commands/uart/simcard.h
        commands/uart/simcard.c
        lib/minmea/minmea.h
        lib/minmea/minmea.c
        lib/minmea/gps.h
        lib/minmea/gps.c
        commands/uart/monitor.h
        commands/uart/monitor.c
        commands/uart/glitch.h
        commands/uart/glitch.c

        # CELL: cellular AT modem commands (UART mode)
        commands/cell/cell.h
        commands/cell/cell.c
        commands/cell/cell_at.c

        # Half duplex UART
        mode/hwhduart.h
        mode/hwhduart.c
        commands/hdplxuart/bridge.c
        commands/hdplxuart/bridge.h

        # I2C 
        mode/hwi2c.c
        mode/hwi2c.h
        pirate/hwi2c_pio.c
        pirate/hwi2c_pio.h
        commands/i2c/scan.h
        commands/i2c/scan.c
        commands/i2c/demos.h
        commands/i2c/demos.c
        lib/i2c_address_list/dev_i2c_addresses.h
        lib/ms5611/ms5611.c
        lib/ms5611/ms5611.h
        lib/tsl2561/driver_tsl2561.c
        lib/tsl2561/driver_tsl2561.h
        commands/i2c/sniff.c 
        commands/i2c/sniff.h
        commands/i2c/ddr5.c
        commands/i2c/ddr5.h
        commands/i2c/ddr4.c
        commands/i2c/ddr4.h        
        commands/eeprom/eeprom_i2c.c 
        commands/eeprom/eeprom_i2c.h
        commands/eeprom/eeprom_i2c_gui.c
        commands/eeprom/eeprom_i2c_gui.h
        commands/i2c/i2c.c
        commands/i2c/i2c.h
        commands/i2c/usbpdo.c 
        commands/i2c/usbpdo.h
        commands/i2c/usbpd.c 
        commands/i2c/usbpd.h
        lib/ap33772s/ap33772s.c
        lib/ap33772s/ap33772s.h 
        lib/mpu6050_light/mpu6050_light.h
        lib/mpu6050_light/mpu6050_light.c
        commands/i2c/mpu6050.c
        commands/i2c/mpu6050.h

        # SPI
        mode/hwspi.c
        mode/hwspi.h
        pirate/hwspi.c
        pirate/hwspi.h
        commands/spi/flash.c
        commands/spi/flash.h
        commands/spi/spiflash.h
        commands/spi/spiflash.c
        #commands/spi/sniff.c
        #commands/spi/sniff.h
        lib/sfud/inc/sfud.h
        lib/sfud/inc/sfud.c
        lib/sfud/inc/sfud_cfg.h
        lib/sfud/inc/sfud_def.h
        lib/sfud/inc/sfud_flash_def.h
        lib/sfud/inc/sfud_port.c
        lib/sfud/inc/sfud_sfdp.c
        commands/eeprom/eeprom_spi.h
        commands/eeprom/eeprom_spi.c

        # 2WIRE
        mode/hw2wire.h
        mode/hw2wire.c
        pirate/hw2wire_pio.h
        pirate/hw2wire_pio.c
        commands/2wire/sle4442.c
        commands/2wire/sle4442.h
        commands/2wire/hw2w_sniff.c 
        commands/2wire/hw2w_sniff.h

        # 3WIRE
        mode/hw3wire.h
        mode/hw3wire.c
        pirate/hw3wire_pio.h
        pirate/hw3wire_pio.c


        # LED
        mode/hwled.c
        mode/hwled.h

        # DIO
        mode/dio.h
        mode/dio.c

        #Infrared
        mode/infrared.h
        mode/infrared.c 
        lib/pico_ir_nec/nec_transmit.c
        lib/pico_ir_nec/nec_transmit.h
        lib/pico_ir_nec/nec_receive.c
        lib/pico_ir_nec/nec_receive.h
        pirate/rc5_pio.c
        pirate/rc5_pio.h
        mode/infrared-struct.h
        commands/infrared/tvbgone.c
        commands/infrared/tvbgone.h
        commands/infrared/tvbgone-codes.h
        commands/infrared/ir_common.c
        commands/infrared/ir_common.h
        commands/infrared/ir_flipper.c
        commands/infrared/ir_flipper.h
        commands/infrared/ir_send.c
        commands/infrared/ir_send.h
        commands/infrared/ir_receive.c
        commands/infrared/ir_receive.h
        commands/infrared/ir_setprotocol.c
        commands/infrared/ir_setprotocol.h
        commands/infrared/ir_devicebgone.c
        commands/infrared/ir_devicebgone.h
        commands/infrared/ir_remote.c
        commands/infrared/ir_remote.h
        commands/infrared/ir_record.c
        commands/infrared/ir_record.h
        commands/infrared/ir_load.c
        commands/infrared/ir_load.h
        commands/infrared/ir_replay.c
        commands/infrared/ir_replay.h
        commands/infrared/ir_jam.c
        commands/infrared/ir_jam.h
        commands/infrared/ir_config.c
        commands/infrared/ir_config.h
        pirate/ir_protocols.c
        pirate/ir_protocols.h
        lib/makehex/MakeHex.cpp
        lib/makehex/MakeHex.h
        lib/makehex/irp_definitions.c
        lib/makehex/irp_definitions.h
        lib/picofreq/picofreq.c
        lib/picofreq/picofreq.h
        commands/infrared/irtxrx.c
        commands/infrared/irtxrx.h

        # CC1101 Sub-GHz radio
        mode/subghz.c
        mode/subghz.h
        mode/subghz_protocols.c
        mode/subghz_protocols.h
        mode/subghz_blocks.c
        mode/subghz_blocks.h
        mode/subghz_rx.c
        mode/subghz_rx.h
        mode/subghz_keystore.c
        mode/subghz_keystore.h
        mode/subghz_file.c
        mode/subghz_file.h
        mode/subghz_cap.c
        mode/subghz_cap.h
        mode/subghz_freqs.c
        mode/subghz_freqs.h
        pirate/cc1101.c
        pirate/cc1101.h

        # LCDTap passive display capture (MIT, https://github.com/shapoco/lcdtap)
        mode/lcdtap.c
        mode/lcdtap.h
        mode/lcdtap_backend.cpp
        mode/lcdtap_backend.h
        lib/lcdtap/src/config.cpp
        lib/lcdtap/src/lcdtap.cpp
        lib/lcdtap/src/spi_display_base.cpp
        lib/lcdtap/src/st7789_controller.cpp
        lib/lcdtap/src/ili9341_controller.cpp
        lib/lcdtap/src/ssd1306_controller.cpp
        lib/lcdtap/src/ssd1331_controller.cpp
        lib/lcdtap/src/st7032_controller.cpp
        lib/lcdtap/src/ks0108_controller.cpp
        lib/lcdtap/pico2/src/spi_slave.cpp
        lib/lcdtap/pico2/src/i2c_slave.cpp

        # nRF24L01 2.4GHz radio
        mode/rf24.c
        mode/rf24.h
        pirate/nrf24l01.c
        pirate/nrf24l01.h

        # PN532 RFID/NFC
        mode/rfid.c
        mode/rfid.h
        pirate/pn532.c
        pirate/pn532.h

        # JTAG
        mode/jtag.h
        mode/jtag.c
        commands/jtag/bluetag.c
        commands/jtag/bluetag.h
        lib/bluetag/src/blueTag.c
        lib/bluetag/src/blueTag.h
        lib/bluetag/src/jep106.inc

        # PS2
        mode/ps2.h
        mode/ps2.c

        # USB
        mode/usb.h
        mode/usb.c

        # DUMMY MODE
        mode/dummy1.c
        mode/dummy1.h 

        # UI
        ui/ui_cmdln.c
        ui/ui_cmdln.h
        ui/ui_process.h
        ui/ui_process.c
        ui/ui_flags.h
        ui/ui_parse.c
        ui/ui_parse.h
        lib/bp_number/bp_number.c
        lib/bp_number/bp_number.h
        lib/bp_args/bp_cmd.c
        lib/bp_args/bp_cmd.h
        lib/bp_args/bp_cmd_linenoise.c
        lib/bp_args/bp_cmd_linenoise.h
        ui/ui_prompt.h
        ui/ui_prompt.c
        ui/ui_mode.c
        ui/ui_mode.h
        ui/ui_info.h
        ui/ui_info.c
        ui/ui_format.c
        ui/ui_init.c
        ui/ui_init.h
        ui/ui_const.h
        ui/ui_const.c
        ui/ui_config.h
        ui/ui_config.c
        ui/ui_help.c
        ui/ui_help.h
        ui/ui_term.c
        ui/ui_term.h
        ui/ui_term_linenoise.c
        ui/ui_term_linenoise.h
        ui/ui_statusbar.c
        ui/ui_statusbar.h
        ui/ui_toolbar.c
        ui/ui_toolbar.h
        ui/ui_pin_render.c
        ui/ui_pin_render.h
        ui/ui_hex.c
        ui/ui_hex.h
        ui/ui_file_picker.c
        ui/ui_file_picker.h
        ui/ui_progress_indicator.h
        ui/ui_progress_indicator.c
        ui/ui_app.h
        ui/ui_app.c
        ui/ui_popup.h
        ui/ui_popup.c
        ui/ui_config_bar.h
        ui/ui_config_bar.c
        ui/ui_mem_gui.h
        ui/ui_mem_gui.c


        # Debug
        debug_uart.c
        debug_uart.h
        debug_rtt.c
        debug_rtt.h

        # JSON parser for saved settings
        mjson/mjson.h
        mjson/mjson.c

        # syntax and commands
        syntax.h
        syntax_internal.h
        syntax_compile.c
        syntax_run.c
        syntax_post.c
        syntax_struct.h
        command_struct.h
        bytecode.h
        pio_config.h

        # FATfs
        fatfs/diskio.c
        fatfs/diskio.h
        fatfs/ff.c
        fatfs/ff.h
        fatfs/ffconf.h
        fatfs/ffsystem.c
        fatfs/ffunicode.c

        # translations
        translation/base.h
        translation/base.c
        translation/en-us.h
        translation/zh-cn.h
        translation/pl-pl.h

        # binary mode
        binmode/binmodes.c
        binmode/binmodes.h
        binmode/binio_helpers.c
        binmode/binio_helpers.h
        mode/binloopback.c
        mode/binloopback.h
        binmode/logicanalyzer.h
        binmode/logicanalyzer.c
        binmode/sump.c
        binmode/sump.h
        binmode/bpio.c
        binmode/bpio.h
        binmode/bpio_transactions.h
        binmode/bpio_1wire.c
        binmode/bpio_1wire.h
        binmode/bpio_i2c.c
        binmode/bpio_i2c.h
        binmode/bpio_spi.c
        binmode/bpio_spi.h
        binmode/bpio_2wire.c
        binmode/bpio_2wire.h
        binmode/bpio_3wire.c
        binmode/bpio_3wire.h
        binmode/bpio_uart.c
        binmode/bpio_uart.h
        binmode/bpio_hiz.c
        binmode/bpio_hiz.h
        binmode/bpio_dio.c
        binmode/bpio_dio.h
        binmode/bpio_led.c
        binmode/bpio_led.h
        binmode/bpio_infrared.c
        binmode/bpio_infrared.h
        binmode/legacy4third.c
        binmode/legacy4third.h
        lib/arduino-ch32v003-swio/arduino_ch32v003.c
        lib/arduino-ch32v003-swio/arduino_ch32v003.h
        lib/arduino-ch32v003-swio/swio.h
        lib/arduino-ch32v003-swio/swio.c
        lib/picorvd/picoswio.c
        lib/picorvd/picoswio.h
        lib/picorvd/debug_defines.h
        binmode/fala.c
        binmode/fala.h
        binmode/falaio.c
        binmode/falaio.h
        binmode/irtoy-air.h
        binmode/irtoy-air.c
        pirate/irio_pio.h
        pirate/irio_pio.c
        binmode/irtoy-irman.h
        binmode/irtoy-irman.c

        #toolbars
        toolbars/logic_bar.c
        toolbars/logic_bar.h
        toolbars/sys_stats.c
        toolbars/sys_stats.h
        toolbars/pin_watcher.c
        toolbars/pin_watcher.h
        commands/global/cmd_toolbar.c
        commands/global/cmd_toolbar.h

        # USB - SPSC queues for inter-core communication
        spsc_queue.h
        usb_tx.c
        usb_tx.h
        usb_rx.c
        usb_rx.h

        # Line editing library (linenoise with BP_EMBEDDED support)
        lib/bp_linenoise/linenoise.h
        lib/bp_linenoise/linenoise.c
        lib/bp_linenoise/ln_cmdreader.h
        lib/bp_linenoise/ln_cmdreader.c

        # Expression evaluator library
        lib/bp_expr/bp_expr.h
        lib/bp_expr/bp_expr.c        

        msc_disk.c
        msc_disk.h
        usb_descriptors.c

	# USBPD 
        #mode/usbpd.c            
        #mode/usbpd.h

        # I2S
        mode/i2s.c
        mode/i2s.h
        commands/i2s/sine.h 
        commands/i2s/sine.c
        commands/i2s/wav.h
        commands/i2s/wav.c
)
set (buspirate_rtt
        lib/rtt/Config/SEGGER_RTT_Conf.h
        lib/rtt/RTT/SEGGER_RTT.c
        lib/rtt/RTT/SEGGER_RTT.h
        lib/rtt/RTT/SEGGER_RTT_printf.c
)

set (rp2350_common
        commands/global/otpdump.h 
        commands/global/otpdump.c
)


set(revisions bus_pirate6)

add_executable(bus_pirate6
                ${buspirate_common}
                fatfs/tf_card.c
                fatfs/tf_card.h
                ${buspirate_rtt}
                ${rp2350_common}
                platform/bpi6-rev2.h
                platform/bpi6-rev2.c
                )
target_compile_definitions(bus_pirate6 PUBLIC BP_VER=6)
target_compile_definitions(bus_pirate6 PUBLIC BP_REV=10)
target_compile_definitions(bus_pirate6 PUBLIC BP_FIRMWARE_HASH="${BP_FIRMWARE_HASH}")
if(DEFINED BP_PENDANTIC)
        target_compile_options(bus_pirate6 PRIVATE -Wpedantic -Wall -Werror -O3 -Wno-unknown-pragmas -Wno-inline -Wno-unused-function)
endif()


set(stdlibs
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_dma
        hardware_pio
        hardware_interp
        hardware_timer
        hardware_watchdog
        hardware_clocks
        hardware_flash
        hardware_adc
        hardware_uart
        pico_multicore
        pico_base_headers
        pico_unique_id
        pico_bootrom 
        hardware_pwm
        tinyusb_device
        tinyusb_board
        hardware_structs
        )        



# Compile the various PIO programs to generate the header files
foreach(revision ${revisions})
        ADD_DEPENDENCIES (${revision} timestamp)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/mode/pico_ps2_sniffer.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/mode/pico_usb_sniffer_lite.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/hw2wire.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/hw3wire.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/hwi2c.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/ws2812.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/apa102.pio) 
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/hw1wire.pio) 
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/hwuart.pio)  
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/spisnif.pio)  
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/binmode/logicanalyzer.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/lib/pico_ir_nec/nec_carrier_burst.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/lib/pico_ir_nec/nec_carrier_control.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/lib/pico_ir_nec/nec_receive.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/rc5.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/lib/picorvd/ch32vswio.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/pwm.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/onewire_library.pio)
        #pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/lib/pio_pwm/pwm.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/irio.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/commands/uart/glitch.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/lib/pico-i2c-sniff/i2c_sniffer.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/lib/pico-i2c-sniff/hw2w_sniffer.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/mode/i2s_out.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/mode/i2s_in.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/lib/lcdtap/pico2/src/spi_4line_mode0.pio)
        pico_generate_pio_header(${revision} ${CMAKE_CURRENT_LIST_DIR}/lib/lcdtap/pico2/src/spi_3line_mode0.pio)

        pico_set_program_name(${revision} ${revision}) 
        pico_set_program_version(${revision} "0.1.0")
        pico_set_program_description(${revision} "RB-Pirate Firmware for RP2350")
        pico_enable_stdio_uart(${revision} 0)
        pico_enable_stdio_usb(${revision} 0)

        #XOSC fix?
        target_compile_definitions(${revision} PUBLIC PICO_XOSC_STARTUP_DELAY_MULTIPLIER=172)
        # useful for debug builds
        #target_compile_definitions(${revision} PUBLIC PICO_USE_STACK_GUARDS=$<CONFIG:DEBUG>)
        target_compile_definitions(${revision} PUBLIC PICO_USE_STACK_GUARDS=1)
        target_compile_definitions(${revision} PUBLIC PICO_STACK_SIZE=4096)        
        target_compile_definitions(${revision} PRIVATE PICO_MALLOC_PANIC=0)
        target_compile_definitions(${revision} PUBLIC BP_EMBEDDED=1)

        # Add to your foreach(revision ${revisions}) loop after target_link_libraries
        target_link_options(${revision} PRIVATE 
        "LINKER:-Map=${CMAKE_BINARY_DIR}/${revision}.map"
        "LINKER:--print-memory-usage"
        "LINKER:--cref"
        )

        # Add the standard library to the build
        target_link_libraries(${revision} ${stdlibs} )
        target_include_directories(${revision} PUBLIC
                ${CMAKE_CURRENT_LIST_DIR}
                .
                ${CMAKE_CURRENT_LIST_DIR}/lib/lcdtap/include
                ${CMAKE_CURRENT_LIST_DIR}/lib/lcdtap/pico2/include
        )
        pico_add_extra_outputs(${revision})
        pico_set_linker_script(${revision} ${CMAKE_CURRENT_LIST_DIR}/boards/memmap_default_rp2350.ld)
        #pico_set_linker_script(${revision} ${CMAKE_CURRENT_LIST_DIR}/pirate/memmap_default.ld)
endforeach()

string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--cref,--print-memory-usage")


# by default this project uses LGPL3 libraries, if you do not want to comply
# with LGPL3 license, please set the following variable to FALSE, otherwise
# you are oblicated to distribute the resulted binaries under LGPL3 license
set(USE_LGPL3 TRUE CACHE BOOL "indicated if to include LGPL3 protected code")
include(../cmake/ansi_colours_import.cmake)
if(USE_LGPL3)
if(LEGACY_ANSI_COLOURS_ENABLED)
        target_link_libraries(bus_pirate6 lib_ansi_colours)
endif()
endif()

 
