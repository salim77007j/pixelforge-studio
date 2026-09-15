//! Blend modes operating on straight-alpha pixels (W3C compositing formulas).

use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, PartialEq, Eq, Debug, Serialize, Deserialize)]
pub enum BlendMode {
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity,
}

pub const BLEND_MODES: &[(&str, BlendMode)] = &[
    ("Normal", BlendMode::Normal),
    ("Multiply", BlendMode::Multiply),
    ("Screen", BlendMode::Screen),
    ("Overlay", BlendMode::Overlay),
    ("Darken", BlendMode::Darken),
    ("Lighten", BlendMode::Lighten),
    ("Color Dodge", BlendMode::ColorDodge),
    ("Color Burn", BlendMode::ColorBurn),
    ("Hard Light", BlendMode::HardLight),
    ("Soft Light", BlendMode::SoftLight),
    ("Difference", BlendMode::Difference),
    ("Exclusion", BlendMode::Exclusion),
    ("Hue", BlendMode::Hue),
    ("Saturation", BlendMode::Saturation),
    ("Color", BlendMode::Color),
    ("Luminosity", BlendMode::Luminosity),
];

impl BlendMode {
    pub fn name(&self) -> &'static str {
        for (n, m) in BLEND_MODES {
            if m == self { return n; }
        }
        "Normal"
    }
    pub fn from_name(s: &str) -> BlendMode {
        for (n, m) in BLEND_MODES {
            if n.eq_ignore_ascii_case(s) { return *m; }
        }
        BlendMode::Normal
    }
    /// Map OpenRaster composite-op names.
    pub fn from_ora(op: &str) -> BlendMode {
        match op {
            "svg:multiply" => BlendMode::Multiply,
            "svg:screen" => BlendMode::Screen,
            "svg:overlay" => BlendMode::Overlay,
            "svg:darken" => BlendMode::Darken,
            "svg:lighten" => BlendMode::Lighten,
            "svg:color-dodge" => BlendMode::ColorDodge,
            "svg:color-burn" => BlendMode::ColorBurn,
            "svg:hard-light" => BlendMode::HardLight,
            "svg:soft-light" => BlendMode::SoftLight,
            "svg:difference" => BlendMode::Difference,
            "svg:exclusion" => BlendMode::Exclusion,
            "svg:hue" => BlendMode::Hue,
            "svg:saturation" => BlendMode::Saturation,
            "svg:color" => BlendMode::Color,
            "svg:luminosity" => BlendMode::Luminosity,
            _ => BlendMode::Normal,
        }
    }
    pub fn to_ora(&self) -> &'static str {
        match self {
            BlendMode::Normal => "svg:src-over",
            BlendMode::Multiply => "svg:multiply",
            BlendMode::Screen => "svg:screen",
            BlendMode::Overlay => "svg:overlay",
            BlendMode::Darken => "svg:darken",
            BlendMode::Lighten => "svg:lighten",
            BlendMode::ColorDodge => "svg:color-dodge",
            BlendMode::ColorBurn => "svg:color-burn",
            BlendMode::HardLight => "svg:hard-light",
            BlendMode::SoftLight => "svg:soft-light",
            BlendMode::Difference => "svg:difference",
            BlendMode::Exclusion => "svg:exclusion",
            BlendMode::Hue => "svg:hue",
            BlendMode::Saturation => "svg:saturation",
            BlendMode::Color => "svg:color",
            BlendMode::Luminosity => "svg:luminosity",
        }
    }
}

#[inline]
fn lum(c: [f32; 3]) -> f32 {
    0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
}

#[inline]
fn clip_color(mut c: [f32; 3]) -> [f32; 3] {
    let l = lum(c);
    let n = c[0].min(c[1]).min(c[2]);
    let x = c[0].max(c[1]).max(c[2]);
    if n < 0.0 {
        for v in c.iter_mut() { *v = l + (*v - l) * l / (l - n + 1e-6); }
    }
    if x > 1.0 {
        for v in c.iter_mut() { *v = l + (*v - l) * (1.0 - l) / (x - l + 1e-6); }
    }
    [c[0].clamp(0.0, 1.0), c[1].clamp(0.0, 1.0), c[2].clamp(0.0, 1.0)]
}

/// W3C SetLum
fn set_lum(c: [f32; 3], nl: f32) -> [f32; 3] {
    let d = nl - lum(c);
    clip_color([c[0] + d, c[1] + d, c[2] + d])
}

