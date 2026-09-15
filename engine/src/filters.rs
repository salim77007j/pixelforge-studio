//! Filters: blur, sharpen, noise, pixelate, distortions, edge detect, emboss, vignette.
//! All operate on the ACTIVE raster layer, blended by the selection mask when present.

use crate::doc::{Document, LayerKind};
use crate::pixel::{clamp_u8, PixelBuffer};
use rayon::prelude::*;
use std::sync::Arc;

/// Run a full-buffer transform closure (in: src buffer, out: new buffer).
/// Result is blended with the original by the selection mask.
fn apply_filter<F>(doc: &mut Document, label: &str, f: F) -> Result<(), String>
where
    F: FnOnce(&PixelBuffer) -> Result<PixelBuffer, String>,
{
    let layer = doc.active_layer_mut().ok_or("no active layer")?;
    if layer.kind != LayerKind::Raster { return Err("active layer is a group".into()); }
    if layer.locked { return Err("layer is locked".into()); }
    let src = layer.pixels.as_ref().ok_or("no pixels")?.as_ref().clone();
    let (lx, ly) = (layer.x, layer.y);
    let sel = doc.selection.clone();
    let filtered = f(&src)?;
    let mut out = src.clone();
    // blend by selection
    match &sel {
        None => out = filtered,
        Some(m) => {
            for y in 0..out.h {
                for x in 0..out.w {
                    let dx = x as i32 + lx;
                    let dy = y as i32 + ly;
                    let v = if dx < 0 || dy < 0 || dx >= m.w as i32 || dy >= m.h as i32 { 0.0 }
                        else { m.data[(dy as usize) * (m.w as usize) + (dx as usize)] as f32 / 255.0 };
                    if v >= 1.0 {
                        let i = PixelBuffer::idx(x, y, out.w);
                        let j = i;
                        out.data[i..i + 4].copy_from_slice(&filtered.data[j..j + 4]);
                    } else if v > 0.0 {
                        let i = PixelBuffer::idx(x, y, out.w);
                        for c in 0..4 {
                            out.data[i + c] = clamp_u8(out.data[i + c] as f32 * (1.0 - v) + filtered.data[i + c] as f32 * v);
                        }
                    }
                }
            }
        }
    }
    let layer = doc.root.find_mut(doc.active).unwrap();
    layer.pixels = Some(Arc::new(out));
    Ok(())
}

/// Separable gaussian blur on premultiplied data. radius in px (sigma = radius/2).
pub fn gaussian_blur_buf(src: &PixelBuffer, radius: f32) -> PixelBuffer {
    let sigma = (radius / 2.0).max(0.05);
    let k = (radius.ceil() as usize * 2 + 1).max(3);
    let half = k / 2;
    let mut kernel = vec![0f32; k];
    let mut sum = 0f32;
    for (i, kv) in kernel.iter_mut().enumerate() {
        let d = i as f32 - half as f32;
        let v = (-d * d / (2.0 * sigma * sigma)).exp();
        *kv = v;
        sum += v;
    }
    for kv in kernel.iter_mut() { *kv /= sum; }
    let (w, h) = (src.w as usize, src.h as usize);
    // premultiply
    let mut pm = vec![0f32; w * h * 4];
    for (i, px) in src.data.chunks_exact(4).enumerate() {
        let a = px[3] as f32 / 255.0;
        pm[i * 4] = px[0] as f32 * a;
        pm[i * 4 + 1] = px[1] as f32 * a;
        pm[i * 4 + 2] = px[2] as f32 * a;
        pm[i * 4 + 3] = a;
    }
    // horizontal
    let mut tmp = vec![0f32; w * h * 4];
    tmp.par_chunks_mut(w * 4).enumerate().for_each(|(y, out_row)| {
        for x in 0..w {
            let mut acc = [0f32; 4];
            for (ki, kv) in kernel.iter().enumerate() {
                let sx = (x + ki).saturating_sub(half).min(w - 1);
                let i = (y * w + sx) * 4;
                acc[0] += pm[i] * kv;
                acc[1] += pm[i + 1] * kv;
                acc[2] += pm[i + 2] * kv;
                acc[3] += pm[i + 3] * kv;
            }
            let o = x * 4;
            out_row[o..o + 4].copy_from_slice(&acc);
        }
    });
    // vertical
    let mut out = src.clone();
    out.data.par_chunks_mut(w * 4).enumerate().for_each(|(y, out_row)| {
        for x in 0..w {
            let mut acc = [0f32; 4];
            for (ki, kv) in kernel.iter().enumerate() {
                let sy = (y + ki).saturating_sub(half).min(h - 1);
                let i = (sy * w + x) * 4;
                acc[0] += tmp[i] * kv;
                acc[1] += tmp[i + 1] * kv;
                acc[2] += tmp[i + 2] * kv;
                acc[3] += tmp[i + 3] * kv;
            }
            let o = x * 4;
            let a = acc[3];
            if a > 1e-6 {
                out_row[o] = clamp_u8(acc[0] / a);
                out_row[o + 1] = clamp_u8(acc[1] / a);
                out_row[o + 2] = clamp_u8(acc[2] / a);
            }
            out_row[o + 3] = clamp_u8(a * 255.0);
        }
    });
    out
}

