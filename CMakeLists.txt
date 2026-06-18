cmake_minimum_required(VERSION 3.16)
project(SiliconV C ASM)

# Enable OBJC only on macOS (for Cocoa frontend)
if(APPLE)
    enable_language(OBJC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(SV_BUILD_TESTS "Build SiliconV unit tests" OFF)
option(SV_BUILD_QT "Build SiliconV Qt6 GUI (requires Qt6)" OFF)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DSV_DEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Default: no hypervisor backends until detected
set(SV_HAS_KVM FALSE)
set(SV_HAS_HVF FALSE)
set(SV_HAS_COCOA FALSE)
set(SV_HAS_TCG FALSE)

# ── Core Libraries ────────────────────────────────

add_library(sv_gic STATIC core/irq/gic.c)
target_include_directories(sv_gic PUBLIC core/irq)

add_library(sv_psci STATIC core/object/psci.c)
target_include_directories(sv_psci PUBLIC core/object)

add_library(sv_dtb STATIC core/memory/dtb.c)
target_include_directories(sv_dtb PUBLIC core/memory)

add_library(sv_bootimg STATIC core/vm/bootimg.c)
target_include_directories(sv_bootimg PUBLIC core/vm)

add_library(sv_machine STATIC core/vm/machine.c)
target_include_directories(sv_machine PUBLIC
    core/vm core/irq core/memory core/object
    devices/uart devices/transport devices/virtio-blk
    devices/virtio-net devices/virtio-console
    hypervisor/abstraction
)

# ── Device Libraries ──────────────────────────────

add_library(sv_uart STATIC devices/uart/pl011.c)
target_include_directories(sv_uart PUBLIC devices/uart)

add_library(sv_virtio STATIC devices/transport/virtio_mmio.c)
target_include_directories(sv_virtio PUBLIC devices/transport)

add_library(sv_virtio_blk STATIC devices/virtio-blk/virtio_blk.c)
target_include_directories(sv_virtio_blk PUBLIC
    devices/virtio-blk devices/transport
)

add_library(sv_virtio_net STATIC devices/virtio-net/virtio_net.c devices/virtio-net/tap_backend.c)
target_include_directories(sv_virtio_net PUBLIC
    devices/virtio-net devices/transport
)

add_library(sv_virtio_console STATIC devices/virtio-console/virtio_console.c)
target_include_directories(sv_virtio_console PUBLIC
    devices/virtio-console devices/transport
)

# ── Apple Device Libraries ────────────────────────

add_library(sv_apple_aic STATIC devices/apple-aic/apple_aic.c)
target_include_directories(sv_apple_aic PUBLIC devices/apple-aic)

add_library(sv_apple_uart STATIC devices/apple-uart/apple_uart.c)
target_include_directories(sv_apple_uart PUBLIC devices/apple-uart)

add_library(sv_apple_dart STATIC devices/apple-dart/apple_dart.c)
target_include_directories(sv_apple_dart PUBLIC devices/apple-dart)

add_library(sv_apple_sep STATIC devices/apple-sep/apple_sep.c)
target_include_directories(sv_apple_sep PUBLIC devices/apple-sep)

add_library(sv_apple_wdt STATIC devices/apple-wdt/apple_wdt.c)
target_include_directories(sv_apple_wdt PUBLIC devices/apple-wdt)

add_library(sv_apple_nvram STATIC devices/apple-nvram/apple_nvram.c)
target_include_directories(sv_apple_nvram PUBLIC devices/apple-nvram)

add_library(sv_apple_timer STATIC devices/apple-timer/apple_timer.c)
target_include_directories(sv_apple_timer PUBLIC devices/apple-timer)

add_library(sv_apple_gpio STATIC devices/apple-gpio/apple_gpio.c)
target_include_directories(sv_apple_gpio PUBLIC devices/apple-gpio)

add_library(sv_apple_i2c STATIC devices/apple-i2c/apple_i2c.c)
target_include_directories(sv_apple_i2c PUBLIC devices/apple-i2c)

add_library(sv_apple_spmi STATIC devices/apple-spmi/apple_spmi.c)
target_include_directories(sv_apple_spmi PUBLIC devices/apple-spmi)

# ── Apple Boot Libraries ─────────────────────────

add_library(sv_apple_img4 STATIC core/boot/apple/img4.c)
target_include_directories(sv_apple_img4 PUBLIC core/boot/apple)
target_link_libraries(sv_apple_img4 PUBLIC lzfse)

add_library(sv_apple_macho STATIC core/boot/apple/macho.c)
target_include_directories(sv_apple_macho PUBLIC core/boot/apple)

add_library(sv_apple_dtre STATIC core/boot/apple/dtre.c)
target_include_directories(sv_apple_dtre PUBLIC core/boot/apple)

add_library(sv_apple_bootargs STATIC core/boot/apple/bootargs.c)
target_include_directories(sv_apple_bootargs PUBLIC core/boot/apple)

add_library(sv_apple_xnuboot STATIC core/boot/apple/xnu_boot.c)
target_include_directories(sv_apple_xnuboot PUBLIC core/boot/apple)
target_link_libraries(sv_apple_xnuboot PUBLIC
    sv_apple_img4 sv_apple_macho sv_apple_dtre sv_apple_bootargs
)

add_library(sv_machine_apple STATIC core/vm/machine_apple.c)
target_include_directories(sv_machine_apple PUBLIC
    core/vm core/irq core/memory core/object
    devices/apple-aic devices/apple-uart devices/apple-dart
    devices/apple-sep devices/apple-wdt devices/apple-nvram
    devices/apple-timer devices/apple-gpio devices/apple-i2c devices/apple-spmi
    devices/transport devices/virtio-blk devices/virtio-net
    devices/virtio-console
    core/boot/apple
    hypervisor/abstraction
)
target_link_libraries(sv_machine_apple PUBLIC
    sv_apple_aic sv_apple_uart sv_apple_dart sv_apple_sep
    sv_apple_wdt sv_apple_nvram sv_apple_timer sv_apple_gpio
    sv_apple_i2c sv_apple_spmi sv_apple_xnuboot
    sv_psci sv_virtio sv_virtio_blk sv_virtio_net sv_virtio_console
)

# ── Hypervisor ────────────────────────────────────

add_library(sv_hv STATIC hypervisor/abstraction/hv.c)
target_include_directories(sv_hv PUBLIC hypervisor/abstraction)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(sv_hv_kvm STATIC hypervisor/kvm/kvm.c)
    target_link_libraries(sv_hv_kvm PUBLIC sv_hv)
    set(SV_HAS_KVM TRUE)
    message(STATUS "KVM backend enabled (ARM64 Linux)")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_library(sv_hv_hvf STATIC hypervisor/hvf/hvf.c)
    target_link_libraries(sv_hv_hvf PUBLIC sv_hv)
    set(SV_HAS_HVF TRUE)
    message(STATUS "HVF backend enabled (ARM64 macOS)")
else()
    message(STATUS "KVM/HVF backend skipped (host: ${CMAKE_SYSTEM_PROCESSOR})")
endif()

# TCG backend — pure software ARM64 emulation, always available
add_library(sv_hv_tcg STATIC
    hypervisor/tcg/tcg.c
    hypervisor/tcg/tcg_decode.c
    hypervisor/tcg/tcg_exec.c
    hypervisor/tcg/tcg_mmu.c
)
target_include_directories(sv_hv_tcg PUBLIC
    hypervisor/tcg
    hypervisor/abstraction
)
target_link_libraries(sv_hv_tcg PUBLIC sv_hv)
set(SV_HAS_TCG TRUE)
message(STATUS "TCG backend enabled (ARM64 software emulation)")

# ── CLI Tool ─────────────────────────────────────

add_executable(sv-cli frontend/cli/main.c)
target_link_libraries(sv-cli
    sv_machine sv_bootimg sv_gic sv_psci sv_dtb
    sv_uart sv_virtio sv_virtio_blk sv_virtio_net sv_virtio_console sv_hv
    sv_machine_apple
    sv_apple_aic sv_apple_uart sv_apple_dart sv_apple_sep
    sv_apple_wdt sv_apple_nvram sv_apple_timer sv_apple_gpio
    sv_apple_i2c sv_apple_spmi
    sv_apple_img4 sv_apple_macho sv_apple_dtre sv_apple_bootargs sv_apple_xnuboot
)
if(SV_HAS_KVM)
    target_link_libraries(sv-cli sv_hv_kvm)
    target_compile_definitions(sv-cli PRIVATE SV_HAS_KVM=1)
endif()
if(SV_HAS_HVF)
    target_link_libraries(sv-cli sv_hv_hvf)
    target_link_libraries(sv-cli "-framework Hypervisor")
    target_compile_definitions(sv-cli PRIVATE SV_HAS_HVF=1)
endif()
if(SV_HAS_TCG)
    # --whole-archive ensures TCG constructor is included even without direct references
    target_link_libraries(sv-cli
        -Wl,--whole-archive sv_hv_tcg -Wl,--no-whole-archive
    )
    target_compile_definitions(sv-cli PRIVATE SV_HAS_TCG=1)
endif()

# ── Tests ────────────────────────────────────────

if(SV_BUILD_TESTS)
    enable_testing()

    add_executable(test_dtb tests/unit/test_dtb.c)
    target_link_libraries(test_dtb sv_dtb)
    add_test(NAME test_dtb COMMAND test_dtb)
    set_tests_properties(test_dtb PROPERTIES LABELS unit)

    add_executable(test_uart tests/unit/test_uart.c)
    target_link_libraries(test_uart sv_uart sv_gic)
    add_test(NAME test_uart COMMAND test_uart)
    set_tests_properties(test_uart PROPERTIES LABELS unit)

    add_executable(test_virtio_mmio tests/unit/test_virtio_mmio.c)
    target_link_libraries(test_virtio_mmio sv_virtio sv_gic)
    add_test(NAME test_virtio_mmio COMMAND test_virtio_mmio)
    set_tests_properties(test_virtio_mmio PROPERTIES LABELS unit)

    add_executable(test_apple_aic tests/unit/test_apple_aic.c)
    target_link_libraries(test_apple_aic sv_apple_aic)
    add_test(NAME test_apple_aic COMMAND test_apple_aic)
    set_tests_properties(test_apple_aic PROPERTIES LABELS unit)

    add_executable(test_apple_xnuboot tests/unit/test_apple_xnuboot.c)
    target_link_libraries(test_apple_xnuboot sv_apple_img4 sv_apple_bootargs)
    add_test(NAME test_apple_xnuboot COMMAND test_apple_xnuboot)
    set_tests_properties(test_apple_xnuboot PROPERTIES LABELS unit)
endif()

# ── Boot Stub (ARM64 cross-compile) ───────────────

find_program(AARCH64_AS aarch64-linux-gnu-as)
find_program(AARCH64_LD aarch64-linux-gnu-ld)
find_program(AARCH64_OBJCOPY aarch64-linux-gnu-objcopy)

if(AARCH64_AS AND AARCH64_LD AND AARCH64_OBJCOPY)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/boot_stub.bin
        COMMAND ${AARCH64_AS} -o ${CMAKE_BINARY_DIR}/boot_stub.o ${CMAKE_SOURCE_DIR}/core/vm/boot_stub.S
        COMMAND ${AARCH64_LD} -T ${CMAKE_SOURCE_DIR}/core/vm/boot_stub.ld -o ${CMAKE_BINARY_DIR}/boot_stub.elf ${CMAKE_BINARY_DIR}/boot_stub.o
        COMMAND ${AARCH64_OBJCOPY} -O binary ${CMAKE_BINARY_DIR}/boot_stub.elf ${CMAKE_BINARY_DIR}/boot_stub.bin
        DEPENDS core/vm/boot_stub.S core/vm/boot_stub.ld
        COMMENT "Building ARM64 boot stub"
    )
    add_custom_target(boot_stub ALL DEPENDS ${CMAKE_BINARY_DIR}/boot_stub.bin)
    message(STATUS "Boot stub enabled (ARM64 cross-compiler found)")
