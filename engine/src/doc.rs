//! Document model: layer tree (raster + groups + masks), compositing, history, JSON serialization.

use crate::blend::{blend_color, BlendMode};
use crate::brush::StrokeSession;
use crate::pixel::{clamp_u8, MaskBuffer, PixelBuffer};
use serde::Serialize;
use std::sync::Arc;

#[derive(Clone, Copy, PartialEq, Eq, Debug, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum LayerKind {
    Raster,
    Group,
}

#[derive(Clone, Debug)]
pub struct LayerNode {
    pub id: u64,
    pub name: String,
    pub kind: LayerKind,
    pub visible: bool,
    pub locked: bool,
    pub opacity: f32, // 0..1
    pub blend: BlendMode,
    pub x: i32,
    pub y: i32,
    pub pixels: Option<Arc<PixelBuffer>>,
    pub mask: Option<Arc<MaskBuffer>>,
    pub mask_enabled: bool,
    pub children: Vec<LayerNode>, // for groups (top-most child first)
}

impl LayerNode {
    pub fn new_raster(id: u64, name: impl Into<String>, pixels: PixelBuffer) -> Self {
        LayerNode {
            id, name: name.into(), kind: LayerKind::Raster,
            visible: true, locked: false, opacity: 1.0, blend: BlendMode::Normal,
            x: 0, y: 0, pixels: Some(Arc::new(pixels)), mask: None, mask_enabled: true,
            children: Vec::new(),
        }
    }
    pub fn new_group(id: u64, name: impl Into<String>) -> Self {
        LayerNode {
            id, name: name.into(), kind: LayerKind::Group,
            visible: true, locked: false, opacity: 1.0, blend: BlendMode::Normal,
            x: 0, y: 0, pixels: None, mask: None, mask_enabled: true,
            children: Vec::new(),
        }
    }

    pub fn find(&self, id: u64) -> Option<&LayerNode> {
        if self.id == id { return Some(self); }
        for c in &self.children {
            if let Some(f) = c.find(id) { return Some(f); }
        }
        None
    }

    pub fn find_mut(&mut self, id: u64) -> Option<&mut LayerNode> {
        if self.id == id { return Some(self); }
        for c in self.children.iter_mut() {
            if let Some(f) = c.find_mut(id) { return Some(f); }
        }
        None
    }

    pub fn remove(&mut self, id: u64) -> Option<LayerNode> {
        for i in 0..self.children.len() {
            if self.children[i].id == id {
                return Some(self.children.remove(i));
            }
        }
        for c in self.children.iter_mut() {
            if let Some(r) = c.remove(id) { return Some(r); }
        }
        None
    }

    pub fn count_all(&self) -> usize {
        1 + self.children.iter().map(|c| c.count_all()).sum::<usize>()
    }

    pub fn memory_bytes(&self) -> u64 {
        let mut n = 0u64;
        if let Some(p) = &self.pixels { n += (p.data.len() + Arc::strong_count(p) * 0) as u64; n += p.data.len() as u64; }
        if let Some(m) = &self.mask { n += m.data.len() as u64; }
        n += self.children.iter().map(|c| c.memory_bytes()).sum::<u64>();
        n
    }

