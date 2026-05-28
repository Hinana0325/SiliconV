/*
 * SiliconV — HWComposer Shim (Display HAL)
 *
 * Bridges virtio-gpu to Android's HWC2 interface.
 *
 * SiliconV display pipeline:
 *   Android SurfaceFlinger
 *       ↓ (HWC2 HAL)
 *   SiliconV HWComposer Shim
 *       ↓ (virtio-gpu framebuffer)
 *   Hypervisor framebuffer
 *       ↓ (SDL / headless)
 *   Host display
 */

#ifndef SILICONV_HWC_HAL_H
#define SILICONV_HWC_HAL_H

#include <stdint.h>
#include <stdbool.h>

/* ── Display Configuration ─────────────────────── */
typedef struct {
    int width;
    int height;
    int dpi;
    int refresh_hz;
    int format;      /* HAL_PIXEL_FORMAT_* */
} sv_display_config_t;

/* ── Layer (composited by HWC) ─────────────────── */
typedef struct {
    int      id;
    int      type;      /* HWC2 composition type */
    int      format;
    int      width;
    int     height;
    int      x, y;      /* Position */
    float    alpha;
    int      transform;  /* Rotation/mirror */
    uint64_t buffer_id;  /* gralloc buffer handle */
    bool     visible;
} sv_hwc_layer_t;

/* Layer composition types */
#define HWC2_COMPOSITION_CLIENT     0   /* GPU composition */
#define HWC2_COMPOSITION_DEVICE     1   /* Hardware composition */
#define HWC2_COMPOSITION_SOLID_COLOR 2
#define HWC2_COMPOSITION_CURSOR     3
#define HWC2_COMPOSITION_SIDEBAND   4

/* ── HWC Display State ─────────────────────────── */
typedef struct {
    sv_display_config_t config;
    sv_hwc_layer_t layers[16];
    int num_layers;
    bool vsync_enabled;
    uint64_t last_vsync_ns;
} sv_hwc_state_t;

/* ── API ───────────────────────────────────────── */

/* Initialize the HWC shim */
int sv_hwc_init(sv_hwc_state_t *hwc, const sv_display_config_t *config);

/* Present a frame (commit all layers to virtio-gpu) */
int sv_hwc_present(sv_hwc_state_t *hwc);

/* Register a layer */
int sv_hwc_add_layer(sv_hwc_state_t *hwc, const sv_hwc_layer_t *layer);

/* Remove a layer */
int sv_hwc_remove_layer(sv_hwc_state_t *hwc, int layer_id);

/* Enable/disable vsync callbacks */
void sv_hwc_set_vsync(sv_hwc_state_t *hwc, bool enable);

/* Get vsync timestamp */
uint64_t sv_hwc_get_vsync(sv_hwc_state_t *hwc);

#endif /* SILICONV_HWC_HAL_H */