else()
    message(STATUS "Boot stub skipped (install gcc-aarch64-linux-gnu)")
endif()

# ── macOS App Bundle ────────────────────────────

if(APPLE)
    set(SILICONV_SDK "macosx")
    set(SV_HAS_COCOA TRUE)

    add_executable(SiliconV
        MACOSX_BUNDLE
        frontend/cocoa/main.m
        frontend/cocoa/AppDelegate.h
        frontend/cocoa/AppDelegate.m
        frontend/cocoa/MainWindowController.h
        frontend/cocoa/MainWindowController.m
        frontend/cocoa/VMViewController.h
        frontend/cocoa/VMViewController.m
        frontend/cocoa/ConsoleViewController.h
        frontend/cocoa/ConsoleViewController.m
        frontend/cocoa/VMEngine.h
        frontend/cocoa/VMEngine.m
    )

    set_target_properties(SiliconV PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/frontend/cocoa/SiliconV-Info.plist"
        MACOSX_BUNDLE_ICON_FILE "SiliconV"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.siliconv.hypervisor"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        OUTPUT_NAME "SiliconV"
    )

    target_link_libraries(SiliconV
        sv_machine sv_bootimg sv_gic sv_psci sv_dtb
        sv_uart sv_virtio sv_virtio_blk sv_virtio_net sv_virtio_console sv_hv
        "-framework AppKit"
        "-framework Foundation"
    )

    # Copy icon into bundle
    set(SILICONV_ICON "${CMAKE_SOURCE_DIR}/frontend/cocoa/SiliconV.icns")
    if(EXISTS "${SILICONV_ICON}")
        add_custom_command(TARGET SiliconV POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SILICONV_ICON}"
                "$<TARGET_FILE_DIR:SiliconV>/../Resources/SiliconV.icns"
            COMMENT "Copying app icon to bundle"
        )
    endif()

    if(SV_HAS_HVF)
        target_link_libraries(SiliconV sv_hv_hvf "-framework Hypervisor")
        target_compile_definitions(SiliconV PRIVATE SV_HAS_HVF=1)
    endif()
    if(SV_HAS_TCG)
        target_link_libraries(SiliconV sv_hv_tcg)
        target_compile_definitions(SiliconV PRIVATE SV_HAS_TCG=1)
    endif()

    message(STATUS "  Cocoa: SiliconV.app bundle enabled")
