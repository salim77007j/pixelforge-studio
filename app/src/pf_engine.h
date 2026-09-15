/* PixelForge Rust Engine — C ABI (generated to mirror engine/src/lib.rs) */
#ifndef PF_ENGINE_H
#define PF_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *PFDoc;

int pf_version(void);
const char *pf_last_error(void);

/* ---------- document ---------- */
PFDoc pf_document_new(uint32_t w, uint32_t h, int32_t fill, uint8_t r, uint8_t g, uint8_t b);
PFDoc pf_document_open(const char *path);
void pf_document_close(PFDoc doc);
int32_t pf_document_size(PFDoc doc, uint32_t *w, uint32_t *h);
int32_t pf_document_modified(PFDoc doc);
int32_t pf_document_mark_saved(PFDoc doc);
const char *pf_document_path(PFDoc doc);
void pf_free_str(char *p);
int32_t pf_document_composite(PFDoc doc, int32_t x, int32_t y, uint32_t w, uint32_t h, uint8_t *out, size_t out_len);
uint64_t pf_document_memory(PFDoc doc);

/* ---------- layers ---------- */
const char *pf_layers_json(PFDoc doc);
uint64_t pf_layer_add(PFDoc doc, int32_t kind, const char *name);
uint64_t pf_layer_add_pixels(PFDoc doc, uint32_t w, uint32_t h, const uint8_t *data, size_t len, const char *name, int32_t x, int32_t y);
int32_t pf_layer_remove(PFDoc doc, uint64_t id);
uint64_t pf_layer_duplicate(PFDoc doc, uint64_t id);
int32_t pf_layer_set_name(PFDoc doc, uint64_t id, const char *name);
int32_t pf_layer_set_visible(PFDoc doc, uint64_t id, int32_t v);
int32_t pf_layer_set_locked(PFDoc doc, uint64_t id, int32_t v);
int32_t pf_layer_set_opacity(PFDoc doc, uint64_t id, float opacity);
int32_t pf_layer_set_blend(PFDoc doc, uint64_t id, const char *blend);
int32_t pf_layer_set_offset(PFDoc doc, uint64_t id, int32_t x, int32_t y);
int32_t pf_layer_offset(PFDoc doc, uint64_t id, int32_t *x, int32_t *y);
int32_t pf_layer_move(PFDoc doc, uint64_t id, uint64_t target_parent, int32_t index);
int32_t pf_layer_merge_down(PFDoc doc, uint64_t id);
int32_t pf_flatten(PFDoc doc);
int32_t pf_layer_set_active(PFDoc doc, uint64_t id);
uint64_t pf_layer_active(PFDoc doc);
int32_t pf_layer_pixels_get(PFDoc doc, uint64_t id, uint8_t *out, size_t out_len);
int32_t pf_layer_pixels_size(PFDoc doc, uint64_t id, uint32_t *w, uint32_t *h);
int32_t pf_layer_pixels_set(PFDoc doc, uint64_t id, const uint8_t *data, size_t len);
int32_t pf_layer_thumbnail(PFDoc doc, uint64_t id, uint32_t max_px, uint8_t *out, size_t out_len, uint32_t *out_w, uint32_t *out_h);
int32_t pf_layer_mask_add(PFDoc doc, uint64_t id, int32_t from_selection);
int32_t pf_layer_mask_remove(PFDoc doc, uint64_t id);
int32_t pf_layer_mask_set_enabled(PFDoc doc, uint64_t id, int32_t enabled);
int32_t pf_layer_mask_paint(PFDoc doc, uint64_t id, float x, float y, float radius, uint8_t value);
int32_t pf_layer_fill(PFDoc doc, uint64_t id, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* ---------- brush engine ---------- */
int32_t pf_stroke_begin(PFDoc doc, int32_t tool, float size, float hardness, float opacity, float flow, float spacing,
                        uint8_t r, uint8_t g, uint8_t b, uint8_t a, int32_t pressure_size, int32_t pressure_opacity,
                        float x, float y, float pressure);
int32_t pf_stroke_move(PFDoc doc, float x, float y, float pressure, int32_t *dx, int32_t *dy, uint32_t *dw, uint32_t *dh);
int32_t pf_stroke_end(PFDoc doc);
int32_t pf_stroke_cancel(PFDoc doc);

/* ---------- fills / shapes / gradients ---------- */
int32_t pf_flood_fill(PFDoc doc, int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t tolerance, int32_t contiguous);
int32_t pf_fill_selection(PFDoc doc, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
int32_t pf_gradient_fill(PFDoc doc, int32_t kind, double x0, double y0, double x1, double y1,
                         uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0, uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1, int32_t dither);
int32_t pf_shape_draw(PFDoc doc, int32_t kind, const double *pts, int32_t npts,
                      uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa, float stroke_w,
                      uint8_t fr, uint8_t fg, uint8_t fb, uint8_t fa);
int32_t pf_pick_color(PFDoc doc, int32_t x, int32_t y, int32_t sample_composite, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);
int32_t pf_histogram(PFDoc doc, uint64_t layer_id, uint32_t *out, size_t out_len);

/* ---------- selections ---------- */
int32_t pf_selection_rect(PFDoc doc, double x, double y, double w, double h, int32_t mode);
int32_t pf_selection_ellipse(PFDoc doc, double cx, double cy, double rx, double ry, int32_t mode);
int32_t pf_selection_lasso(PFDoc doc, const double *pts, int32_t npts, int32_t mode);
int32_t pf_selection_wand(PFDoc doc, int32_t x, int32_t y, uint8_t tolerance, int32_t contiguous, int32_t mode, int32_t sample_composite);
int32_t pf_selection_all(PFDoc doc);
int32_t pf_selection_none(PFDoc doc);
int32_t pf_selection_invert(PFDoc doc);
int32_t pf_selection_feather(PFDoc doc, float radius);
int32_t pf_selection_bounds(PFDoc doc, int32_t *x, int32_t *y, uint32_t *w, uint32_t *h);
const float *pf_selection_outline(PFDoc doc, int32_t *count);
int32_t pf_selection_stroke(PFDoc doc, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float width);
int32_t pf_selection_clear(PFDoc doc);

/* ---------- transforms ---------- */
int32_t pf_layer_translate(PFDoc doc, uint64_t id, int32_t dx, int32_t dy, int32_t commit);
int32_t pf_layer_affine(PFDoc doc, uint64_t id, const double *m, int32_t resample);
int32_t pf_layer_flip(PFDoc doc, uint64_t id, int32_t horizontal);
int32_t pf_image_rotate(PFDoc doc, int32_t deg);
int32_t pf_layer_perspective(PFDoc doc, uint64_t id, const double *corners, int32_t resample);
int32_t pf_image_resize(PFDoc doc, uint32_t w, uint32_t h, int32_t resample);
int32_t pf_canvas_resize(PFDoc doc, uint32_t w, uint32_t h, int32_t anchor);
int32_t pf_image_crop(PFDoc doc, int32_t x, int32_t y, uint32_t w, uint32_t h);
int32_t pf_image_trim(PFDoc doc);

/* ---------- adjustments ---------- */
int32_t pf_adjust_brightness_contrast(PFDoc doc, float brightness, float contrast);
int32_t pf_adjust_hue_saturation(PFDoc doc, float hue, float sat, float light);
int32_t pf_adjust_levels(PFDoc doc, uint8_t in_black, uint8_t in_white, float gamma, uint8_t out_black, uint8_t out_white, int32_t channel);
int32_t pf_adjust_curves(PFDoc doc, const float *points, int32_t n, int32_t channel);
int32_t pf_adjust_invert(PFDoc doc);
int32_t pf_adjust_desaturate(PFDoc doc);
int32_t pf_adjust_threshold(PFDoc doc, uint8_t t);
int32_t pf_adjust_posterize(PFDoc doc, uint8_t levels);
int32_t pf_adjust_auto_contrast(PFDoc doc);

/* ---------- filters ---------- */
int32_t pf_filter_blur(PFDoc doc, float radius, int32_t gaussian);
int32_t pf_filter_sharpen(PFDoc doc, float amount, float radius);
int32_t pf_filter_noise(PFDoc doc, uint8_t amount, int32_t mono);
int32_t pf_filter_pixelate(PFDoc doc, uint32_t size);
int32_t pf_filter_twirl(PFDoc doc, float angle, float radius);
int32_t pf_filter_wave(PFDoc doc, float amplitude, float wavelength);
int32_t pf_filter_edges(PFDoc doc, float strength);
int32_t pf_filter_emboss(PFDoc doc, float strength);
int32_t pf_filter_vignette(PFDoc doc, float amount);

/* ---------- history ---------- */
int32_t pf_history_undo(PFDoc doc);
int32_t pf_history_redo(PFDoc doc);
int32_t pf_history_count(PFDoc doc);
int32_t pf_history_index(PFDoc doc);
const char *pf_history_label(PFDoc doc, int32_t i);
int32_t pf_history_goto(PFDoc doc, int32_t i);
int32_t pf_history_clear(PFDoc doc);

/* ---------- preview (dialog live preview) ---------- */
int32_t pf_preview_begin(PFDoc doc);
int32_t pf_preview_commit(PFDoc doc, const char *label);
int32_t pf_preview_cancel(PFDoc doc);

/* ---------- io ---------- */
int32_t pf_export(PFDoc doc, const char *path, int32_t format, uint8_t quality, int32_t lossless,
                  uint8_t bg_r, uint8_t bg_g, uint8_t bg_b, uint32_t scale_pct);
int32_t pf_save_ora(PFDoc doc, const char *path);
uint64_t pf_import_image_as_layer(PFDoc doc, const char *path, const char *name);
const char *pf_rust_engine_info(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PF_ENGINE_H */
