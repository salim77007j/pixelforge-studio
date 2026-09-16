//! Selection tools: rectangular/elliptical/lasso/wand masks, feather, invert, marching-ants outline.

use crate::doc::{Document, Rect};
use crate::pixel::MaskBuffer;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SelMode {
    Replace,
    Add,
    Subtract,
    Intersect,
}

fn new_mask(doc: &Document, mode: SelMode) -> MaskBuffer {
    match (&doc.selection, mode) {
        (Some(prev), SelMode::Add) => prev.as_ref().clone(),
        (Some(prev), SelMode::Subtract) => prev.as_ref().clone(),
        (Some(prev), SelMode::Intersect) => prev.as_ref().clone(),
        _ => MaskBuffer::new(doc.w, doc.h, 0).expect("mask"),
    }
}

fn combine(mask: &mut MaskBuffer, mode: SelMode, x: u32, y: u32, v: u8) {
    let i = (y as usize) * (mask.w as usize) + (x as usize);
    mask.data[i] = match mode {
        SelMode::Replace => v,
        SelMode::Add => mask.data[i].max(v),
        SelMode::Subtract => mask.data[i].saturating_sub(v),
        SelMode::Intersect => mask.data[i].min(v),
    };
}

pub fn select_rect(doc: &mut Document, x: f64, y: f64, w: f64, h: f64, mode: SelMode) {
    let mut mask = new_mask(doc, mode);
    let (mw, mh) = (mask.w, mask.h);
    // 2x2 supersample for AA
    let x0 = x.floor().max(0.0) as i32;
    let y0 = y.floor().max(0.0) as i32;
    let x1 = (x + w).ceil() as i32;
    let y1 = (y + h).ceil() as i32;
    for py in y0..y1 {
        for px in x0..x1 {
            if px < 0 || py < 0 { continue; }
            let (ux, uy) = (px as u32, py as u32);
            if ux >= mw || uy >= mh { continue; }
            let mut cov = 0u32;
            for sy in 0..2 {
                for sx in 0..2 {
                    let fx = px as f64 + (sx as f64 + 0.5) / 2.0;
                    let fy = py as f64 + (sy as f64 + 0.5) / 2.0;
                    if fx >= x && fx < x + w && fy >= y && fy < y + h { cov += 1; }
                }
            }
            combine(&mut mask, mode, ux, uy, (cov * 255 / 4) as u8);
        }
    }
    doc.selection = Some(std::sync::Arc::new(mask));
    doc.cached_outline = None;
}

pub fn select_ellipse(doc: &mut Document, cx: f64, cy: f64, rx: f64, ry: f64, mode: SelMode) {
    let mut mask = new_mask(doc, mode);
    let (mw, mh) = (mask.w, mask.h);
    let x0 = (cx - rx - 1.0).floor().max(0.0) as i32;
    let y0 = (cy - ry - 1.0).floor().max(0.0) as i32;
    let x1 = (cx + rx + 1.0).ceil() as i32;
    let y1 = (cy + ry + 1.0).ceil() as i32;
    for py in y0..y1 {
        for px in x0..x1 {
            if px < 0 || py < 0 { continue; }
            let (ux, uy) = (px as u32, py as u32);
            if ux >= mw || uy >= mh { continue; }
            let mut cov = 0u32;
            for sy in 0..2 {
                for sx in 0..2 {
                    let fx = (px as f64 + (sx as f64 + 0.5) / 2.0 - cx) / rx.max(1e-6);
                    let fy = (py as f64 + (sy as f64 + 0.5) / 2.0 - cy) / ry.max(1e-6);
                    if fx * fx + fy * fy <= 1.0 { cov += 1; }
                }
            }
            combine(&mut mask, mode, ux, uy, (cov * 255 / 4) as u8);
        }
    }
    doc.selection = Some(std::sync::Arc::new(mask));
    doc.cached_outline = None;
}