endif()

# ── Cross-Platform Qt6 GUI (Linux + Windows) ──────

if(SV_BUILD_QT)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    add_executable(siliconv-qt
        frontend/qt/main.cpp
        frontend/qt/MainWindow.h
        frontend/qt/MainWindow.cpp
        frontend/qt/VMDisplayWidget.h
        frontend/qt/VMDisplayWidget.cpp
        frontend/qt/ConsoleWidget.h
        frontend/qt/ConsoleWidget.cpp
        frontend/qt/VMManager.h
        frontend/qt/VMManager.cpp
    )

    set_target_properties(siliconv-qt PROPERTIES
        AUTOMOC ON
        AUTORCC ON
        AUTOUIC ON
    )

    target_include_directories(siliconv-qt PRIVATE
        core/vm core/irq core/memory core/object
        devices/uart devices/transport devices/virtio-blk
        devices/virtio-net devices/virtio-console
        hypervisor/abstraction
    )

    target_link_libraries(siliconv-qt PRIVATE
        Qt6::Widgets
        sv_machine sv_bootimg sv_gic sv_psci sv_dtb
        sv_uart sv_virtio sv_virtio_blk sv_virtio_net sv_virtio_console sv_hv
    )

    # Win32: mark as GUI app (no console window)
    if(WIN32)
        set_target_properties(siliconv-qt PROPERTIES
            WIN32_EXECUTABLE TRUE
            OUTPUT_NAME "SiliconV"
        )
        message(STATUS "  Qt: Windows GUI mode (siliconv-qt.exe → SiliconV.exe)")
    endif()

    # KVM backend (Linux ARM64)
    if(SV_HAS_KVM)
        target_link_libraries(siliconv-qt PRIVATE sv_hv_kvm)
        target_compile_definitions(siliconv-qt PRIVATE SV_HAS_KVM=1)
    endif()

    # TCG backend (software fallback)
    if(SV_HAS_TCG)
        target_link_libraries(siliconv-qt PRIVATE sv_hv_tcg)
        target_compile_definitions(siliconv-qt PRIVATE SV_HAS_TCG=1)
    endif()

    # WHPX backend (Windows x64) — future, when hypervisor/whpx/ is implemented
    if(WIN32)
        message(STATUS "  Qt: WHPX backend not yet implemented (use --dry-run for demo)")
    endif()

    message(STATUS "  Qt: siliconv-qt target enabled (${CMAKE_SYSTEM_NAME})")
endif()

# ── Summary ───────────────────────────────────────

message(STATUS "")
message(STATUS "SiliconV: ${CMAKE_SYSTEM_PROCESSOR} / ${CMAKE_BUILD_TYPE}")
message(STATUS "  KVM: ${SV_HAS_KVM}")
message(STATUS "  HVF: ${SV_HAS_HVF}")
message(STATUS "  TCG: ${SV_HAS_TCG}")
message(STATUS "  Cocoa: ${SV_HAS_COCOA} (macOS)")
message(STATUS "  Qt: ${SV_BUILD_QT} (${CMAKE_SYSTEM_NAME})")
message(STATUS "")