/// W3C SetSat: scale chroma of c to saturation s
fn set_sat(c: [f32; 3], s: f32) -> [f32; 3] {
    // indices of min, mid, max
    let (mn, mx) = (c[0].min(c[1]).min(c[2]), c[0].max(c[1]).max(c[2]));
    if mx - mn < 1e-9 {
        return [0.0, 0.0, 0.0];
    }
    let mut out = [0f32; 3];
    for i in 0..3 {
        if (c[i] - mx).abs() < 1e-9 {
            out[i] = s;
        } else if (c[i] - mn).abs() < 1e-9 {
            out[i] = 0.0;
        } else {
            out[i] = s * (c[i] - mn) / (mx - mn);
        }
    }
    out
}

/// RGB in 0..1 -> HSL (h in 0..360, s,l in 0..1)
pub fn rgb_to_hsl(rgb: [f32; 3]) -> [f32; 3] {
    let r = rgb[0]; let g = rgb[1]; let b = rgb[2];
    let max = r.max(g).max(b);
    let min = r.min(g).min(b);
    let l = (max + min) / 2.0;
    if (max - min).abs() < 1e-9 {
        return [0.0, 0.0, l];
    }
    let d = max - min;
    let s = if l > 0.5 { d / (2.0 - max - min) } else { d / (max + min) };
    let h = if (max - r).abs() < 1e-9 {
        (g - b) / d + if g < b { 6.0 } else { 0.0 }
    } else if (max - g).abs() < 1e-9 {
        (b - r) / d + 2.0
    } else {
        (r - g) / d + 4.0
    };
    [h * 60.0, s, l]
}

/// HSL -> RGB (h 0..360, s,l 0..1)
pub fn hsl_to_rgb(hsl: [f32; 3]) -> [f32; 3] {
    let h = (((hsl[0] % 360.0) + 360.0) % 360.0) / 360.0;
    let s = hsl[1].clamp(0.0, 1.0);
    let l = hsl[2].clamp(0.0, 1.0);
    if s <= 0.0 {
        return [l, l, l];
    }
    let q = if l < 0.5 { l * (1.0 + s) } else { l + s - l * s };
    let p = 2.0 * l - q;
    let hue = |mut t: f32| {
        if t < 0.0 { t += 1.0; }
        if t > 1.0 { t -= 1.0; }
        if t < 1.0 / 6.0 { p + (q - p) * 6.0 * t }
        else if t < 0.5 { q }
        else if t < 2.0 / 3.0 { p + (q - p) * (2.0 / 3.0 - t) * 6.0 }
        else { p }
    };
    [hue(h + 1.0 / 3.0), hue(h), hue(h - 1.0 / 3.0)]
}

