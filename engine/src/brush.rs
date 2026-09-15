//! Brush engine: stamp-based strokes with pressure sensitivity, flow/opacity model, eraser & pencil.

use crate::doc::{Document, LayerKind};
use crate::pixel::{clamp_u8, PixelBuffer};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BrushTool {
    Brush,
    Pencil,
    Eraser,
}

#[derive(Clone, Copy, Debug)]
pub struct BrushParams {
    pub tool: BrushTool,
    pub size: f32,        // diameter px
    pub hardness: f32,    // 0..1
    pub opacity: f32,     // 0..1 stroke cap
    pub flow: f32,        // 0..1 per-dab alpha
    pub spacing: f32,     // fraction of size
    pub color: [u8; 4],
    pub pressure_size: bool,
    pub pressure_opacity: bool,
}

impl Default for BrushParams {
    fn default() -> Self {
        BrushParams {
            tool: BrushTool::Brush,
            size: 20.0, hardness: 0.5, opacity: 1.0, flow: 1.0, spacing: 0.12,
            color: [30, 30, 30, 255], pressure_size: false, pressure_opacity: false,
        }
    }
}

pub struct StrokeSession {
    pub layer_id: u64,
    pub params: BrushParams,
    pub working: PixelBuffer,   // live-mutated copy of the layer
    pub last: Option<(f32, f32, f32)>, // x, y, pressure (doc coords)
    pub leftover: f32,          // spacing accumulator
    pub dirty: (i32, i32, i32, i32), // bbox in doc coords
    pub min_pressure: f32,
}

impl StrokeSession {
    pub fn new(doc: &Document, params: BrushParams) -> Result<Self, String> {
        let layer = doc.active_layer().ok_or("no active layer")?;
        if layer.kind != LayerKind::Raster { return Err("cannot paint on a group layer".into()); }
        if layer.locked { return Err("layer is locked".into()); }
        let px = layer.pixels.as_ref().ok_or("layer has no pixels")?;
        Ok(StrokeSession {
            layer_id: layer.id,
            params,
            working: (**px).clone(),
            last: None,
            leftover: 0.0,
            dirty: (i32::MAX, i32::MAX, i32::MIN, i32::MIN),
            min_pressure: 1.0,
        })
    }

    fn mark_dirty(&mut self, x0: i32, y0: i32, x1: i32, y1: i32) {
        self.dirty.0 = self.dirty.0.min(x0);
        self.dirty.1 = self.dirty.1.min(y0);
        self.dirty.2 = self.dirty.2.max(x1);
        self.dirty.3 = self.dirty.3.max(y1);
    }

    /// Begin/move the stroke. Coordinates in DOCUMENT space; converted to layer space here.
    pub fn stroke_to(&mut self, doc: &Document, x: f32, y: f32, pressure: f32) {
        let p = pressure.clamp(0.0, 1.0).max(0.02);
        self.min_pressure = self.min_pressure.min(p);
        match self.last {
            None => {
                self.dab(doc, x, y, p);
                self.last = Some((x, y, p));
            }
            Some((lx, ly, lp)) => {
                let dx = x - lx;
                let dy = y - ly;
                let dist = (dx * dx + dy * dy).sqrt();
                let base_radius = self.effective_radius(p);
                let step = (self.params.spacing.max(0.01) * base_radius * 2.0).max(0.75);
                let mut travelled = self.leftover;
                while travelled + step <= dist {
                    travelled += step;
                    let t = travelled / dist;
                    let ix = lx + dx * t;
                    let iy = ly + dy * t;
                    let ip = lp + (p - lp) * t;
                    self.dab(doc, ix, iy, ip);
                }
                self.leftover = travelled.max(dist) - dist;
                if self.leftover < 0.0 { self.leftover = 0.0; }
                self.last = Some((x, y, p));
            }
        }
    }

    fn effective_radius(&self, pressure: f32) -> f32 {
        let mut r = self.params.size * 0.5;
        if self.params.pressure_size {
            r *= (0.15 + 0.85 * pressure).max(0.05);
        }
        r.max(0.5)
    }

