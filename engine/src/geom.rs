//! Geometry: affine & perspective transforms with resampling, shape rasterization, fills, gradients.

use crate::doc::{Document, LayerKind};
use crate::pixel::{clamp_u8, PixelBuffer};
use std::sync::Arc;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Resample {
    Nearest,
    Bilinear,
    Bicubic,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Anchor {
    TopLeft, Top, TopRight,
    Left, Center, Right,
    BottomLeft, Bottom, BottomRight,
}

#[derive(Clone, Copy, Debug)]
pub struct Mat3 {
    pub m: [f64; 9], // row-major
}

impl Mat3 {
    pub fn identity() -> Self {
        Mat3 { m: [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0] }
    }
    /// Affine 2x3 (a b c d tx ty) — note: c = tx column.
    pub fn affine(a: f64, b: f64, c: f64, d: f64, tx: f64, ty: f64) -> Self {
        Mat3 { m: [a, b, tx, c, d, ty, 0.0, 0.0, 1.0] }
    }
    pub fn mul(&self, o: &Mat3) -> Mat3 {
        let mut r = [0f64; 9];
        for i in 0..3 {
            for j in 0..3 {
                let mut s = 0.0;
                for k in 0..3 {
                    s += self.m[i * 3 + k] * o.m[k * 3 + j];
                }
                r[i * 3 + j] = s;
            }
        }
        Mat3 { m: r }
    }
    pub fn inverse(&self) -> Option<Mat3> {
        let m = &self.m;
        let det = m[0] * (m[4] * m[8] - m[5] * m[7])
            - m[1] * (m[3] * m[8] - m[5] * m[6])
            + m[2] * (m[3] * m[7] - m[4] * m[6]);
        if det.abs() < 1e-12 { return None; }
        let id = 1.0 / det;
        Some(Mat3 { m: [
            (m[4] * m[8] - m[5] * m[7]) * id,
            (m[2] * m[7] - m[1] * m[8]) * id,
            (m[1] * m[5] - m[2] * m[4]) * id,
            (m[5] * m[6] - m[3] * m[8]) * id,
            (m[0] * m[8] - m[2] * m[6]) * id,
            (m[2] * m[3] - m[0] * m[5]) * id,
            (m[3] * m[7] - m[4] * m[6]) * id,
            (m[1] * m[6] - m[0] * m[7]) * id,
            (m[0] * m[4] - m[1] * m[3]) * id,
        ] })
    }
    #[inline]
    pub fn apply(&self, x: f64, y: f64) -> (f64, f64) {
        (
            self.m[0] * x + self.m[1] * y + self.m[2],
            self.m[3] * x + self.m[4] * y + self.m[5],
        )
    }
}

/// Homography mapping unit square (0,0),(1,0),(1,1),(0,1) onto 4 corner points.
pub fn homography_from_corners(corners: &[(f64, f64); 4]) -> Mat3 {
    // solve for H mapping (u,v) in unit square -> (x,y)
    let (x0, y0) = corners[0];
    let (x1, y1) = corners[1];
    let (x2, y2) = corners[2];
    let (x3, y3) = corners[3];
    // Standard direct linear transform for 4-point mapping
    let sx = x0 - x1 + x2 - x3;
    let sy = y0 - y1 + y2 - y3;
    if sx.abs() < 1e-12 && sy.abs() < 1e-12 {
        // affine
        return Mat3::affine(
            x1 - x0, x3 - x0, x0,
            y1 - y0, y3 - y0, y0,
        );
    }
    let dx1 = x1 - x2;
    let dx2 = x3 - x2;
    let dy1 = y1 - y2;
    let dy2 = y3 - y2;
    let denom = dx1 * dy2 - dx2 * dy1;
    if denom.abs() < 1e-12 {
        return Mat3::affine(x1 - x0, x3 - x0, x0, y1 - y0, y3 - y0, y0);
    }
    let g = (sx * dy2 - sy * dx2) / denom;
    let h = (sy * dx1 - sx * dy1) / denom;
    Mat3 { m: [
        x1 - x0 + g * x1, x3 - x0 + h * x3, x0,
        y1 - y0 + g * y1, y3 - y0 + h * y3, y0,
        g, h, 1.0,
    ] }
}

#[inline]
fn sample_bilinear(buf: &PixelBuffer, x: f64, y: f64) -> [u8; 4] {
    if x < 0.0 || y < 0.0 || x > buf.w as f64 - 1.0 || y > buf.h as f64 - 1.0 {
        return [0, 0, 0, 0];
    }
    let x0 = x.floor() as usize;
    let y0 = y.floor() as usize;
    let x1 = (x0 + 1).min(buf.w as usize - 1);
    let y1 = (y0 + 1).min(buf.h as usize - 1);
    let fx = x - x0 as f64;
    let fy = y - y0 as f64;
    let g = |xx: usize, yy: usize| -> [f64; 4] {
        let i = (yy * buf.w as usize + xx) * 4;
        let a = buf.data[i + 3] as f64 / 255.0;
        [buf.data[i] as f64 * a, buf.data[i + 1] as f64 * a, buf.data[i + 2] as f64 * a, a]
    };
    let c00 = g(x0, y0);
    let c10 = g(x1, y0);
    let c01 = g(x0, y1);
    let c11 = g(x1, y1);
    let mut out = [0f64; 4];
    for i in 0..4 {
        let top = c00[i] * (1.0 - fx) + c10[i] * fx;
        let bot = c01[i] * (1.0 - fx) + c11[i] * fx;
        out[i] = top * (1.0 - fy) + bot * fy;
    }
    if out[3] > 1e-6 {
        [(out[0] / out[3]).round() as u8, (out[1] / out[3]).round() as u8, (out[2] / out[3]).round() as u8, (out[3] * 255.0).round() as u8]
    } else {
        [0, 0, 0, 0]
    }
}

#[inline]
fn cubic_kernel(t: f64) -> f64 {
    // Catmull-Rom
    let a = t.abs();
    if a <= 1.0 { 1.5 * a * a * a - 2.5 * a * a + 1.0 }
    else if a < 2.0 { -0.5 * a * a * a + 2.5 * a * a - 4.0 * a + 2.0 }
    else { 0.0 }
}

#[inline]
fn sample_bicubic(buf: &PixelBuffer, x: f64, y: f64) -> [u8; 4] {
    if x < 0.0 || y < 0.0 || x > buf.w as f64 - 1.0 || y > buf.h as f64 - 1.0 {
        return [0, 0, 0, 0];
    }
    let x0 = x.floor() as i64;
    let y0 = y.floor() as i64;
    let fx = x - x0 as f64;
    let fy = y - y0 as f64;
    let mut acc = [0f64; 4];
    let mut wsum = 0f64;
    for dy in -1..=2i64 {
        let wy = cubic_kernel(dy as f64 - fy);
        if wy == 0.0 { continue; }
        for dx in -1..=2i64 {
            let wx = cubic_kernel(dx as f64 - fx);
            if wx == 0.0 { continue; }
            let xx = (x0 + dx).clamp(0, buf.w as i64 - 1) as usize;
            let yy = (y0 + dy).clamp(0, buf.h as i64 - 1) as usize;
            let w = wx * wy;
            let i = (yy * buf.w as usize + xx) * 4;
            let a = buf.data[i + 3] as f64 / 255.0;
            acc[0] += buf.data[i] as f64 * a * w;
            acc[1] += buf.data[i + 1] as f64 * a * w;
            acc[2] += buf.data[i + 2] as f64 * a * w;
            acc[3] += a * w;
            wsum += w;
        }
    }
    if acc[3] > 1e-6 {
        [(acc[0] / acc[3]).round() as u8, (acc[1] / acc[3]).round() as u8, (acc[2] / acc[3]).round() as u8, (acc[3] / wsum * 255.0).round() as u8]
    } else {
        [0, 0, 0, 0]
    }
}

#[inline]
fn sample_nearest(buf: &PixelBuffer, x: f64, y: f64) -> [u8; 4] {
    let xx = x.round() as i64;
    let yy = y.round() as i64;
    if xx < 0 || yy < 0 || xx >= buf.w as i64 || yy >= buf.h as i64 {
        return [0, 0, 0, 0];
    }
    let i = (yy as usize * buf.w as usize + xx as usize) * 4;
    [buf.data[i], buf.data[i + 1], buf.data[i + 2], buf.data[i + 3]]
}

pub fn sample(buf: &PixelBuffer, x: f64, y: f64, r: Resample) -> [u8; 4] {
    match r {
        Resample::Nearest => sample_nearest(buf, x, y),
        Resample::Bilinear => sample_bilinear(buf, x, y),
        Resample::Bicubic => sample_bicubic(buf, x, y),
    }
}

/// Resample a buffer to a new size.
pub fn resize_buffer(src: &PixelBuffer, nw: u32, nh: u32, r: Resample) -> Result<PixelBuffer, String> {
    let mut out = PixelBuffer::new(nw, nh, [0, 0, 0, 0])?;
    let sx = src.w as f64 / nw as f64;
    let sy = src.h as f64 / nh as f64;
    for y in 0..nh {
        for x in 0..nw {
            let fx = (x as f64 + 0.5) * sx - 0.5;
            let fy = (y as f64 + 0.5) * sy - 0.5;
            out.set(x, y, sample(src, fx, fy, r));
        }
    }
    Ok(out)
}

/// Transform a layer's pixels with an affine matrix (doc-space), baking offsets.
pub fn layer_affine(doc: &mut Document, layer_id: u64, m: &Mat3, r: Resample) -> Result<(), String> {
    let (w, h) = (doc.w, doc.h);
    let layer = doc.root.find(layer_id).ok_or("layer not found")?;
    if layer.kind != LayerKind::Raster { return Err("cannot transform a group".into()); }
    let src = layer.pixels.as_ref().ok_or("no pixels")?.as_ref().clone();
    let (lx, ly) = (layer.x, layer.y);
    let inv = m.inverse().ok_or("singular transform")?;
    let mut out = PixelBuffer::new(w, h, [0, 0, 0, 0])?;
    for y in 0..h {
        for x in 0..w {
            // dest doc coords -> src doc coords -> layer-local
            let (sx, sy) = inv.apply(x as f64 + 0.5, y as f64 + 0.5);
            let llx = sx - lx as f64 - 0.5;
            let lly = sy - ly as f64 - 0.5;
            let c = sample(&src, llx, lly, r);
            out.set(x, y, c);
        }
    }
    let layer = doc.root.find_mut(layer_id).unwrap();
    layer.pixels = Some(Arc::new(out));
    layer.x = 0;
    layer.y = 0;
    Ok(())
}

/// Perspective-warp a layer from its current doc rect to 4 dest corners.
pub fn layer_perspective(doc: &mut Document, layer_id: u64, corners: &[(f64, f64); 4], r: Resample) -> Result<(), String> {
    let layer = doc.root.find(layer_id).ok_or("layer not found")?;
    if layer.kind != LayerKind::Raster { return Err("cannot transform a group".into()); }
    let src = layer.pixels.as_ref().ok_or("no pixels")?.as_ref().clone();
    let (lx, ly) = (layer.x, layer.y);
    // src quad in doc space: layer rect corners (tl, tr, br, bl)
    let sw = src.w as f64;
    let sh = src.h as f64;
    let src_quad = [
        (lx as f64, ly as f64),
        (lx as f64 + sw, ly as f64),
        (lx as f64 + sw, ly as f64 + sh),
        (lx as f64, ly as f64 + sh),
    ];
    // H maps unit square -> src_quad; then compose with corners-mapping
    let h_src = homography_from_corners(&src_quad);
    let h_dst = homography_from_corners(corners);
    // We need mapping dest(doc) -> src(layer-local):
    // doc -> unit square (inverse of h_dst), then unit square -> src doc (h_src), then minus offset
    let inv_dst = h_dst.inverse().ok_or("degenerate corners")?;
    let full = h_src.mul(&inv_dst);
    let inv = full.inverse().ok_or("singular")?;
    let (w, h) = (doc.w, doc.h);
    let mut out = PixelBuffer::new(w, h, [0, 0, 0, 0])?;
    // limit to dest quad bbox
    let minx = corners.iter().map(|c| c.0).fold(f64::MAX, f64::min).floor().max(0.0) as u32;
    let maxx = corners.iter().map(|c| c.0).fold(f64::MIN, f64::max).ceil().min(w as f64) as u32;
    let miny = corners.iter().map(|c| c.1).fold(f64::MAX, f64::min).floor().max(0.0) as u32;
    let maxy = corners.iter().map(|c| c.1).fold(f64::MIN, f64::max).ceil().min(h as f64) as u32;
    for y in miny..maxy {
        for x in minx..maxx {
            let (sxd, syd) = inv.apply(x as f64 + 0.5, y as f64 + 0.5);
            let llx = sxd - lx as f64 - 0.5;
            let lly = syd - ly as f64 - 0.5;
            // check unit-square containment (with small tolerance)
            let (u, v) = inv_dst.apply(x as f64 + 0.5, y as f64 + 0.5);
            if u < -0.002 || v < -0.002 || u > 1.002 || v > 1.002 { continue; }
            let c = sample(&src, llx, lly, r);
            out.set(x, y, c);
        }
    }
    let layer = doc.root.find_mut(layer_id).unwrap();
    layer.pixels = Some(Arc::new(out));
    layer.x = 0;
    layer.y = 0;
    Ok(())
}

/// Anchor position of old content in new canvas.
pub fn anchor_offset(old_w: i64, old_h: i64, new_w: i64, new_h: i64, a: Anchor) -> (i64, i64) {
    let x = match a {
        Anchor::TopLeft | Anchor::Left | Anchor::BottomLeft => 0,
        Anchor::Top | Anchor::Center | Anchor::Bottom => (new_w - old_w) / 2,
        Anchor::TopRight | Anchor::Right | Anchor::BottomRight => new_w - old_w,
    };
    let y = match a {
        Anchor::TopLeft | Anchor::Top | Anchor::TopRight => 0,
        Anchor::Left | Anchor::Center | Anchor::Right => (new_h - old_h) / 2,
        Anchor::BottomLeft | Anchor::Bottom | Anchor::BottomRight => new_h - old_h,
    };
    (x, y)
}

// ---------------- Shape rasterization ----------------

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ShapeKind {
    Line,
    Rect,
    Ellipse,
    Polygon,
}

/// Draw an AA shape into the ACTIVE layer (doc coords), clipped by selection.
/// stroke: (rgba, width) optional; fill: rgba optional.
pub fn draw_shape(
    doc: &mut Document,
    kind: ShapeKind,
    pts: &[f64], // doc-space points
    stroke: Option<([u8; 4], f32)>,
    fill: Option<[u8; 4]>,
) -> Result<(), String> {
    if pts.len() < 4 { return Err("not enough points".into()); }
    let layer_id = doc.active;
    let layer = doc.root.find(layer_id).ok_or("no active layer")?;
    if layer.kind != LayerKind::Raster { return Err("cannot draw on a group".into()); }
    if layer.locked { return Err("layer is locked".into()); }
    let src = layer.pixels.as_ref().ok_or("no pixels")?.as_ref().clone();
    let (lx, ly) = (layer.x, layer.y);
    let (w, h) = (src.w, src.h);
    let mut out = src.clone();

    let sel = doc.selection.clone();

    // bbox in doc space (pad by stroke width)
    let pad = stroke.as_ref().map(|s| s.1.ceil() as i64 + 2).unwrap_or(2);
    let minx = pts.iter().step_by(2).fold(f64::MAX, |a, &b| a.min(b)).floor() as i64 - pad;
    let maxx = pts.iter().step_by(2).fold(f64::MIN, |a, &b| a.max(b)).ceil() as i64 + pad;
    let miny = pts.iter().skip(1).step_by(2).fold(f64::MAX, |a, &b| a.min(b)).floor() as i64 - pad;
    let maxy = pts.iter().skip(1).step_by(2).fold(f64::MIN, |a, &b| a.max(b)).ceil() as i64 + pad;

    let poly: Vec<(f64, f64)> = pts.chunks_exact(2).map(|c| (c[0], c[1])).collect();

    // signed distance to segment
    let dist_seg = |px: f64, py: f64, ax: f64, ay: f64, bx: f64, by: f64| -> f64 {
        let abx = bx - ax;
        let aby = by - ay;
        let apx = px - ax;
        let apy = py - ay;
        let t = (apx * abx + apy * aby) / (abx * abx + aby * aby).max(1e-12);
        let t = t.clamp(0.0, 1.0);
        let cx = ax + abx * t - px;
        let cy = ay + aby * t - py;
        (cx * cx + cy * cy).sqrt()
    };

    let closed_shape = kind == ShapeKind::Rect || kind == ShapeKind::Ellipse || kind == ShapeKind::Polygon;
    let n_edges = if kind == ShapeKind::Rect { 4 } else { poly.len() };
    // build edge list
    let mut edges: Vec<((f64, f64), (f64, f64))> = Vec::new();
    match kind {
        ShapeKind::Line => {
            edges.push((poly[0], poly[1]));
        }
        ShapeKind::Rect => {
            let (x0, y0) = poly[0];
            let (x1, y1) = poly[1];
            let (a, b) = (x0.min(x1), y0.min(y1));
            let (c, d) = (x0.max(x1), y0.max(y1));
            edges.push(((a, b), (c, b)));
            edges.push(((c, b), (c, d)));
            edges.push(((c, d), (a, d)));
            edges.push(((a, d), (a, b)));
        }
        ShapeKind::Ellipse => {
            // approximate with polyline (64 segments)
            let (x0, y0) = poly[0];
            let (x1, y1) = poly[1];
            let cx = (x0 + x1) / 2.0;
            let cy = (y0 + y1) / 2.0;
            let rx = ((x1 - x0).abs() / 2.0).max(1e-3);
            let ry = ((y1 - y0).abs() / 2.0).max(1e-3);
            let n = 64;
            for i in 0..n {
                let t0 = i as f64 / n as f64 * std::f64::consts::TAU;
                let t1 = (i + 1) as f64 / n as f64 * std::f64::consts::TAU;
                edges.push((
                    (cx + rx * t0.cos(), cy + ry * t0.sin()),
                    (cx + rx * t1.cos(), cy + ry * t1.sin()),
                ));
            }
        }
        ShapeKind::Polygon => {
            for i in 0..poly.len() {
                edges.push((poly[i], poly[(i + 1) % poly.len()]));
            }
        }
    }

    let inside_poly = |fx: f64, fy: f64| -> bool {
        let mut inside = false;
        let n = poly.len();
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

    let y_start = miny.max(0);
    let y_end = maxy.min(h as i64 + ly as i64);
    let x_start = minx.max(0);
    let x_end = maxx.min(w as i64 + lx as i64);

    for dy in y_start..y_end {
        for dx in x_start..x_end {
            // 2x2 supersample
            let mut stroke_cov = 0u32;
            let mut fill_cov = 0u32;
            for sy in 0..2 {
                for sx in 0..2 {
                    let fx = dx as f64 + (sx as f64 + 0.5) / 2.0;
                    let fy = dy as f64 + (sy as f64 + 0.5) / 2.0;
                    if let Some((_, width)) = &stroke {
                        let half = *width as f64 / 2.0 + 0.25;
                        let mut hit = false;
                        for ((ax, ay), (bx, by)) in &edges {
                            if dist_seg(fx, fy, *ax, *ay, *bx, *by) <= half {
                                hit = true;
                                break;
                            }
                        }
                        if hit { stroke_cov += 1; }
                    }
                    if let Some(_) = &fill {
                        if closed_shape {
                            if kind == ShapeKind::Ellipse {
                                let (x0, y0) = poly[0];
                                let (x1, y1) = poly[1];
                                let cx = (x0 + x1) / 2.0;
                                let cy = (y0 + y1) / 2.0;
                                let rx = ((x1 - x0).abs() / 2.0).max(1e-3);
                                let ry = ((y1 - y0).abs() / 2.0).max(1e-3);
                                let nx = (fx - cx) / rx;
                                let ny = (fy - cy) / ry;
                                if nx * nx + ny * ny <= 1.0 { fill_cov += 1; }
                            } else if kind == ShapeKind::Rect {
                                let (x0, y0) = poly[0];
                                let (x1, y1) = poly[1];
                                let (a, b) = (x0.min(x1), y0.min(y1));
                                let (c, d) = (x0.max(x1), y0.max(y1));
                                if fx >= a && fx <= c && fy >= b && fy <= d { fill_cov += 1; }
                            } else if inside_poly(fx, fy) {
                                fill_cov += 1;
                            }
                        }
                    }
                }
            }
            // convert doc -> layer coords
            let llx = dx as i32 - lx;
            let lly = dy as i32 - ly;
            if llx < 0 || lly < 0 || llx >= w as i32 || lly >= h as i32 { continue; }
            let sel_v = match &sel {
                None => 1.0f32,
                Some(m) => {
                    if dx < 0 || dy < 0 || dx >= m.w as i64 || dy >= m.h as i64 { 0.0 }
                    else { m.data[(dy as usize) * (m.w as usize) + (dx as usize)] as f32 / 255.0 }
                }
            };
            if sel_v <= 0.0 { continue; }
            let i = ((lly as usize) * (w as usize) + (llx as usize)) * 4;
            // apply fill first, then stroke on top
            if let Some(col) = &fill {
                let a = (fill_cov as f32 / 4.0) * (col[3] as f32 / 255.0) * sel_v;
                blend_px(&mut out.data, i, *col, a);
            }
            if let Some((col, _)) = &stroke {
                let a = (stroke_cov as f32 / 4.0) * (col[3] as f32 / 255.0) * sel_v;
                blend_px(&mut out.data, i, *col, a);
            }
        }
    }
    let _ = n_edges;
    let layer = doc.root.find_mut(layer_id).unwrap();
    layer.pixels = Some(Arc::new(out));
    Ok(())
}

#[inline]
fn blend_px(data: &mut [u8], i: usize, col: [u8; 4], a: f32) {
    if a <= 0.0 { return; }
    let sa = a;
    let da = data[i + 3] as f32 / 255.0;
    let oa = sa + da * (1.0 - sa);
    if oa <= 0.0 { return; }
    for c in 0..3 {
        let sc = col[c] as f32;
        let dc = data[i + c] as f32;
        data[i + c] = clamp_u8((sc * sa + dc * da * (1.0 - sa)) / oa);
    }
    data[i + 3] = clamp_u8(oa * 255.0);
}

// ---------------- Fills ----------------

/// Flood fill (bucket) at doc coords.
pub fn flood_fill(doc: &mut Document, x: i32, y: i32, col: [u8; 4], tolerance: u8, contiguous: bool) -> Result<(), String> {
    let layer = doc.active_layer().ok_or("no active layer")?;
    if layer.kind != LayerKind::Raster { return Err("cannot fill a group".into()); }
    if layer.locked { return Err("layer is locked".into()); }
    let px = layer.pixels.as_ref().ok_or("no pixels")?;
    let (lx, ly) = (layer.x, layer.y);
    let (w, h) = (px.w as usize, px.h as usize);
    let llx = x - lx;
    let lly = y - ly;
    if llx < 0 || lly < 0 || llx >= px.w as i32 || lly >= px.h as i32 {
        return Err("fill point outside layer".into());
    }
    let idx = (lly as usize * w + llx as usize) * 4;
    let target = [px.data[idx], px.data[idx + 1], px.data[idx + 2], px.data[idx + 3]];
    let tol = tolerance as i32;
    let matches = |data: &[u8], i: usize| {
        let d = (data[i] as i32 - target[0] as i32).abs()
            .max((data[i + 1] as i32 - target[1] as i32).abs())
            .max((data[i + 2] as i32 - target[2] as i32).abs())
            .max((data[i + 3] as i32 - target[3] as i32).abs());
        d <= tol
    };
    let sel = doc.selection.clone();
    let mut out = px.as_ref().clone();

    let set_px = |out: &mut PixelBuffer, xx: usize, yy: usize| {
        let selv = match &sel {
            None => 1.0f32,
            Some(m) => {
                let dx = xx as i32 + lx;
                let dy = yy as i32 + ly;
                if dx < 0 || dy < 0 || dx >= m.w as i32 || dy >= m.h as i32 { 0.0 }
                else { m.data[(dy as usize) * (m.w as usize) + (dx as usize)] as f32 / 255.0 }
            }
        };
        if selv <= 0.0 { return; }
        let a = (col[3] as f32 / 255.0) * selv;
        let i = (yy * w + xx) * 4;
        blend_px(&mut out.data, i, col, a);
    };

    if contiguous {
        let mut visited = vec![false; w * h];
        let mut stack: Vec<(i32, i32, i32)> = Vec::new();
        let mut sl = llx;
        let mut sr = llx;
        while sl > 0 && matches(&px.data, ((lly as usize) * w + (sl - 1) as usize) * 4) { sl -= 1; }
        while (sr as usize) < w - 1 && matches(&px.data, ((lly as usize) * w + (sr + 1) as usize) * 4) { sr += 1; }
        stack.push((lly, sl, sr));
        for xx in sl..=sr { visited[lly as usize * w + xx as usize] = true; }
        while let Some((yy, xl, xr)) = stack.pop() {
            for xx in xl..=xr {
                set_px(&mut out, xx as usize, yy as usize);
            }
            for ny in [yy - 1, yy + 1] {
                if ny < 0 || ny as usize >= h { continue; }
                let mut cx = xl;
                while cx <= xr {
                    let m = !visited[ny as usize * w + cx as usize] && matches(&px.data, (ny as usize * w + cx as usize) * 4);
                    if m {
                        let mut nl = cx;
                        while nl > 0 && !visited[ny as usize * w + (nl - 1) as usize] && matches(&px.data, (ny as usize * w + (nl - 1) as usize) * 4) { nl -= 1; }
                        let mut nr = cx;
                        while (nr as usize) < w - 1 && !visited[ny as usize * w + (nr + 1) as usize] && matches(&px.data, (ny as usize * w + (nr + 1) as usize) * 4) { nr += 1; }
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
        for yy in 0..h {
            for xx in 0..w {
                if matches(&px.data, (yy * w + xx) * 4) {
                    set_px(&mut out, xx, yy);
                }
            }
        }
    }
    let layer = doc.root.find_mut(doc.active).unwrap();
    layer.pixels = Some(Arc::new(out));
    Ok(())
}

/// Fill the selection (or whole layer) with a solid color.
pub fn fill_selection(doc: &mut Document, col: [u8; 4]) -> Result<(), String> {
    let layer = doc.active_layer().ok_or("no active layer")?;
    if layer.kind != LayerKind::Raster { return Err("cannot fill a group".into()); }
    if layer.locked { return Err("layer is locked".into()); }
    let src = layer.pixels.as_ref().ok_or("no pixels")?.as_ref().clone();
    let (lx, ly) = (layer.x, layer.y);
    let mut out = src.clone();
    let sel = doc.selection.clone();
    for y in 0..out.h {
        for x in 0..out.w {
            let v = match &sel {
                None => 1.0f32,
                Some(m) => {
                    let dx = x as i32 + lx;
                    let dy = y as i32 + ly;
                    if dx < 0 || dy < 0 || dx >= m.w as i32 || dy >= m.h as i32 { 0.0 }
                    else { m.data[(dy as usize) * (m.w as usize) + (dx as usize)] as f32 / 255.0 }
                }
            };
            if v > 0.0 {
                let i = PixelBuffer::idx(x, y, out.w);
                blend_px(&mut out.data, i, col, (col[3] as f32 / 255.0) * v);
            }
        }
    }
    let layer = doc.root.find_mut(doc.active).unwrap();
    layer.pixels = Some(Arc::new(out));
    Ok(())
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum GradientKind {
    Linear,
    Radial,
}

/// Fill the selection (or whole layer) with a 2-stop gradient (doc coords).
pub fn gradient_fill(
    doc: &mut Document,
    kind: GradientKind,
    x0: f64, y0: f64, x1: f64, y1: f64,
    c0: [u8; 4], c1: [u8; 4],
    dither: bool,
) -> Result<(), String> {
    let layer = doc.active_layer().ok_or("no active layer")?;
    if layer.kind != LayerKind::Raster { return Err("cannot fill a group".into()); }
    if layer.locked { return Err("layer is locked".into()); }
    let src = layer.pixels.as_ref().ok_or("no pixels")?.as_ref().clone();
    let (lx, ly) = (layer.x, layer.y);
    let mut out = src.clone();
    let sel = doc.selection.clone();
    let vx = x1 - x0;
    let vy = y1 - y0;
    let len2 = (vx * vx + vy * vy).max(1e-9);
    let mut rng: u32 = 0x9E3779B9;
    for y in 0..out.h {
        for x in 0..out.w {
            let dx = x as i64 + lx as i64;
            let dy = y as i64 + ly as i64;
            let v = match &sel {
                None => 1.0f32,
                Some(m) => {
                    if dx < 0 || dy < 0 || dx >= m.w as i64 || dy >= m.h as i64 { 0.0 }
                    else { m.data[(dy as usize) * (m.w as usize) + (dx as usize)] as f32 / 255.0 }
                }
            };
            if v <= 0.0 { continue; }
            let fx = x as f64 + 0.5 + lx as f64;
            let fy = y as f64 + 0.5 + ly as f64;
            let t = match kind {
                GradientKind::Linear => {
                    ((fx - x0) * vx + (fy - y0) * vy) / len2
                }
                GradientKind::Radial => {
                    let r = ((fx - x0) * (fx - x0) + (fy - y0) * (fy - y0)).sqrt();
                    let rl = len2.sqrt();
                    r / rl
                }
            };
            let mut t = t.clamp(0.0, 1.0) as f32;
            if dither {
                // ordered-ish dithering to reduce banding
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                t = (t + ((rng & 0xFF) as f32 / 255.0 - 0.5) * (1.0 / 64.0)).clamp(0.0, 1.0);
            }
            let col = [
                (c0[0] as f32 * (1.0 - t) + c1[0] as f32 * t) as u8,
                (c0[1] as f32 * (1.0 - t) + c1[1] as f32 * t) as u8,
                (c0[2] as f32 * (1.0 - t) + c1[2] as f32 * t) as u8,
                (c0[3] as f32 * (1.0 - t) + c1[3] as f32 * t) as u8,
            ];
            let i = PixelBuffer::idx(x, y, out.w);
            blend_px(&mut out.data, i, col, (col[3] as f32 / 255.0) * v);
        }
    }
    let layer = doc.root.find_mut(doc.active).unwrap();
    layer.pixels = Some(Arc::new(out));
    Ok(())
}
