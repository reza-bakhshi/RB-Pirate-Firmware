# Tasks

## 1. CMSIS-DAP v2 Debug Probe
- [x] 1.1 Implement USB bulk descriptor endpoints for WinUSB CMSIS-DAP v2 in `src/usb_descriptors.c`
- [x] 1.2 Implement CMSIS-DAP v2 command processing and SWD driver in `src/binmode/cmsis_dap.c`
- [x] 1.3 Verify target flash programming and breakpoint debugging with OpenOCD on an RP2040/RP2350 target

## 2. HSTX DVI Digital Video
- [x] 2.1 Configure RP2350 HSTX serializer for TMDS encoding and 640x480 DVI timing in `src/pirate/hstx_dvi.c`
- [x] 2.2 Implement character font renderer and scanline DMA ping-pong buffers
- [x] 2.3 Verify video signal stability and frame rendering on an HDMI/DVI monitor