pub fn box_blur_buf(src: &PixelBuffer, radius: u32) -> PixelBuffer {
    let r = radius.max(1) as usize;
    let (w, h) = (src.w as usize, src.h as usize);
    let mut pm = vec![0f32; w * h * 4];
    for (i, px) in src.data.chunks_exact(4).enumerate() {
        let a = px[3] as f32 / 255.0;
        pm[i * 4] = px[0] as f32 * a;
        pm[i * 4 + 1] = px[1] as f32 * a;
        pm[i * 4 + 2] = px[2] as f32 * a;
        pm[i * 4 + 3] = a;
    }
    let mut tmp = vec![0f32; w * h * 4];
    tmp.par_chunks_mut(w * 4).enumerate().for_each(|(y, out_row)| {
        for x in 0..w {
            let x0 = x.saturating_sub(r);
            let x1 = (x + r + 1).min(w);
            let n = (x1 - x0) as f32;
            let mut acc = [0f32; 4];
            for xx in x0..x1 {
                let i = (y * w + xx) * 4;
                acc[0] += pm[i]; acc[1] += pm[i + 1]; acc[2] += pm[i + 2]; acc[3] += pm[i + 3];
            }
            let o = x * 4;
            for c in 0..4 { out_row[o + c] = acc[c] / n; }
        }
    });
    let mut out = src.clone();
    out.data.par_chunks_mut(w * 4).enumerate().for_each(|(y, out_row)| {
        for x in 0..w {
            let y0 = y.saturating_sub(r);
            let y1 = (y + r + 1).min(h);
            let n = (y1 - y0) as f32;
            let mut acc = [0f32; 4];
            for yy in y0..y1 {
                let i = (yy * w + x) * 4;
                acc[0] += tmp[i]; acc[1] += tmp[i + 1]; acc[2] += tmp[i + 2]; acc[3] += tmp[i + 3];
            }
            let o = x * 4;
            let a = acc[3] / n;
            if a > 1e-6 {
                out_row[o] = clamp_u8(acc[0] / n / a);
                out_row[o + 1] = clamp_u8(acc[1] / n / a);
                out_row[o + 2] = clamp_u8(acc[2] / n / a);
            }
            out_row[o + 3] = clamp_u8(a * 255.0);
        }
    });
    out
}

pub fn blur(doc: &mut Document, radius: f32, gaussian: bool) -> Result<(), String> {
    apply_filter(doc, "Blur", |src| {
        Ok(if gaussian {
            gaussian_blur_buf(src, radius)
        } else {
            box_blur_buf(src, radius as u32)
        })
    })
}

pub fn sharpen(doc: &mut Document, amount: f32, radius: f32) -> Result<(), String> {
    let amount = amount.max(0.0);
    apply_filter(doc, "Sharpen", |src| {
        let blurred = gaussian_blur_buf(src, radius.max(0.5));
        let mut out = src.clone();
        for (o, (s, b)) in out.data.chunks_exact_mut(4).zip(src.data.chunks_exact(4).zip(blurred.data.chunks_exact(4))) {
            for c in 0..3 {
                let v = s[c] as f32 + amount * (s[c] as f32 - b[c] as f32);
                o[c] = clamp_u8(v);
            }
        }
        Ok(out)
    })
}