/// Polygon (lasso) — even-odd point-in-polygon, 2x2 supersampled.
pub fn select_lasso(doc: &mut Document, pts: &[f64], mode: SelMode) {
    if pts.len() < 6 { return; }
    let mut mask = new_mask(doc, mode);
    let (mw, mh) = (mask.w, mask.h);
    let poly: Vec<(f64, f64)> = pts.chunks_exact(2).map(|c| (c[0], c[1])).collect();
    let n = poly.len();
    let inside = |fx: f64, fy: f64| -> bool {
        let mut inside = false;
        let mut j = n - 1;
        for i in 0..n {
            let (xi, yi) = poly[i];
            let (xj, yj) = poly[j];
            if (yi > fy) != (yj > fy) && fx < (xj - xi) * (fy - yi) / (yj - yi) + xi {
                inside = !inside;
            }
            j = i;
        }
        inside
    };
    let minx = poly.iter().map(|p| p.0).fold(f64::MAX, f64::min).floor().max(0.0) as i32;
    let maxx = poly.iter().map(|p| p.0).fold(f64::MIN, f64::max).ceil().min(mw as f64) as i32;
    let miny = poly.iter().map(|p| p.1).fold(f64::MAX, f64::min).floor().max(0.0) as i32;
    let maxy = poly.iter().map(|p| p.1).fold(f64::MIN, f64::max).ceil().min(mh as f64) as i32;
    for py in miny..=maxy {
        for px in minx..=maxx {
            if px < 0 || py < 0 { continue; }
            let (ux, uy) = (px as u32, py as u32);
            if ux >= mw || uy >= mh { continue; }
            let mut cov = 0u32;
            for sy in 0..2 {
                for sx in 0..2 {
                    let fx = px as f64 + (sx as f64 + 0.5) / 2.0;
                    let fy = py as f64 + (sy as f64 + 0.5) / 2.0;
                    if inside(fx, fy) { cov += 1; }
                }
            }
            if cov > 0 {
                combine(&mut mask, mode, ux, uy, (cov * 255 / 4) as u8);
            }
        }
    }
    doc.selection = Some(std::sync::Arc::new(mask));
    doc.cached_outline = None;
}

