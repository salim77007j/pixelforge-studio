//! Color adjustments: brightness/contrast, levels, curves, hue/saturation, invert, etc.
//! All operate on the ACTIVE raster layer, blended by the selection mask when present.

use crate::doc::{Document, LayerKind};
use crate::pixel::clamp_u8;
use rayon::prelude::*;
use std::sync::Arc;

/// Apply a per-pixel color transform closure (receives straight RGBA, returns new RGB).
/// Alpha is preserved. Selection blends result with original.
fn apply_rgb_transform<F: Fn([u8; 3]) -> [u8; 3] + Sync>(doc: &mut Document, f: F) -> Result<(), String> {
    let sel = doc.selection.clone();
    let layer = doc.active_layer_mut().ok_or("no active layer")?;
    if layer.kind != LayerKind::Raster { return Err("active layer is a group".into()); }
    if layer.locked { return Err("layer is locked".into()); }
    let px = layer.pixels.as_mut().ok_or("layer has no pixels")?;
    let buf = Arc::get_mut(px).ok_or_else(|| "layer buffer busy (finish stroke first)".to_string())?;
    let (lx, ly) = (layer.x, layer.y);
    let (w, h) = (buf.w as usize, buf.h as usize);
    let data = &mut buf.data;
    data.par_chunks_mut(w * 4).enumerate().for_each(|(y, row)| {
        for x in 0..w {
            let i = x * 4;
            let rgb = [row[i], row[i + 1], row[i + 2]];
            let out = f(rgb);
            let blend = match &sel {
                None => 1.0,
                Some(m) => {
                    let dx = x as i32 + lx;
                    let dy = y as i32 + ly;
                    if dx < 0 || dy < 0 || dx >= m.w as i32 || dy >= m.h as i32 { 0.0 }
                    else { m.data[(dy as usize) * (m.w as usize) + (dx as usize)] as f32 / 255.0 }
                }
            };
            if blend >= 1.0 {
                row[i] = out[0];
                row[i + 1] = out[1];
                row[i + 2] = out[2];
            } else if blend > 0.0 {
                row[i] = clamp_u8(rgb[0] as f32 * (1.0 - blend) + out[0] as f32 * blend);
                row[i + 1] = clamp_u8(rgb[1] as f32 * (1.0 - blend) + out[1] as f32 * blend);
                row[i + 2] = clamp_u8(rgb[2] as f32 * (1.0 - blend) + out[2] as f32 * blend);
            }
        }
    });
    let _ = h;
    Ok(())
}

/// For per-channel transforms (levels/curves channel selection).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Channel {
    Rgb,
    R,
    G,
    B,
}

pub fn brightness_contrast(doc: &mut Document, brightness: f32, contrast: f32) -> Result<(), String> {
    // brightness -100..100, contrast -100..100 (linear model)
    let b = brightness / 100.0;
    let c = contrast / 100.0;
    let lut = build_lut(|v| {
        let mut x = v as f32 / 255.0;
        x += b * 0.4;
        x = 0.5 + (x - 0.5) * (1.0 + c);
        x.clamp(0.0, 1.0) * 255.0
    });
    apply_lut(doc, &lut, &lut, &lut)
}

/// Build a 256-entry LUT from a closure.
pub fn build_lut<F: Fn(u8) -> f32>(f: F) -> [u8; 256] {
    let mut lut = [0u8; 256];
    for (i, v) in lut.iter_mut().enumerate() {
        *v = clamp_u8(f(i as u8));
    }
    lut
}

pub fn apply_lut(doc: &mut Document, lr: &[u8; 256], lg: &[u8; 256], lb: &[u8; 256]) -> Result<(), String> {
    let lr = *lr;
    let lg = *lg;
    let lb = *lb;
    apply_rgb_transform(doc, move |rgb| [lr[rgb[0] as usize], lg[rgb[1] as usize], lb[rgb[2] as usize]])
}

