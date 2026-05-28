cmake_minimum_required(VERSION 3.16)
project(SiliconV C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ──────────────────────────────────────────────
# Build Options
# ──────────────────────────────────────────────
option(SV_BUILD_TESTS "Build tests" OFF)

# ──────────────────────────────────────────────
# Compiler Flags
# ──────────────────────────────────────────────
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DSV_DEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ──────────────────────────────────────────────
# PL011 UART Emulation
# ──────────────────────────────────────────────
add_library(sv_uart STATIC
    devices/uart/pl011.c
)
target_include_directories(sv_uart PUBLIC
    ${CMAKE_SOURCE_DIR}/devices/uart
    ${CMAKE_SOURCE_DIR}/hypervisor/abstraction
)

# ──────────────────────────────────────────────
# Hypervisor Abstraction
# ──────────────────────────────────────────────
add_library(sv_hv STATIC
    hypervisor/abstraction/hv.c
)
target_include_directories(sv_hv PUBLIC
    ${CMAKE_SOURCE_DIR}/hypervisor/abstraction
)

# ──────────────────────────────────────────────
# KVM Backend (Linux ARM64 only)
# ──────────────────────────────────────────────
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(sv_hv_kvm STATIC
        hypervisor/kvm/kvm.c
    )
    target_link_libraries(sv_hv_kvm PUBLIC sv_hv)
    set(SV_HAS_KVM TRUE)
    message(STATUS "KVM backend enabled (ARM64 Linux)")
else()
    message(STATUS "KVM backend skipped (not ARM64 Linux, host is ${CMAKE_SYSTEM_PROCESSOR})")
endif()

# ──────────────────────────────────────────────
# SiliconV CLI Launcher
# ──────────────────────────────────────────────
add_executable(siliconv
    frontend/cli/main.c
)
target_link_libraries(siliconv
    sv_uart
    sv_hv
)
if(SV_HAS_KVM)
    target_link_libraries(siliconv sv_hv_kvm)
    target_compile_definitions(siliconv PRIVATE SV_HAS_KVM=1)
endif()

# ──────────────────────────────────────────────
# Boot Stub (ARM64 assembly — cross-compile)
# ──────────────────────────────────────────────
find_program(AARCH64_AS aarch64-linux-gnu-as)
find_program(AARCH64_LD aarch64-linux-gnu-ld)
find_program(AARCH64_OBJCOPY aarch64-linux-gnu-objcopy)

if(AARCH64_AS AND AARCH64_LD AND AARCH64_OBJCOPY)
    set(BOOT_STUB_SRC ${CMAKE_SOURCE_DIR}/core/vm/boot_stub.S)
    set(BOOT_STUB_LD  ${CMAKE_SOURCE_DIR}/core/vm/boot_stub.ld)
    set(BOOT_STUB_ELF ${CMAKE_BINARY_DIR}/boot_stub.elf)
    set(BOOT_STUB_BIN ${CMAKE_BINARY_DIR}/boot_stub.bin)

    add_custom_command(
        OUTPUT ${BOOT_STUB_BIN}
        COMMAND ${AARCH64_AS} -o ${BOOT_STUB_ELF}.o ${BOOT_STUB_SRC}
        COMMAND ${AARCH64_LD} -T ${BOOT_STUB_LD} -o ${BOOT_STUB_ELF} ${BOOT_STUB_ELF}.o
        COMMAND ${AARCH64_OBJCOPY} -O binary ${BOOT_STUB_ELF} ${BOOT_STUB_BIN}
        DEPENDS ${BOOT_STUB_SRC} ${BOOT_STUB_LD}
        COMMENT "Building ARM64 boot stub"
    )
    add_custom_target(boot_stub ALL DEPENDS ${BOOT_STUB_BIN})
    message(STATUS "Boot stub will be built (ARM64 cross-compiler found)")
else()
    message(STATUS "Boot stub skipped (aarch64 cross-compiler not found)")
    message(STATUS "  Install: apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu")
endif()

# ──────────────────────────────────────────────
# Tests
# ──────────────────────────────────────────────
if(SV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ──────────────────────────────────────────────
# Summary
# ──────────────────────────────────────────────
message(STATUS "")
message(STATUS "SiliconV Build Configuration:")
message(STATUS "  Host:    ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  Build:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  KVM:     ${SV_HAS_KVM}")
message(STATUS "")
