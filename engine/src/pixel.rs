//! Pixel buffer primitives — straight (non-premultiplied) RGBA8, matching QImage::Format_RGBA8888.

pub const MAX_DIM: u32 = 16384;

#[derive(Clone, Debug)]
pub struct PixelBuffer {
    pub w: u32,
    pub h: u32,
    pub data: Vec<u8>, // RGBA8 straight alpha, len = w*h*4
}

#[derive(Clone, Debug)]
pub struct MaskBuffer {
    pub w: u32,
    pub h: u32,
    pub data: Vec<u8>, // 8-bit coverage, len = w*h
}

impl PixelBuffer {
    pub fn new(w: u32, h: u32, fill: [u8; 4]) -> Result<Self, String> {
        if w == 0 || h == 0 || w > MAX_DIM || h > MAX_DIM {
            return Err(format!("invalid buffer size {w}x{h}"));
        }
        let n = (w as usize) * (h as usize);
        if n > (1usize << 30) {
            return Err("buffer too large".into());
        }
        let mut data = vec![0u8; n * 4];
        if fill[3] != 0 {
            for px in data.chunks_exact_mut(4) {
                px.copy_from_slice(&fill);
            }
        }
        Ok(PixelBuffer { w, h, data })
    }

    #[inline]
    pub fn idx(x: u32, y: u32, w: u32) -> usize {
        ((y as usize) * (w as usize) + (x as usize)) * 4
    }

    #[inline]
    pub fn get(&self, x: u32, y: u32) -> [u8; 4] {
        let i = Self::idx(x, y, self.w);
        [self.data[i], self.data[i + 1], self.data[i + 2], self.data[i + 3]]
    }

    #[inline]
    pub fn set(&mut self, x: u32, y: u32, c: [u8; 4]) {
        let i = Self::idx(x, y, self.w);
        self.data[i] = c[0];
        self.data[i + 1] = c[1];
        self.data[i + 2] = c[2];
        self.data[i + 3] = c[3];
    }

    /// Downscale into a thumbnail fitting max_px (box filter). Returns (w,h,data).
    pub fn thumbnail(&self, max_px: u32) -> (u32, u32, Vec<u8>) {
        if self.w == 0 || self.h == 0 {
            return (1, 1, vec![0; 4]);
        }
        let scale = (max_px as f64 / self.w.max(self.h) as f64).min(1.0);
        let tw = ((self.w as f64 * scale).ceil() as u32).max(1);
        let th = ((self.h as f64 * scale).ceil() as u32).max(1);
        let mut out = vec![0u8; (tw * th * 4) as usize];
        let sx = self.w as f64 / tw as f64;
        let sy = self.h as f64 / th as f64;
        for ty in 0..th {
            let y0 = (sy * ty as f64).floor() as u32;
            let y1 = ((sy * (ty as f64 + 1.0)).ceil() as u32).clamp(y0 + 1, self.h);
            for tx in 0..tw {
                let x0 = (sx * tx as f64).floor() as u32;
                let x1 = ((sx * (tx as f64 + 1.0)).ceil() as u32).clamp(x0 + 1, self.w);
                let mut acc = [0f64; 4];
                let mut n = 0u32;
                for y in y0..y1 {
                    for x in x0..x1 {
                        let i = Self::idx(x, y, self.w);
                        let a = self.data[i + 3] as f64 / 255.0;
                        acc[0] += self.data[i] as f64 * a;
                        acc[1] += self.data[i + 1] as f64 * a;
                        acc[2] += self.data[i + 2] as f64 * a;
                        acc[3] += a;
                        n += 1;
                    }
                }
                let o = ((ty * tw + tx) * 4) as usize;
                if acc[3] > 0.0 {
                    out[o] = (acc[0] / acc[3]).round().clamp(0.0, 255.0) as u8;
                    out[o + 1] = (acc[1] / acc[3]).round().clamp(0.0, 255.0) as u8;
                    out[o + 2] = (acc[2] / acc[3]).round().clamp(0.0, 255.0) as u8;
                }
                out[o + 3] = (acc[3] / n as f64 * 255.0).round().clamp(0.0, 255.0) as u8;
            }
        }
        (tw, th, out)
    }
}

impl MaskBuffer {
    pub fn new(w: u32, h: u32, fill: u8) -> Result<Self, String> {
        if w == 0 || h == 0 || w > MAX_DIM || h > MAX_DIM {
            return Err(format!("invalid mask size {w}x{h}"));
        }
        Ok(MaskBuffer { w, h, data: vec![fill; (w * h) as usize] })
    }

    #[inline]
    pub fn get(&self, x: u32, y: u32) -> u8 {
        self.data[(y as usize) * (self.w as usize) + (x as usize)]
    }

    #[inline]
    pub fn set(&mut self, x: u32, y: u32, v: u8) {
        let i = (y as usize) * (self.w as usize) + (x as usize);
        self.data[i] = v;
    }
}

#[inline]
pub fn clamp_u8(v: f32) -> u8 {
    if v <= 0.0 { 0 } else if v >= 255.0 { 255 } else { v as u8 }
}

#[inline]
pub fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}