pub fn levels(
    doc: &mut Document,
    in_black: u8, in_white: u8, gamma: f32,
    out_black: u8, out_white: u8,
    channel: Channel,
) -> Result<(), String> {
    let ib = in_black as f32;
    let iw = in_white.max(in_black + 1) as f32;
    let ob = out_black as f32;
    let ow = out_white as f32;
    let g = gamma.max(0.01);
    let lut = build_lut(|v| {
        let t = ((v as f32 - ib) / (iw - ib)).clamp(0.0, 1.0);
        let t = t.powf(1.0 / g);
        t * (ow - ob) + ob
    });
    let n = [128u8; 256]; // neutral passthrough
    match channel {
        Channel::Rgb => apply_lut(doc, &lut, &lut, &lut),
        Channel::R => apply_lut(doc, &lut, &n, &n),
        Channel::G => apply_lut(doc, &n, &lut, &n),
        Channel::B => apply_lut(doc, &n, &n, &lut),
    }
}

/// Monotone-cubic (Fritsch-Carlson) spline through points -> 256 LUT.
pub fn curves_lut(points: &[(f32, f32)]) -> [u8; 256] {
    let mut pts: Vec<(f32, f32)> = points.to_vec();
    pts.retain(|p| p.0.is_finite() && p.1.is_finite());
    pts.push((0.0, pts.first().map(|p| p.1).unwrap_or(0.0)));
    pts.push((255.0, pts.last().map(|p| p.1).unwrap_or(255.0)));
    pts.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap());
    pts.dedup_by(|a, b| a.0 == b.0);
    let n = pts.len();
    if n < 2 {
        return build_lut(|v| v as f32);
    }
    if n == 2 {
        let (x0, y0) = pts[0];
        let (x1, y1) = pts[1];
        if x1 - x0 < 1e-6 { return build_lut(|_| y1); }
        return build_lut(move |v| {
            let t = ((v as f32 - x0) / (x1 - x0)).clamp(0.0, 1.0);
            y0 + (y1 - y0) * t
        });
    }
    // slopes
    let mut d = vec![0f32; n];
    for i in 0..n - 1 {
        d[i] = (pts[i + 1].1 - pts[i].1) / (pts[i + 1].0 - pts[i].0).max(1e-6);
    }
    d[n - 1] = d[n - 2];
    let mut m = vec![0f32; n];
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for i in 1..n - 1 {
        if d[i - 1] * d[i] <= 0.0 {
            m[i] = 0.0;
        } else {
            let w1 = 2.0 * (pts[i + 1].0 - pts[i].0) + (pts[i].0 - pts[i - 1].0);
            let w2 = (pts[i + 1].0 - pts[i].0) + 2.0 * (pts[i].0 - pts[i - 1].0);
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i]);
        }
    }
    build_lut(|v| {
        let x = v as f32;
        let mut i = 0usize;
        while i < n - 2 && x > pts[i + 1].0 { i += 1; }
        let (x0, y0) = pts[i];
        let (x1, y1) = pts[i + 1];
        let h = (x1 - x0).max(1e-6);
        let t = ((x - x0) / h).clamp(0.0, 1.0);
        let t2 = t * t;
        let t3 = t2 * t;
        let h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        let h10 = t3 - 2.0 * t2 + t;
        let h01 = -2.0 * t3 + 3.0 * t2;
        let h11 = t3 - t2;
        (h00 * y0 + h10 * h * m[i] + h01 * y1 + h11 * h * m[i + 1]).clamp(0.0, 255.0)
    })
}

pub fn apply_curves(doc: &mut Document, points: &[(f32, f32)], channel: Channel) -> Result<(), String> {
    let lut = curves_lut(points);
    let n = [128u8; 256];
    // neutral passthrough: identity
    let idn = build_lut(|v| v as f32);
    let _ = &n;
    match channel {
        Channel::Rgb => apply_lut(doc, &lut, &lut, &lut),
        Channel::R => apply_lut(doc, &lut, &idn, &idn),
        Channel::G => apply_lut(doc, &idn, &lut, &idn),
        Channel::B => apply_lut(doc, &idn, &idn, &lut),
    }
}