/// Magic wand: flood select on composite or active layer by tolerance.
pub fn select_wand(doc: &Document, x: i32, y: i32, tolerance: u8, contiguous: bool, mode: SelMode, sample_composite: bool) -> MaskBuffer {
    let (w, h) = (doc.w as usize, doc.h as usize);
    if x < 0 || y < 0 || x as usize >= w || y as usize >= h {
        return MaskBuffer::new(doc.w, doc.h, 0).unwrap();
    }
    // sample source
    let comp;
    let data: &[u8] = if sample_composite {
        comp = {
            let mut b = vec![0u8; w * h * 4];
            doc.composite(Rect { x: 0, y: 0, w: doc.w, h: doc.h }, &mut b);
            b
        };
        &comp
    } else {
        // build doc-size temp from active layer honoring its offset
        let mut b = vec![0u8; w * h * 4];
        if let Some(layer) = doc.active_layer() {
            if let Some(p) = &layer.pixels {
                let lx0 = (layer.x.max(0) as usize).min(w);
                let ly0 = (layer.y.max(0) as usize).min(h);
                let copy_w = (p.w as usize).min(w - lx0);
                let copy_h = (p.h as usize).min(h - ly0);
                for yy in 0..copy_h {
                    for xx in 0..copy_w {
                        let si = (yy * (p.w as usize) + xx) * 4;
                        let di = ((ly0 + yy) * w + lx0 + xx) * 4;
                        b[di..di + 4].copy_from_slice(&p.data[si..si + 4]);
                    }
                }
            }
        }
        comp = b;
        &comp
    };

    let idx = (y as usize * w + x as usize) * 4;
    let target = [data[idx], data[idx + 1], data[idx + 2], data[idx + 3]];
    let tol = tolerance as i32;

    let matches = |i: usize| {
        let d = (data[i] as i32 - target[0] as i32).abs()
            .max((data[i + 1] as i32 - target[1] as i32).abs())
            .max((data[i + 2] as i32 - target[2] as i32).abs())
            .max((data[i + 3] as i32 - target[3] as i32).abs());
        d <= tol
    };

    let mut result = MaskBuffer::new(doc.w, doc.h, 0).unwrap();
    if contiguous {
        // scanline flood fill with stack of (y, x_left, x_right)
        let mut stack: Vec<(i32, i32, i32)> = Vec::new();
        // seed span
        let mut visited = vec![false; w * h];
        let mut seed_l = x;
        let mut seed_r = x;
        while seed_l > 0 && matches(((y as usize) * w + (seed_l - 1) as usize) * 4) { seed_l -= 1; }
        while (seed_r as usize) < w - 1 && matches(((y as usize) * w + (seed_r + 1) as usize) * 4) { seed_r += 1; }
        stack.push((y, seed_l, seed_r));
        for xx in seed_l..=seed_r {
            visited[y as usize * w + xx as usize] = true;
        }
        while let Some((yy, xl, xr)) = stack.pop() {
            for xx in xl..=xr {
                result.set(xx as u32, yy as u32, 255);
            }
            for ny in [yy - 1, yy + 1] {
                if ny < 0 || ny as usize >= h { continue; }
                let mut cx = xl;
                while cx <= xr {
                    let m = !visited[ny as usize * w + cx as usize] && matches((ny as usize * w + cx as usize) * 4);
                    if m {
                        let mut nl = cx;
                        while nl > 0 && !visited[ny as usize * w + (nl - 1) as usize] && matches((ny as usize * w + (nl - 1) as usize) * 4) { nl -= 1; }
                        let mut nr = cx;
                        while (nr as usize) < w - 1 && !visited[ny as usize * w + (nr + 1) as usize] && matches((ny as usize * w + (nr + 1) as usize) * 4) { nr += 1; }
                        for vx in nl..=nr { visited[ny as usize * w + vx as usize] = true; }
                        stack.push((ny, nl, nr));
                        cx = nr + 1;
                    } else {
                        cx += 1;
                    }
                }
            }
        }
    } else {
        for i in 0..w * h {
            if matches(i * 4) {
                result.data[i] = 255;
            }
        }
    }

    // combine with mode
    let mut mask = new_mask(doc, mode);
    for i in 0..w * h {
        let v = result.data[i];
        if v > 0 {
            mask.data[i] = match mode {
                SelMode::Replace => v,
                SelMode::Add => mask.data[i].max(v),
                SelMode::Subtract => mask.data[i].saturating_sub(v),
                SelMode::Intersect => mask.data[i].min(v),
            };
        }
    }
    mask
}

pub fn feather(doc: &mut Document, radius: f32) {
    if let Some(m) = doc.selection.clone() {
        let blurred = gaussian_blur_mask(&m, radius.max(0.1));
        doc.selection = Some(std::sync::Arc::new(blurred));
        doc.cached_outline = None;
    }
}

pub fn gaussian_blur_mask(m: &MaskBuffer, radius: f32) -> MaskBuffer {
    let sigma = (radius / 2.0).max(0.05);
    let k = (radius.ceil() as usize * 2 + 1).max(3);
    let mut kernel = vec![0f32; k];
    let c = k as f32 / 2.0;
    let mut sum = 0f32;
    for (i, kv) in kernel.iter_mut().enumerate() {
        let d = i as f32 - (c - 0.5);
        let v = (-d * d / (2.0 * sigma * sigma)).exp();
        *kv = v;
        sum += v;
    }
    for kv in kernel.iter_mut() { *kv /= sum; }
    let (w, h) = (m.w as usize, m.h as usize);
    let half = k / 2;
    let mut tmp = vec![0f32; w * h];
    let src: Vec<f32> = m.data.iter().map(|&v| v as f32).collect();
    for y in 0..h {
        for x in 0..w {
            let mut acc = 0f32;
            for (ki, kv) in kernel.iter().enumerate() {
                let sx = (x + ki).saturating_sub(half).min(w - 1);
                acc += src[y * w + sx] * kv;
            }
            tmp[y * w + x] = acc;
        }
    }
    let mut out = vec![0u8; w * h];
    for y in 0..h {
        for x in 0..w {
            let mut acc = 0f32;
            for (ki, kv) in kernel.iter().enumerate() {
                let sy = (y + ki).saturating_sub(half).min(h - 1);
                acc += tmp[sy * w + x] * kv;
            }
            out[y * w + x] = acc.round().clamp(0.0, 255.0) as u8;
        }
    }
    MaskBuffer { w: m.w, h: m.h, data: out }
}