    /// Depth-first flatten for JSON export
    pub fn walk(&self, f: &mut impl FnMut(&LayerNode)) {
        f(self);
        for c in &self.children { c.walk(f); }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Rect { pub x: i32, pub y: i32, pub w: u32, pub h: u32 }

#[derive(Clone)]
pub struct DocState {
    pub w: u32,
    pub h: u32,
    pub root: LayerNode,
    pub active: u64,
    pub selection: Option<Arc<MaskBuffer>>,
}

pub struct History {
    pub states: Vec<(String, DocState)>,
    pub index: usize,
    pub limit: usize,
}

impl History {
    pub fn new(initial: DocState, limit: usize) -> Self {
        History { states: vec![("Document".to_string(), initial)], index: 0, limit }
    }
    pub fn push(&mut self, label: &str, state: DocState) {
        self.states.truncate(self.index + 1);
        self.states.push((label.to_string(), state));
        if self.states.len() > self.limit {
            let drop = self.states.len() - self.limit;
            self.states.drain(0..drop);
        }
        self.index = self.states.len() - 1;
    }
    pub fn undo(&mut self) -> bool {
        if self.index == 0 { return false; }
        self.index -= 1;
        true
    }
    pub fn redo(&mut self) -> bool {
        if self.index + 1 >= self.states.len() { return false; }
        self.index += 1;
        true
    }
    pub fn goto(&mut self, i: usize) -> bool {
        if i < self.states.len() { self.index = i; true } else { false }
    }
    pub fn current(&self) -> &DocState {
        &self.states[self.index].1
    }
}

pub struct Document {
    pub w: u32,
    pub h: u32,
    pub root: LayerNode,
    pub active: u64,
    pub selection: Option<Arc<MaskBuffer>>,
    pub history: History,
    pub next_id: u64,
    pub modified: bool,
    pub path: Option<String>,
    pub previewing: Option<DocState>,
    pub stroke: Option<StrokeSession>,
    pub cached_json: Option<String>,
    pub cached_outline: Option<Vec<f32>>,
}

impl Document {
    pub fn new(w: u32, h: u32, fill: [u8; 4]) -> Result<Self, String> {
        let bg = PixelBuffer::new(w, h, fill)?;
        let mut root = LayerNode::new_group(1, "Root");
        root.children.push(LayerNode::new_raster(2, "Background", bg));
        let doc = Document {
            w, h,
            root: root.clone(),
            active: 2,
            selection: None,
            history: History::new(DocState { w, h, root, active: 2, selection: None }, 64),
            next_id: 3,
            modified: false,
            path: None,
            previewing: None,
            stroke: None,
            cached_json: None,
            cached_outline: None,
        };
        Ok(doc)
    }

    pub fn empty_history_root(w: u32, h: u32, root: LayerNode, active: u64) -> Self {
        let next = count_max_id(&root) + 1;
        let sel = None;
        Document {
            w, h, root: root.clone(), active,
            selection: None,
            history: History::new(DocState { w, h, root, active, selection: sel }, 64),
            next_id: next,
            modified: false, path: None, previewing: None, stroke: None,
            cached_json: None, cached_outline: None,
        }
    }

    pub fn state(&self) -> DocState {
        DocState { w: self.w, h: self.h, root: self.root.clone(), active: self.active, selection: self.selection.clone() }
    }

    pub fn apply_state(&mut self, s: &DocState) {
        self.w = s.w;
        self.h = s.h;
        self.root = s.root.clone();
        self.active = s.active;
        self.selection = s.selection.clone();
        self.cached_json = None;
        self.cached_outline = None;
    }

    /// Record a history entry (after a completed operation). No-op while previewing.
    pub fn push_history(&mut self, label: &str) {
        if self.previewing.is_some() { return; }
        self.history.push(label, self.state());
        self.modified = true;
        self.cached_json = None;
    }

    pub fn alloc_id(&mut self) -> u64 {
        self.next_id += 1;
        self.next_id - 1
    }

    pub fn active_layer(&self) -> Option<&LayerNode> {
        self.root.find(self.active)
    }

    pub fn active_layer_mut(&mut self) -> Option<&mut LayerNode> {
        self.root.find_mut(self.active)
    }

    /// Mutable raster buffer of the active layer (must exist and be unlocked raster).
    pub fn active_pixels(&self) -> Result<Arc<PixelBuffer>, String> {
        match self.active_layer() {
            Some(l) if l.kind == LayerKind::Raster => {
                l.pixels.clone().ok_or_else(|| "active layer has no pixels".to_string())
            }
            Some(_) => Err("active layer is a group".into()),
            None => Err("no active layer".into()),
        }
    }

    /// Selection coverage at doc coords (255 if no selection)
    #[inline]
    pub fn sel_at(&self, x: i32, y: i32) -> f32 {
        if x < 0 || y < 0 { return 0.0; }
        match &self.selection {
            None => 1.0,
            Some(m) => {
                let (ux, uy) = (x as u32, y as u32);
                if ux >= m.w || uy >= m.h { 0.0 } else { m.data[(uy * m.w + ux) as usize] as f32 / 255.0 }
            }
        }
    }

    // ---------- Compositing ----------

    /// Composite the whole stack into `out` (straight RGBA, w*h*4 for `region`).
    pub fn composite(&self, region: Rect, out: &mut [u8]) {
        let rw = region.w as usize;
        let rh = region.h as usize;
        debug_assert!(out.len() >= rw * rh * 4);
        for v in out[..rw * rh * 4].iter_mut() { *v = 0; }
        self.render_into(&self.root, region, out, rw);
    }

    fn render_into(&self, node: &LayerNode, region: Rect, out: &mut [u8], rw: usize) {
        if !node.visible || node.opacity <= 0.0 { return; }
        match node.kind {
            LayerKind::Group => {
                let mut tmp = PixelBuffer::from_raw(region.w, region.h, vec![0u8; rw * region.h as usize * 4]);
                for child in &node.children {
                    self.render_into(child, region, &mut tmp.data, rw);
                }
                blend_buffer_region(out, rw, region, &tmp, region.x, region.y, node.opacity, node.blend, None, None);
            }
            LayerKind::Raster => {
                let px = match &node.pixels { Some(p) => p, None => return };
                // Stroke session live preview: if this layer is being stroked, blend the working buffer instead
                if let Some(s) = &self.stroke {
                    if s.layer_id == node.id {
                        blend_buffer_region(out, rw, region, &s.working, node.x, node.y, node.opacity, node.blend,
                            node.mask.as_deref().filter(|_| node.mask_enabled), None);
                        return;
                    }
                }
                blend_buffer_region(out, rw, region, px, node.x, node.y, node.opacity, node.blend,
                    node.mask.as_deref().filter(|_| node.mask_enabled), None);
            }
        }
    }

    pub fn layer_json(&mut self) -> String {
        if let Some(j) = &self.cached_json { return j.clone(); }
        #[derive(Serialize)]
        struct JLayer<'a> {
            id: u64,
            name: &'a str,
            kind: &'static str,
            visible: bool,
            locked: bool,
            opacity: f32,
            blend: &'static str,
            x: i32,
            y: i32,
            has_mask: bool,
            mask_enabled: bool,
            children: Vec<JLayer<'a>>,
        }
        fn conv(l: &LayerNode) -> JLayer<'_> {
            JLayer {
                id: l.id, name: &l.name,
                kind: if l.kind == LayerKind::Group { "group" } else { "raster" },
                visible: l.visible, locked: l.locked, opacity: l.opacity,
                blend: l.blend.name(), x: l.x, y: l.y,
                has_mask: l.mask.is_some(), mask_enabled: l.mask_enabled,
                children: l.children.iter().map(conv).collect(),
            }
        }
        let mut js = serde_json::to_string(&conv(&self.root)).unwrap_or_else(|_| "{}".into());
        js.push_str(&format!("|{}", self.active));
        self.cached_json = Some(js.clone());
        js
    }