/// Blend two unpremultiplied colors (0..1 range). cs = source, cd = backdrop.
pub fn blend_color(mode: BlendMode, cs: [f32; 3], cd: [f32; 3]) -> [f32; 3] {
    match mode {
        BlendMode::Normal => cs,
        BlendMode::Multiply => [cs[0] * cd[0], cs[1] * cd[1], cs[2] * cd[2]],
        BlendMode::Screen => [cs[0] + cd[0] - cs[0] * cd[0], cs[1] + cd[1] - cs[1] * cd[1], cs[2] + cd[2] - cs[2] * cd[2]],
        BlendMode::Overlay => [
            if cd[0] <= 0.5 { 2.0 * cd[0] * cs[0] } else { 1.0 - 2.0 * (1.0 - cd[0]) * (1.0 - cs[0]) },
            if cd[1] <= 0.5 { 2.0 * cd[1] * cs[1] } else { 1.0 - 2.0 * (1.0 - cd[1]) * (1.0 - cs[1]) },
            if cd[2] <= 0.5 { 2.0 * cd[2] * cs[2] } else { 1.0 - 2.0 * (1.0 - cd[2]) * (1.0 - cs[2]) },
        ],
        BlendMode::Darken => [cs[0].min(cd[0]), cs[1].min(cd[1]), cs[2].min(cd[2])],
        BlendMode::Lighten => [cs[0].max(cd[0]), cs[1].max(cd[1]), cs[2].max(cd[2])],
        BlendMode::ColorDodge => [
            if cd[0] >= 1.0 - 1e-6 { 1.0 } else if cs[0] <= 1e-6 { cd[0] } else { (cd[0] / (1.0 - cs[0])).min(1.0) },
            if cd[1] >= 1.0 - 1e-6 { 1.0 } else if cs[1] <= 1e-6 { cd[1] } else { (cd[1] / (1.0 - cs[1])).min(1.0) },
            if cd[2] >= 1.0 - 1e-6 { 1.0 } else if cs[2] <= 1e-6 { cd[2] } else { (cd[2] / (1.0 - cs[2])).min(1.0) },
        ],
        BlendMode::ColorBurn => [
            if cd[0] <= 1e-6 { 0.0 } else if cs[0] >= 1.0 - 1e-6 { 1.0 } else { 1.0 - ((1.0 - cd[0]) / cs[0]).min(1.0) },
            if cd[1] <= 1e-6 { 0.0 } else if cs[1] >= 1.0 - 1e-6 { 1.0 } else { 1.0 - ((1.0 - cd[1]) / cs[1]).min(1.0) },
            if cd[2] <= 1e-6 { 0.0 } else if cs[2] >= 1.0 - 1e-6 { 1.0 } else { 1.0 - ((1.0 - cd[2]) / cs[2]).min(1.0) },
        ],
        BlendMode::HardLight => [
            if cs[0] <= 0.5 { 2.0 * cs[0] * cd[0] } else { 1.0 - 2.0 * (1.0 - cs[0]) * (1.0 - cd[0]) },
            if cs[1] <= 0.5 { 2.0 * cs[1] * cd[1] } else { 1.0 - 2.0 * (1.0 - cs[1]) * (1.0 - cd[1]) },
            if cs[2] <= 0.5 { 2.0 * cs[2] * cd[2] } else { 1.0 - 2.0 * (1.0 - cs[2]) * (1.0 - cd[2]) },
        ],
        BlendMode::SoftLight => [
            if cs[0] <= 0.5 { cd[0] - (1.0 - 2.0 * cs[0]) * cd[0] * (1.0 - cd[0]) } else { let d = if cd[0] <= 0.25 { ((16.0 * cd[0] - 12.0) * cd[0] + 4.0) * cd[0] } else { cd[0].sqrt() }; cd[0] + (2.0 * cs[0] - 1.0) * (d - cd[0]) },
            if cs[1] <= 0.5 { cd[1] - (1.0 - 2.0 * cs[1]) * cd[1] * (1.0 - cd[1]) } else { let d = if cd[1] <= 0.25 { ((16.0 * cd[1] - 12.0) * cd[1] + 4.0) * cd[1] } else { cd[1].sqrt() }; cd[1] + (2.0 * cs[1] - 1.0) * (d - cd[1]) },
            if cs[2] <= 0.5 { cd[2] - (1.0 - 2.0 * cs[2]) * cd[2] * (1.0 - cd[2]) } else { let d = if cd[2] <= 0.25 { ((16.0 * cd[2] - 12.0) * cd[2] + 4.0) * cd[2] } else { cd[2].sqrt() }; cd[2] + (2.0 * cs[2] - 1.0) * (d - cd[2]) },
        ],
        BlendMode::Difference => [(cd[0] - cs[0]).abs(), (cd[1] - cs[1]).abs(), (cd[2] - cs[2]).abs()],
        BlendMode::Exclusion => [cd[0] + cs[0] - 2.0 * cd[0] * cs[0], cd[1] + cs[1] - 2.0 * cd[1] * cs[1], cd[2] + cs[2] - 2.0 * cd[2] * cs[2]],
        // Non-separable (W3C): Cs = source, Cb = backdrop
        BlendMode::Hue => set_lum(set_sat(cs, sat_of(cd)), lum(cd)),
        BlendMode::Saturation => set_lum(set_sat(cd, sat_of(cs)), lum(cd)),
        BlendMode::Color => set_lum(cs, lum(cd)),
        BlendMode::Luminosity => set_lum(cd, lum(cs)),
    }
}

#[inline]
fn sat_of(c: [f32; 3]) -> f32 {
    c[0].max(c[1]).max(c[2]) - c[0].min(c[1]).min(c[2])
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn blend_basic() {
        assert_eq!(blend_color(BlendMode::Multiply, [0.5, 0.5, 0.5], [1.0, 1.0, 1.0]), [0.5, 0.5, 0.5]);
        let r = blend_color(BlendMode::Screen, [0.5, 0.0, 1.0], [0.5, 0.5, 0.0]);
        assert!((r[0] - 0.75).abs() < 1e-5 && (r[1] - 0.5).abs() < 1e-5 && (r[2] - 1.0).abs() < 1e-5);
        let r = blend_color(BlendMode::Hue, [1.0, 0.0, 0.0], [0.2, 0.2, 0.9]);
        assert!((r[0] - r[2]) > 0.0); // hue of source (red) applied, backdrop luminosity preserved-ish
    }
    #[test]
    fn hsl_roundtrip() {
        for c in [[0.9, 0.1, 0.3], [0.2, 0.8, 0.4], [0.1, 0.1, 0.1], [1.0, 1.0, 0.0]] {
            let h = rgb_to_hsl(c);
            let back = hsl_to_rgb(h);
            for i in 0..3 {
                assert!((c[i] - back[i]).abs() < 1e-4, "{c:?} -> {h:?} -> {back:?}");
            }
        }
    }
}