pub fn hue_saturation(doc: &mut Document, hue: f32, sat: f32, light: f32) -> Result<(), String> {
    let h_shift = hue; // -180..180
    let s_scale = 1.0 + sat / 100.0; // sat -100..100
    let l_scale = light / 100.0; // -100..100
    apply_rgb_transform(doc, move |rgb| {
        let c = [rgb[0] as f32 / 255.0, rgb[1] as f32 / 255.0, rgb[2] as f32 / 255.0];
        let mut hsl = crate::blend::rgb_to_hsl(c);
        hsl[0] = hsl[0] + h_shift;
        hsl[1] = (hsl[1] * s_scale).clamp(0.0, 1.0);
        hsl[2] = if l_scale >= 0.0 {
            hsl[2] + (1.0 - hsl[2]) * l_scale
        } else {
            hsl[2] * (1.0 + l_scale)
        };
        let out = crate::blend::hsl_to_rgb(hsl);
        [clamp_u8(out[0] * 255.0), clamp_u8(out[1] * 255.0), clamp_u8(out[2] * 255.0)]
    })
}

pub fn invert(doc: &mut Document) -> Result<(), String> {
    apply_rgb_transform(doc, |rgb| [255 - rgb[0], 255 - rgb[1], 255 - rgb[2]])
}

pub fn desaturate(doc: &mut Document) -> Result<(), String> {
    apply_rgb_transform(doc, |rgb| {
        let l = clamp_u8(0.299 * rgb[0] as f32 + 0.587 * rgb[1] as f32 + 0.114 * rgb[2] as f32);
        [l, l, l]
    })
}

pub fn threshold(doc: &mut Document, t: u8) -> Result<(), String> {
    apply_rgb_transform(doc, move |rgb| {
        let l = (0.299 * rgb[0] as f32 + 0.587 * rgb[1] as f32 + 0.114 * rgb[2] as f32) as u8;
        let v = if l >= t { 255 } else { 0 };
        [v, v, v]
    })
}

pub fn posterize(doc: &mut Document, levels: u8) -> Result<(), String> {
    let n = levels.clamp(2, 255) as f32;
    apply_rgb_transform(doc, move |rgb| {
        let q = |v: u8| ((v as f32 / 255.0 * n).floor() / (n - 1.0) * 255.0).clamp(0.0, 255.0) as u8;
        [q(rgb[0]), q(rgb[1]), q(rgb[2])]
    })
}

pub fn auto_contrast(doc: &mut Document) -> Result<(), String> {
    // compute luma histogram of active layer, find 0.5% percentiles
    let layer = doc.active_layer().ok_or("no active layer")?;
    let px = layer.pixels.as_ref().ok_or("no pixels")?;
    let mut hist = [0u64; 256];
    for p in px.data.chunks_exact(4) {
        let l = (0.299 * p[0] as f32 + 0.587 * p[1] as f32 + 0.114 * p[2] as f32) as usize;
        hist[l.min(255)] += 1;
    }
    let total = (px.w * px.h) as f64;
    let mut lo = 0u8;
    let mut hi = 255u8;
    let mut acc = 0f64;
    for (i, h) in hist.iter().enumerate() {
        acc += *h as f64;
        if acc >= total * 0.005 { lo = i as u8; break; }
    }
    acc = 0.0;
    for (i, h) in hist.iter().enumerate().rev() {
        acc += *h as f64;
        if acc >= total * 0.005 { hi = i as u8; break; }
    }
    if hi <= lo { return Ok(()); }
    let ib = lo as f32;
    let iw = hi as f32;
    let lut = build_lut(|v| (((v as f32 - ib) / (iw - ib)).clamp(0.0, 1.0)) * 255.0);
    apply_lut(doc, &lut, &lut, &lut)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn curves_identity() {
        let lut = curves_lut(&[(0.0, 0.0), (255.0, 255.0)]);
        for i in 0..256 {
            assert!((lut[i] as i32 - i as i32).abs() <= 1, "lut[{i}]={}", lut[i]);
        }
    }
    #[test]
    fn curves_mid() {
        let lut = curves_lut(&[(0.0, 0.0), (128.0, 190.0), (255.0, 255.0)]);
        assert!(lut[128] >= 185 && lut[128] <= 195, "{}", lut[128]);
    }
}