    pub fn histogram(&self, layer_id: u64, out: &mut [u32; 1024]) {
        for v in out.iter_mut() { *v = 0; }
        let mut tally = |data: &[u8]| {
            for px in data.chunks_exact(4) {
                out[px[0] as usize] += 1;
                out[256 + px[1] as usize] += 1;
                out[512 + px[2] as usize] += 1;
                let luma = ((0.299 * px[0] as f32 + 0.587 * px[1] as f32 + 0.114 * px[2] as f32) as usize).min(255);
                out[768 + luma] += 1;
            }
        };
        if layer_id == 0 {
            let mut b = vec![0u8; (self.w as usize) * (self.h as usize) * 4];
            self.composite(Rect { x: 0, y: 0, w: self.w, h: self.h }, &mut b);
            tally(&b);
        } else if let Some(p) = self.root.find(layer_id).and_then(|l| l.pixels.as_deref()) {
            tally(&p.data);
        }
    }
}

impl PixelBuffer {
    pub fn from_raw(w: u32, h: u32, data: Vec<u8>) -> PixelBuffer {
        PixelBuffer { w, h, data }
    }
}

fn count_max_id(root: &LayerNode) -> u64 {
    let mut m = root.id;
    root.walk(&mut |l| m = m.max(l.id));
    m
}

