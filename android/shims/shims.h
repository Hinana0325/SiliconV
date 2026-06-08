/*
 * SiliconV — Android HAL Shim Layer
 *
 * Provides HAL stubs for Android frameworks that expect hardware
 * that SiliconV doesn't physically emulate.
 *
 * Shimmed HALs:
 *   - hwcomposer (display) → virtio-gpu
 *   - gralloc (graphics memory) → DMA-buf heaps
 *   - audio (stubs only)
 *   - sensors (stubs only)
 *   - camera (stubs only)
 *   - power (stubs only)
 */

#ifndef SILICONV_SHIMS_H
#define SILICONV_SHIMS_H

#include <stdint.h>
#include <stdbool.h>

/* ── HAL Module IDs ────────────────────────────── */

/* Display */
#define SV_HAL_HWC2       "hwcomposer"
#define SV_HAL_GRALLOC    "gralloc"

/* Graphics */
#define SV_HAL_GPU        "gpu"

/* Input */
#define SV_HAL_INPUT      "input"

/* Audio (stubbed) */
#define SV_HAL_AUDIO       "audio"

/* Sensors (stubbed) */
#define SV_HAL_SENSORS     "sensors"

/* Camera (stubbed) */
#define SV_HAL_CAMERA      "camera"

/* Power */
#define SV_HAL_POWER       "power"

/* ── Shim Configuration ────────────────────────── */

typedef struct {
    /* Display */
    int  display_width;
    int  display_height;
    int  display_dpi;
    int  display_refresh_hz;

    /* GPU */
    bool gpu_3d_enabled;    /* false = 2D framebuffer only */
    bool vulkan_enabled;

    /* Audio */
    bool audio_stubbed;     /* true = silent, no real audio */

    /* Sensors */
    bool sensors_stubbed;   /* true = no sensor data */

    /* Camera */
    bool camera_stubbed;    /* true = no camera */

    /* Power */
    bool power_stubbed;     /* true = always-on */
} sv_shim_config_t;

static inline sv_shim_config_t sv_shim_config_default(void)
{
    sv_shim_config_t cfg = {
        .display_width = 1080,
        .display_height = 2400,
        .display_dpi = 420,
        .display_refresh_hz = 60,
        .gpu_3d_enabled = false,  /* v0: 2D only */
        .vulkan_enabled = false,
        .audio_stubbed = true,
        .sensors_stubbed = true,
        .camera_stubbed = true,
        .power_stubbed = true,
    };
    return cfg;
}

/* ── Shim Registration ─────────────────────────── */

/*
 * In the Android vendor image, each shim is a shared library:
 *   hw/siliconv/hwcomposer/siliconv_hwcomposer.so
 *   hw/siliconv/gralloc/siliconv_gralloc.so
 *   hw/siliconv/audio/siliconv_audio.so  (stub)
 *   hw/siliconv/sensors/siliconv_sensors.so  (stub)
 *
 * These are loaded by Android's HIDL/AIDL HAL mechanism.
 */

/* ── Build-time Shim Config ────────────────────── */

/*
 * Android.mk / Android.bp snippet for SiliconV:
 *
 * cc_binary {
 *     name: "android.hardware.graphics.composer@2.4-service.siliconv",
 *     init_rc: ["hardware/siliconv/hwcomposer/service.rc"],
 *     vendor: true,
 *     relative_install_path: "hw",
 *     srcs: ["service.cpp", "HwcHal.cpp"],
 *     shared_libs: [
 *         "libhardware",
 *         "libhidlbase",
 *         "android.hardware.graphics.composer@2.4",
 *     ],
 * }
 */

#endif /* SILICONV_SHIMS_H */
