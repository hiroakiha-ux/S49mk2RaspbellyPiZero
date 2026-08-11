cmake_minimum_required(VERSION 3.16)
project(s49mk2_bridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(BUILD_TESTS "Build the programs under tests/" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

# Header-only-ish utilities (hex dump for --dry-run logging and tests/).
# Portable: no USB/ALSA/Linux-specific headers, builds anywhere.
add_library(mk2_util
  src/util/hex_dump.cpp
)
target_include_directories(mk2_util PUBLIC include)

add_library(mk2_usb
  src/usb/hid_device.cpp
  src/usb/bulk_display_device.cpp
)
target_include_directories(mk2_usb PUBLIC include)
target_link_libraries(mk2_usb PUBLIC PkgConfig::LIBUSB)

# Portable: RGB565 canvas + packet builder are pure logic, no USB dependency.
# (LcdBulkDevice, which does need libusb, lives in mk2_usb and is linked in
# separately by whatever wants to actually transmit a built packet.)
add_library(mk2_display
  src/display/lcd_canvas.cpp
  src/display/lcd_packet.cpp
)
target_include_directories(mk2_display PUBLIC include)

add_library(mk2_midi
  src/midi/alsa_rawmidi_port.cpp
  src/midi/router.cpp
  src/midi/seqtrak_sysex.cpp
)
target_include_directories(mk2_midi PUBLIC include)
target_link_libraries(mk2_midi PUBLIC PkgConfig::ALSA mk2_util)

add_library(mk2_seq
  src/seq/step_sequencer.cpp
)
target_include_directories(mk2_seq PUBLIC include)
target_link_libraries(mk2_seq PUBLIC mk2_midi)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # ARMv6 (Raspberry Pi Zero) needs libatomic for 64-bit atomic operations.
  target_link_libraries(mk2_seq PUBLIC atomic)
endif()

add_executable(s49mk2_bridge
  src/app/main.cpp
  src/app/controller_app.cpp
)
target_include_directories(s49mk2_bridge PRIVATE include)
target_link_libraries(s49mk2_bridge PRIVATE
  mk2_usb
  mk2_display
  mk2_midi
  mk2_seq
  mk2_util
  pthread
)

install(TARGETS s49mk2_bridge RUNTIME DESTINATION bin)

if(BUILD_TESTS)
  # Hardware-free: validates LCD packet byte layout against known-good
  # vectors from the original Python project's test suite. Only needs
  # mk2_display + mk2_util, so it builds on any host (including macOS).
  add_executable(lcd_packet_selftest
    tests/lcd_packet_selftest.cpp
  )
  target_include_directories(lcd_packet_selftest PRIVATE include)
  target_link_libraries(lcd_packet_selftest PRIVATE mk2_display mk2_util)

  # Hardware-free: validates SEQTRAK SysEx message builders/parser against
  # hand-computed vectors from SEQTRAK_data_list_En_D0.pdf. Pure logic, no
  # ALSA dependency, builds anywhere.
  add_executable(seqtrak_sysex_selftest
    tests/seqtrak_sysex_selftest.cpp
    src/midi/seqtrak_sysex.cpp
  )
  target_include_directories(seqtrak_sysex_selftest PRIVATE include)

  # Read-only HID report dump. Uses Linux hidraw directly, so it only makes
  # sense (and only builds) on Linux, i.e. the Pi itself. Also links
  # mk2_display for the optional --wake-lcd test rectangle (see the flag's
  # doc comment in tests/hid_input_dump.cpp).
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(hid_input_dump
      tests/hid_input_dump.cpp
    )
    target_include_directories(hid_input_dump PRIVATE include)
    target_link_libraries(hid_input_dump PRIVATE mk2_usb mk2_display mk2_util)

    # One-shot visual check: draws small/medium/large text (font8x8_basic)
    # to the left LCD. Needs the LCD bulk endpoint (libusb), same gate as
    # hid_input_dump above for consistency (both are real-hardware checks).
    add_executable(lcd_text_demo
      tests/lcd_text_demo.cpp
    )
    target_include_directories(lcd_text_demo PRIVATE include)
    target_link_libraries(lcd_text_demo PRIVATE mk2_usb mk2_display)

    # Japanese/ASCII text demo (Shinonome 16-dot font), small/medium/large,
    # spilling onto the right LCD if a line overflows one screen's width.
    add_executable(lcd_shinonome_demo
      tests/lcd_shinonome_demo.cpp
    )
    target_include_directories(lcd_shinonome_demo PRIVATE include)
    target_link_libraries(lcd_shinonome_demo PRIVATE mk2_usb mk2_display)

    # SEQTRAK SysEx Parameter Request/Reply round-trip probe. ALSA rawmidi is
    # Linux-only, same as hid_input_dump above.
    add_executable(seqtrak_param_probe
      tests/seqtrak_param_probe.cpp
    )
    target_include_directories(seqtrak_param_probe PRIVATE include)
    target_link_libraries(seqtrak_param_probe PRIVATE mk2_midi mk2_util)

    # Scans every top-level SEQTRAK parameter block for Parameter Request
    # read support, with a generous per-block timeout.
    add_executable(seqtrak_block_scan
      tests/seqtrak_block_scan.cpp
    )
    target_include_directories(seqtrak_block_scan PRIVATE include)
    target_link_libraries(seqtrak_block_scan PRIVATE mk2_midi mk2_util)

    # Tests whether selecting a factory preset (Bank Select + Program
    # Change) actually populates the Sound Common Name SysEx block, or
    # whether preset names only ever exist in companion editor apps.
    add_executable(seqtrak_name_probe
      tests/seqtrak_name_probe.cpp
    )
    target_include_directories(seqtrak_name_probe PRIVATE include)
    target_link_libraries(seqtrak_name_probe PRIVATE mk2_midi mk2_util)
  endif()
endif()