/// Blend a source layer (doc-space position sx_off, sy_off) onto dst within `region`.
fn blend_buffer_region(
    dst: &mut [u8], dst_w: usize, region: Rect,
    src: &PixelBuffer, sx_off: i32, sy_off: i32,
    opacity: f32, mode: BlendMode,
    mask: Option<&MaskBuffer>, _locked_fade: Option<f32>,
) {
    // Source rect in doc space
    let sx0 = sx_off;
    let sy0 = sy_off;
    let sx1 = sx_off + src.w as i32;
    let sy1 = sy_off + src.h as i32;
    // Intersect with region
    let rx0 = region.x.max(sx0);
    let ry0 = region.y.max(sy0);
    let rx1 = (region.x + region.w as i32).min(sx1);
    let ry1 = (region.y + region.h as i32).min(sy1);
    if rx0 >= rx1 || ry0 >= ry1 { return; }
    let (rx0, ry0, rx1, ry1) = (rx0 as u32, ry0 as u32, rx1 as u32, ry1 as u32);
    let base_x = region.x as u32;
    let base_y = region.y as u32;
    dst.par_chunks_mut(dst_w * 4).enumerate().for_each(|(row, row_slice)| {
        let dy = base_y + row as u32;
        if dy < ry0 || dy >= ry1 { return; }
        let src_y = dy as i32 - sy0;
        for dx in rx0..rx1 {
            let src_x = dx as i32 - sx0;
            let si = ((src_y as usize) * (src.w as usize) + (src_x as usize)) * 4;
            let mut sa = src.data[si + 3] as f32 / 255.0 * opacity;
            // mask lookup (mask is stored in layer-local coords)
            if let Some(m) = mask {
                let mx = src_x;
                let my = src_y;
                if mx < 0 || my < 0 || mx >= m.w as i32 || my >= m.h as i32 {
                    sa = 0.0;
                } else {
                    sa *= m.data[(my as usize) * (m.w as usize) + (mx as usize)] as f32 / 255.0;
                }
            }
            if sa <= 0.0 { continue; }
            let di = ((dx - base_x) as usize) * 4;
            let ad = row_slice[di + 3] as f32 / 255.0;
            let cs = [
                src.data[si] as f32 / 255.0,
                src.data[si + 1] as f32 / 255.0,
                src.data[si + 2] as f32 / 255.0,
            ];
            let cd = [
                row_slice[di] as f32 / 255.0,
                row_slice[di + 1] as f32 / 255.0,
                row_slice[di + 2] as f32 / 255.0,
            ];
            let b = blend_color(mode, cs, cd);
            let out_a = sa + ad * (1.0 - sa);
            if out_a <= 0.0 {
                row_slice[di] = 0; row_slice[di + 1] = 0; row_slice[di + 2] = 0; row_slice[di + 3] = 0;
                return;
            }
            let pm = |bc: f32, sc: f32, dc: f32| (bc * sa + dc * ad * (1.0 - sa)) / out_a;
            row_slice[di] = clamp_u8(pm(b[0], cs[0], cd[0]) * 255.0);
            row_slice[di + 1] = clamp_u8(pm(b[1], cs[1], cd[1]) * 255.0);
            row_slice[di + 2] = clamp_u8(pm(b[2], cs[2], cd[2]) * 255.0);
            row_slice[di + 3] = clamp_u8(out_a * 255.0);
        }
    });
}

/// Public wrapper used by merge-down.
pub fn blend_pub(dst: &mut [u8], dst_w: u32, region: &Rect, src: &PixelBuffer, sx: i32, sy: i32, opacity: f32, mode: BlendMode, mask: Option<&MaskBuffer>) {
    blend_buffer_region(dst, dst_w as usize, *region, src, sx, sy, opacity, mode, mask, None);
}

use rayon::prelude::*;