pub fn noise(doc: &mut Document, amount: u8, mono: bool) -> Result<(), String> {
    apply_filter(doc, "Noise", |src| {
        let mut out = src.clone();
        let mut state: u64 = 0x853C49E6748FEA9B;
        let mut next = || {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            state
        };
        for px in out.data.chunks_exact_mut(4) {
            if mono {
                let n = ((next() % (2 * amount as u64 + 1)) as i64 - amount as i64) as f32;
                for c in 0..3 {
                    px[c] = clamp_u8(px[c] as f32 + n);
                }
            } else {
                for c in 0..3 {
                    let n = ((next() % (2 * amount as u64 + 1)) as i64 - amount as i64) as f32;
                    px[c] = clamp_u8(px[c] as f32 + n);
                }
            }
        }
        Ok(out)
    })
}

pub fn pixelate(doc: &mut Document, size: u32) -> Result<(), String> {
    let size = size.max(2);
    apply_filter(doc, "Pixelate", move |src| {
        let mut out = src.clone();
        let (w, h) = (src.w as usize, src.h as usize);
        let s = size as usize;
        let mut by = 0;
        while by < h {
            let mut bx = 0;
            while bx < w {
                let x1 = (bx + s).min(w);
                let y1 = (by + s).min(h);
                let mut acc = [0f64; 4];
                let mut n = 0f64;
                for y in by..y1 {
                    for x in bx..x1 {
                        let i = (y * w + x) * 4;
                        let a = src.data[i + 3] as f64 / 255.0;
                        acc[0] += src.data[i] as f64 * a;
                        acc[1] += src.data[i + 1] as f64 * a;
                        acc[2] += src.data[i + 2] as f64 * a;
                        acc[3] += a;
                        n += 1.0;
                    }
                }
                let avg = if acc[3] > 0.0 {
                    [(acc[0] / acc[3]).round() as u8, (acc[1] / acc[3]).round() as u8, (acc[2] / acc[3]).round() as u8, (acc[3] / n * 255.0).round() as u8]
                } else {
                    [0, 0, 0, 0]
                };
                for y in by..y1 {
                    for x in bx..x1 {
                        let i = (y * w + x) * 4;
                        out.data[i..i + 4].copy_from_slice(&avg);
                    }
                }
                bx += s;
            }
            by += s;
        }
        Ok(out)
    })
}

/// Distortion helper: inverse-map each pixel through a closure (doc coords centered).
fn distort<F>(doc: &mut Document, label: &str, f: F) -> Result<(), String>
where
    F: Fn(f64, f64) -> (f64, f64) + Sync,
{
    apply_filter(doc, label, move |src| {
        let (w, h) = (src.w, src.h);
        let mut out = src.clone();
        out.data.par_chunks_mut(w as usize * 4).enumerate().for_each(|(y, out_row)| {
            for x in 0..w {
                let (sx, sy) = f(x as f64, y as f64);
                let c = crate::geom::sample(src, sx, sy, crate::geom::Resample::Bilinear);
                out_row[x as usize * 4..x as usize * 4 + 4].copy_from_slice(&c);
            }
        });
        Ok(out)
    })
}

pub fn twirl(doc: &mut Document, angle_deg: f32, radius: f32) -> Result<(), String> {
    let (w, h) = (doc.w as f64, doc.h as f64);
    let cx = w / 2.0;
    let cy = h / 2.0;
    let r = radius.min((w.min(h) / 2.0) as f32) as f64;
    let ang = angle_deg.to_radians() as f64;
    distort(doc, "Twirl", move |x, y| {
        let dx = x - cx;
        let dy = y - cy;
        let d = (dx * dx + dy * dy).sqrt();
        if d >= r || r <= 0.0 { return (x, y); }
        let t = 1.0 - d / r;
        let t = t * t;
        let theta = ang * t;
        let cos = theta.cos();
        let sin = theta.sin();
        let nx = dx * cos - dy * sin + cx;
        let ny = dx * sin + dy * cos + cy;
        (nx, ny)
    })
}