    fn dab(&mut self, doc: &Document, x: f32, y: f32, pressure: f32) {
        let layer = match doc.root.find(self.layer_id) { Some(l) => l, None => return };
        let lx = x - layer.x as f32;
        let ly = y - layer.y as f32;
        let radius = self.effective_radius(pressure);
        let mut alpha = self.params.flow;
        if self.params.pressure_opacity {
            alpha *= (0.1 + 0.9 * pressure).max(0.02);
        }
        let cap = self.params.opacity;
        let hard = self.params.hardness.clamp(0.0, 1.0);
        let eraser = self.params.tool == BrushTool::Eraser;
        let pencil = self.params.tool == BrushTool::Pencil;
        let col = self.params.color;

        let x0f = (lx - radius).floor().max(0.0) as i32;
        let y0f = (ly - radius).floor().max(0.0) as i32;
        let x1f = (lx + radius).ceil() as i32;
        let y1f = (ly + radius).ceil() as i32;
        let w = self.working.w as i32;
        let h = self.working.h as i32;
        let x1 = x1f.min(w);
        let y1 = y1f.min(h);

        // selection clip lookup needs doc coords
        let sel = &doc.selection;
        let sel_off = (layer.x, layer.y);
        let has_sel = sel.is_some();

        for py in y0f..y1 {
            for px in x0f..x1 {
                let dx = px as f32 + 0.5 - lx;
                let dy = py as f32 + 0.5 - ly;
                let d = (dx * dx + dy * dy).sqrt() / radius;
                if d >= 1.0 { continue; }
                let cov: f32 = if pencil || hard >= 0.995 {
                    if d < 1.0 { 1.0 } else { 0.0 }
                } else {
                    // linear falloff from hardness edge
                    let t = if d <= hard { 0.0 } else { (d - hard) / (1.0 - hard).max(1e-4) };
                    // smoothstep for soft edge
                    let t = t * t * (3.0 - 2.0 * t);
                    1.0 - t
                };
                let mut a = alpha * cov;
                if a <= 0.0 { continue; }
                if has_sel {
                    let docx = px + sel_off.0;
                    let docy = py + sel_off.1;
                    let m = doc.sel_at(docx, docy);
                    a *= m;
                    if a <= 0.0 { continue; }
                }
                let i = ((py as usize) * (w as usize) + (px as usize)) * 4;
                let data = &mut self.working.data;
                if eraser {
                    // multiplicative buildup with cap (never increases alpha)
                    let cur = data[i + 3] as f32 / 255.0;
                    let floor = (1.0 - cap).min(cur);
                    let target = (cur * (1.0 - a)).max(floor);
                    data[i + 3] = (target * 255.0).round().clamp(0.0, 255.0) as u8;
                } else {
                    // source-over with stroke-opacity cap
                    let cur = data[i + 3] as f32 / 255.0;
                    let mut target = cur + a * (1.0 - cur);
                    if target > cap { target = cap; }
                    if target < cur { target = cur; }
                    if target > 1e-6 {
                        if cur > 0.999 {
                            // opaque destination: blend color toward brush color
                            for c in 0..3 {
                                let v = col[c] as f32 * a + data[i + c] as f32 * (1.0 - a);
                                data[i + c] = clamp_u8(v);
                            }
                        } else {
                            let denom = (1.0 - cur).max(1e-6);
                            let a_eff = ((target - cur) / denom).clamp(0.0, 1.0);
                            for c in 0..3 {
                                let v = (col[c] as f32 * a_eff + data[i + c] as f32 * cur * (1.0 - a_eff)) / target;
                                data[i + c] = clamp_u8(v);
                            }
                        }
                        data[i + 3] = clamp_u8(target * 255.0);
                    }
                }
            }
        }
        self.mark_dirty(x0f + layer.x, y0f + layer.y, x1 + layer.x, y1 + layer.y);
    }

    /// Commit the stroke to the layer (caller pushes history).
    pub fn commit(self, doc: &mut Document) {
        let layer = match doc.root.find_mut(self.layer_id) { Some(l) => l, None => return };
        layer.pixels = Some(std::sync::Arc::new(self.working));
    }
}