/// Marching-squares outline of the selection (threshold 128). Returns segments [x0,y0,x1,y1]*.
pub fn outline(doc: &Document) -> Vec<f32> {
    let m = match &doc.selection { Some(m) => m, None => return Vec::new() };
    let (w, h) = (m.w as usize, m.h as usize);
    let at = |x: i64, y: i64| -> u8 {
        if x < 0 || y < 0 || x >= w as i64 || y >= h as i64 { 0 }
        else { m.data[(y as usize) * w + (x as usize)] }
    };
    let mut segs = Vec::new();
    for y in 0..(h as i64 + 1) {
        for x in 0..(w as i64 + 1) {
            // corners: tl, tr, br, bl
            let tl = at(x - 1, y - 1) >= 128;
            let tr = at(x, y - 1) >= 128;
            let br = at(x, y) >= 128;
            let bl = at(x - 1, y) >= 128;
            let code = (tl as u8) << 3 | (tr as u8) << 2 | (br as u8) << 1 | bl as u8;
            let (fx, fy) = (x as f32, y as f32);
            match code {
                0 | 15 => {}
                1 => segs.push([fx, fy, fx + 1.0, fy]),       // bl
                2 => segs.push([fx + 1.0, fy, fx + 1.0, fy + 1.0]), // br
                3 => segs.push([fx, fy, fx + 1.0, fy + 1.0]),
                4 => segs.push([fx + 1.0, fy - 1.0, fx + 1.0, fy]), // tr
                5 => { segs.push([fx, fy - 1.0, fx, fy]); segs.push([fx + 1.0, fy - 1.0, fx + 1.0, fy]); }
                6 => segs.push([fx + 1.0, fy - 1.0, fx + 1.0, fy + 1.0]),
                7 => segs.push([fx, fy - 1.0, fx, fy]),
                8 => segs.push([fx, fy - 1.0, fx, fy]),       // tl
                9 => segs.push([fx, fy - 1.0, fx + 1.0, fy + 1.0]),
                10 => { segs.push([fx, fy - 1.0, fx, fy]); segs.push([fx + 1.0, fy, fx + 1.0, fy + 1.0]); }
                11 => segs.push([fx + 1.0, fy - 1.0, fx + 1.0, fy]),
                12 => segs.push([fx, fy, fx, fy + 1.0]),
                13 => segs.push([fx + 1.0, fy, fx + 1.0, fy + 1.0]),
                14 => segs.push([fx, fy, fx + 1.0, fy]),
                _ => {}
            }
        }
    }
    segs.iter().flat_map(|s| s.iter().copied()).collect()
}

pub fn bounds(doc: &Document) -> Option<(i32, i32, u32, u32)> {
    let m = doc.selection.as_ref()?;
    let (mut x0, mut y0, mut x1, mut y1) = (i32::MAX, i32::MAX, i32::MIN, i32::MIN);
    let mut found = false;
    for y in 0..m.h {
        for x in 0..m.w {
            if m.data[(y * m.w + x) as usize] > 0 {
                found = true;
                x0 = x0.min(x as i32);
                y0 = y0.min(y as i32);
                x1 = x1.max(x as i32);
                y1 = y1.max(y as i32);
            }
        }
    }
    if !found { return None; }
    Some((x0, y0, (x1 - x0 + 1) as u32, (y1 - y0 + 1) as u32))
}