pub fn wave(doc: &mut Document, amplitude: f32, wavelength: f32) -> Result<(), String> {
    let amp = amplitude as f64;
    let wl = wavelength.max(2.0) as f64;
    distort(doc, "Wave", move |x, y| {
        let dx = amp * (y as f64 * std::f64::consts::TAU / wl).sin();
        let dy = amp * (x as f64 * std::f64::consts::TAU / wl).sin();
        (x - dx, y - dy)
    })
}

pub fn edge_detect(doc: &mut Document, strength: f32) -> Result<(), String> {
    let s = strength.max(0.01);
    apply_filter(doc, "Edge Detect", move |src| {
        let (w, h) = (src.w as usize, src.h as usize);
        let mut out = src.clone();
        out.data.par_chunks_mut(w * 4).enumerate().for_each(|(y, out_row)| {
            if y == 0 || y + 1 >= h { return; }
            for x in 1..w.saturating_sub(1) {
                let luma = |xx: usize, yy: usize| {
                    let i = (yy * w + xx) * 4;
                    0.299 * src.data[i] as f32 + 0.587 * src.data[i + 1] as f32 + 0.114 * src.data[i + 2] as f32
                };
                let gx = luma(x + 1, y - 1) + 2.0 * luma(x + 1, y) + luma(x + 1, y + 1)
                    - luma(x - 1, y - 1) - 2.0 * luma(x - 1, y) - luma(x - 1, y + 1);
                let gy = luma(x - 1, y + 1) + 2.0 * luma(x, y + 1) + luma(x + 1, y + 1)
                    - luma(x - 1, y - 1) - 2.0 * luma(x, y - 1) - luma(x + 1, y - 1);
                let mag = ((gx * gx + gy * gy).sqrt() * s).min(255.0);
                let o = x * 4;
                out_row[o] = mag as u8;
                out_row[o + 1] = mag as u8;
                out_row[o + 2] = mag as u8;
            }
        });
        Ok(out)
    })
}

pub fn emboss(doc: &mut Document, strength: f32) -> Result<(), String> {
    let s = strength.max(0.01);
    apply_filter(doc, "Emboss", move |src| {
        let (w, h) = (src.w as usize, src.h as usize);
        let mut out = src.clone();
        out.data.par_chunks_mut(w * 4).enumerate().for_each(|(y, out_row)| {
            if y == 0 || y + 1 >= h { return; }
            for x in 1..w.saturating_sub(1) {
                let luma = |xx: usize, yy: usize| {
                    let i = (yy * w + xx) * 4;
                    0.299 * src.data[i] as f32 + 0.587 * src.data[i + 1] as f32 + 0.114 * src.data[i + 2] as f32
                };
                let v = 128.0 + s * (luma(x - 1, y - 1) - luma(x + 1, y + 1));
                let v = v.clamp(0.0, 255.0) as u8;
                let o = x * 4;
                out_row[o] = v;
                out_row[o + 1] = v;
                out_row[o + 2] = v;
            }
        });
        Ok(out)
    })
}

pub fn vignette(doc: &mut Document, amount: f32) -> Result<(), String> {
    let a = amount.clamp(0.0, 1.0) as f64;
    apply_filter(doc, "Vignette", move |src| {
        let (w, h) = (src.w as f64, src.h as f64);
        let cx = w / 2.0;
        let cy = h / 2.0;
        let rmax = (w * w + h * h).sqrt() / 2.0;
        let mut out = src.clone();
        for y in 0..src.h {
            for x in 0..src.w {
                let dx = x as f64 - cx;
                let dy = y as f64 - cy;
                let r = (dx * dx + dy * dy).sqrt() / rmax;
                let f = (1.0 - a * r * r).max(0.0);
                let i = PixelBuffer::idx(x, y, src.w);
                for c in 0..3 {
                    out.data[i + c] = clamp_u8(out.data[i + c] as f32 * f as f32);
                }
            }
        }
        Ok(out)
    })
}
