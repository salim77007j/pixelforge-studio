//! Import/export: PNG/JPEG/WebP/GIF/BMP/TIFF via `image`, OpenRaster (layered), PSD (import), SVG.

use crate::blend::BlendMode;
use crate::doc::{Document, LayerNode};
use crate::geom::{resize_buffer, Resample};
use crate::pixel::{MaskBuffer, PixelBuffer};
use image::codecs::gif::GifEncoder;
use image::codecs::jpeg::JpegEncoder;
use image::codecs::webp::WebPEncoder;
use image::codecs::bmp::BmpEncoder;
use image::{AnimationDecoder, DynamicImage, ImageEncoder, ImageFormat};
use std::fs::File;
use std::io::{BufReader, BufWriter, Cursor, Read, Seek, Write};
use std::sync::Arc;

pub const FORMAT_NAMES: &[&str] = &["PNG", "JPEG", "WebP", "BMP", "TIFF", "GIF", "OpenRaster"];
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Format {
    Png = 0,
    Jpeg = 1,
    Webp = 2,
    Bmp = 3,
    Tiff = 4,
    Gif = 5,
    Ora = 6,
}

pub fn ext_to_format(ext: &str) -> Option<Format> {
    match ext.to_ascii_lowercase().as_str() {
        "png" => Some(Format::Png),
        "jpg" | "jpeg" => Some(Format::Jpeg),
        "webp" => Some(Format::Webp),
        "bmp" => Some(Format::Bmp),
        "tif" | "tiff" => Some(Format::Tiff),
        "gif" => Some(Format::Gif),
        "ora" => Some(Format::Ora),
        _ => None,
    }
}

pub fn is_layered(ext: &str) -> bool {
    matches!(ext_to_format(ext), Some(Format::Ora)) || ext.eq_ignore_ascii_case("psd")
}

// ---------------- Decoding ----------------

fn dyn_to_buffer(img: DynamicImage) -> PixelBuffer {
    let rgba = img.to_rgba8();
    PixelBuffer::from_raw(rgba.width(), rgba.height(), rgba.into_raw())
}

/// Open any supported single-layer image. The format is sniffed from the file
/// content (magic bytes), so mislabeled extensions still open correctly.
pub fn open_image(path: &str) -> Result<PixelBuffer, String> {
    let data = std::fs::read(path).map_err(|e| format!("cannot open file: {e}"))?;
    let mut reader = image::ImageReader::new(Cursor::new(&data));
    reader = reader.with_guessed_format().map_err(|e| format!("format detect failed: {e}"))?;
    let fmt = reader.format();
    if fmt.is_none() {
        return Err("unrecognized image format".into());
    }
    let img = reader.decode().map_err(|e| format!("decode failed: {e}"))?;
    Ok(dyn_to_buffer(img))
}

/// Open a GIF: all frames as separate buffers (first frame's canvas).
pub fn open_gif_frames(path: &str) -> Result<Vec<PixelBuffer>, String> {
    let data = std::fs::read(path).map_err(|e| e.to_string())?;
    let decoder = image::codecs::gif::GifDecoder::new(Cursor::new(&data)).map_err(|e| e.to_string())?;
    let mut out = Vec::new();
    for f in decoder.into_frames() {
        let f = f.map_err(|e| e.to_string())?;
        let buf = f.buffer();
        out.push(PixelBuffer::from_raw(buf.width(), buf.height(), buf.clone().into_raw()));
        if out.len() >= 256 { break; }
    }
    if out.is_empty() { return Err("GIF has no frames".into()); }
    Ok(out)
}

/// Render SVG at natural size (fallback 512).
pub fn open_svg(path: &str) -> Result<PixelBuffer, String> {
    let data = std::fs::read(path).map_err(|e| e.to_string())?;
    render_svg_data(&data)
}

pub fn render_svg_data(data: &[u8]) -> Result<PixelBuffer, String> {
    let opt = resvg::usvg::Options::default();
    let tree = resvg::usvg::Tree::from_data(data, &opt)
        .map_err(|e| format!("SVG parse: {e}"))?;
    let size = tree.size();
    let w = if size.width() > 0.5 { size.width().round() as u32 } else { 512 };
    let h = if size.height() > 0.5 { size.height().round() as u32 } else { 512 };
    let w = w.clamp(1, crate::pixel::MAX_DIM);
    let h = h.clamp(1, crate::pixel::MAX_DIM);
    let mut pixmap = resvg::tiny_skia::Pixmap::new(w, h).ok_or("pixmap alloc")?;
    resvg::render(&tree, resvg::tiny_skia::Transform::identity(), &mut pixmap.as_mut());
    Ok(PixelBuffer::from_raw(w, h, pixmap.take()))
}

/// Open PSD (layered import).
pub fn open_psd(path: &str) -> Result<Document, String> {
    let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
    let psd = psd::Psd::from_bytes(&bytes).map_err(|e| format!("PSD parse: {e}"))?;
    let w = psd.width();
    let h = psd.height();
    let mut root = LayerNode::new_group(1, "Root");
    // psd.layers() is bottom-to-top; our children are top-first, so reverse
    let mut layers = Vec::new();
    let mut id = 2u64;
    for layer in psd.layers() {
        let rgba = layer.rgba();
        let buf = PixelBuffer::from_raw(w, h, rgba);
        let mut node = LayerNode::new_raster(id, layer.name().to_string(), buf);
        node.opacity = layer.opacity() as f32 / 255.0;
        node.blend = psd_blend();
        node.visible = layer.visible();
        layers.push(node);
        id += 1;
    }
    layers.reverse();
    root.children = layers;
    let active = root.children.first().map(|l| l.id).unwrap_or(1);
    let mut doc = Document::empty_history_root(w, h, root, active);
    doc.history.push("Open PSD", doc.state());
    Ok(doc)
}

fn psd_blend() -> BlendMode {
    // NOTE: the `psd` crate does not publicly export its BlendMode enum,
    // so imported PSD layers default to Normal blending (documented limitation).
    BlendMode::Normal
}

/// Open OpenRaster (layered).
pub fn open_ora(path: &str) -> Result<Document, String> {
    let file = File::open(path).map_err(|e| e.to_string())?;
    let mut zip = zip::ZipArchive::new(BufReader::new(file)).map_err(|e| e.to_string())?;
    let stack_xml = read_zip_entry(&mut zip, "stack.xml").ok_or("stack.xml missing")?;
    let xml = String::from_utf8_lossy(&stack_xml).into_owned();
    let doc = roxmltree::Document::parse(&xml).map_err(|e| e.to_string())?;
    let root_el = doc.root_element();
    let w: u32 = root_el.attribute("w").and_then(|v| v.parse().ok()).unwrap_or(64);
    let h: u32 = root_el.attribute("h").and_then(|v| v.parse().ok()).unwrap_or(64);
    let mut next_id = 2u64;
    let mut root = LayerNode::new_group(1, "Root");
    let stack = root_el.children().filter(|n| n.is_element() && n.tag_name().name() == "stack").next();
    let stack = stack.ok_or("no <stack>")?;
    let children = parse_ora_stack(stack, &mut zip, &mut next_id, w, h)?;
    root.children = children; // first = top
    let active = root.children.first().map(|l| l.id).unwrap_or(1);
    let mut doc = Document::empty_history_root(w, h, root, active);
    doc.history.push("Open ORA", doc.state());
    Ok(doc)
}

fn parse_ora_stack<R: Read + Seek>(
    stack: roxmltree::Node,
    zip: &mut zip::ZipArchive<R>,
    next_id: &mut u64,
    w: u32, h: u32,
) -> Result<Vec<LayerNode>, String> {
    let mut children = Vec::new(); // in file order = bottom-first
    for node in stack.children().filter(|n| n.is_element()) {
        match node.tag_name().name() {
            "layer" => {
                let src = node.attribute("src").unwrap_or("").to_string();
                let name = node.attribute("name").unwrap_or("Layer").to_string();
                let opacity: f32 = node.attribute("opacity").and_then(|v| v.parse().ok()).unwrap_or(1.0);
                let visible = node.attribute("visibility").map(|v| v == "visible").unwrap_or(true);
                let x: i32 = node.attribute("x").and_then(|v| v.parse().ok()).unwrap_or(0);
                let y: i32 = node.attribute("y").and_then(|v| v.parse().ok()).unwrap_or(0);
                let blend = BlendMode::from_ora(node.attribute("composite-op").unwrap_or("svg:src-over"));
                let mask_src = node.attribute("mask").map(|s| s.to_string());
                let px = match read_zip_entry(zip, &src) {
                    Some(data) => {
                        let img = image::load_from_memory(&data).map_err(|e| format!("layer png: {e}"))?;
                        let rgba = img.to_rgba8();
                        PixelBuffer::from_raw(rgba.width(), rgba.height(), rgba.into_raw())
                    }
                    None => PixelBuffer::new(w, h, [0, 0, 0, 0])?,
                };
                let mask = mask_src
                    .and_then(|ms| read_zip_entry(zip, &ms))
                    .and_then(|data| image::load_from_memory(&data).ok())
                    .map(|img| {
                        let rgba = img.to_rgba8();
                        let (mw, mh) = (rgba.width(), rgba.height());
                        let raw = rgba.into_raw();
                        // mask stored in alpha channel
                        let mut m = MaskBuffer::new(mw, mh, 255).unwrap();
                        for i in 0..(mw * mh) as usize {
                            m.data[i] = raw[i * 4 + 3];
                        }
                        m
                    });
                let mut ln = LayerNode::new_raster(*next_id, name, px);
                *next_id += 1;
                ln.opacity = opacity.clamp(0.0, 1.0);
                ln.visible = visible;
                ln.x = x;
                ln.y = y;
                ln.blend = blend;
                ln.mask = mask.map(Arc::new);
                children.push(ln);
            }
            "stack" => {
                let name = node.attribute("name").unwrap_or("Group").to_string();
                let opacity: f32 = node.attribute("opacity").and_then(|v| v.parse().ok()).unwrap_or(1.0);
                let mut g = LayerNode::new_group(*next_id, name);
                *next_id += 1;
                g.opacity = opacity.clamp(0.0, 1.0);
                let kids = parse_ora_stack(node, zip, next_id, w, h)?;
                // file order bottom-first; children top-first
                let mut kids = kids;
                kids.reverse();
                g.children = kids;
                children.push(g);
            }
            _ => {}
        }
    }
    // reverse to top-first
    children.reverse();
    Ok(children)
}

fn read_zip_entry<R: Read + Seek>(zip: &mut zip::ZipArchive<R>, name: &str) -> Option<Vec<u8>> {
    let mut f = zip.by_name(name).ok()?;
    let mut buf = Vec::new();
    f.read_to_end(&mut buf).ok()?;
    Some(buf)
}

// ---------------- Encoding ----------------

/// Flatten composite to DynamicImage (optional background fill for alpha-less formats, optional scale).
pub fn composite_to_image(
    rgba: Vec<u8>, w: u32, h: u32,
    bg: Option<[u8; 3]>,
    scale_pct: u32,
) -> Result<DynamicImage, String> {
    let buf = if scale_pct != 0 && scale_pct != 100 {
        let nw = ((w as u64 * scale_pct as u64 / 100).max(1) as u32).min(crate::pixel::MAX_DIM);
        let nh = ((h as u64 * scale_pct as u64 / 100).max(1) as u32).min(crate::pixel::MAX_DIM);
        resize_buffer(&PixelBuffer::from_raw(w, h, rgba), nw, nh, Resample::Bicubic)?
    } else {
        PixelBuffer::from_raw(w, h, rgba)
    };
    let mut data = buf.data;
    if let Some(bg) = bg {
        for px in data.chunks_exact_mut(4) {
            let a = px[3] as f32 / 255.0;
            px[0] = crate::pixel::clamp_u8(px[0] as f32 * a + bg[0] as f32 * (1.0 - a));
            px[1] = crate::pixel::clamp_u8(px[1] as f32 * a + bg[1] as f32 * (1.0 - a));
            px[2] = crate::pixel::clamp_u8(px[2] as f32 * a + bg[2] as f32 * (1.0 - a));
            px[3] = 255;
        }
    }
    let img = image::RgbaImage::from_raw(buf.w, buf.h, data).ok_or("image build")?;
    Ok(DynamicImage::ImageRgba8(img))
}

pub fn export_image(
    path: &str,
    format: Format,
    rgba: Vec<u8>, w: u32, h: u32,
    quality: u8,
    lossless: bool,
    bg: Option<[u8; 3]>,
    scale_pct: u32,
) -> Result<(), String> {
    let needs_bg = matches!(format, Format::Jpeg | Format::Bmp);
    let bg = if needs_bg { Some(bg.unwrap_or([255, 255, 255])) } else { bg };
    let img = composite_to_image(rgba, w, h, bg, scale_pct)?;
    let file = BufWriter::new(File::create(path).map_err(|e| e.to_string())?);
    match format {
        Format::Png => {
            img.write_to(&mut BufWriter::new(File::create(path).map_err(|e| e.to_string())?), ImageFormat::Png)
                .map_err(|e| e.to_string())
        }
        Format::Jpeg => {
            let rgb = img.to_rgb8();
            let enc = JpegEncoder::new_with_quality(file, quality);
            enc.write_image(rgb.as_raw(), rgb.width(), rgb.height(), image::ExtendedColorType::Rgb8)
                .map_err(|e| e.to_string())
        }
        Format::Webp => {
            // image-webp 0.2 encodes VP8L lossless (quality param reserved for future lossy path)
            let _ = lossless;
            let _ = quality;
            let enc = WebPEncoder::new_lossless(file);
            enc.write_image(img.as_bytes(), img.width(), img.height(), image::ExtendedColorType::Rgba8)
                .map_err(|e| e.to_string())
        }
        Format::Bmp => {
            let rgb = img.to_rgb8();
            let mut b = BufWriter::new(File::create(path).map_err(|e| e.to_string())?);
            let enc = BmpEncoder::new(&mut b);
            enc.write_image(rgb.as_raw(), rgb.width(), rgb.height(), image::ExtendedColorType::Rgb8)
                .map_err(|e| e.to_string())
        }
        Format::Tiff => {
            img.write_to(&mut BufWriter::new(File::create(path).map_err(|e| e.to_string())?), ImageFormat::Tiff)
                .map_err(|e| e.to_string())
        }
        Format::Gif => {
            let rgba8 = img.to_rgba8();
            let mut b = BufWriter::new(File::create(path).map_err(|e| e.to_string())?);
            let mut enc = GifEncoder::new(&mut b);
            let rgb = image::RgbaImage::from_fn(rgba8.width(), rgba8.height(), |x, y| {
                let p = rgba8.get_pixel(x, y);
                if p[3] > 127 { *p } else { image::Rgba([p[0], p[1], p[2], 255]) }
            });
            enc.write_image(rgb.as_raw(), rgb.width(), rgb.height(), image::ExtendedColorType::Rgba8)
                .map_err(|e| e.to_string())
        }
        Format::Ora => Err("use save_ora for OpenRaster".into()),
    }
}

fn xml_escape(s: &str) -> String {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}

/// Save document as OpenRaster (layers, groups, masks, blend modes, opacity).
pub fn save_ora(doc: &Document, path: &str) -> Result<(), String> {
    let file = File::create(path).map_err(|e| e.to_string())?;
    let mut zip = zip::ZipWriter::new(BufWriter::new(file));
    let opts: zip::write::SimpleFileOptions = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Stored);
    zip.start_file("mimetype", opts).map_err(|e| e.to_string())?;
    zip.write_all(b"image/openraster").map_err(|e| e.to_string())?;

    let mut xml = String::new();
    xml.push_str("<?xml version='1.0' encoding='UTF-8'?>\n");
    xml.push_str(&format!("<image version=\"0.0.3\" w=\"{}\" h=\"{}\">\n", doc.w, doc.h));
    xml.push_str("  <stack>\n");

    fn write_stack(node: &LayerNode, xml: &mut String, zip: &mut zip::ZipWriter<BufWriter<File>>, counter: &mut u32, depth: usize) -> Result<(), String> {
        let indent = "  ".repeat(depth + 2);
        let deflate = zip::write::SimpleFileOptions::default()
            .compression_method(zip::CompressionMethod::Deflated);
        // children are stored top-first in memory; ORA is bottom-first => iterate reversed
        for child in node.children.iter().rev() {
            match child.kind {
                crate::doc::LayerKind::Group => {
                    xml.push_str(&format!(
                        "{indent}<stack name=\"{}\" opacity=\"{:.4}\"{}>\n",
                        xml_escape(&child.name),
                        child.opacity,
                        if child.visible { "" } else { " visibility=\"hidden\"" }
                    ));
                    write_stack(child, xml, zip, counter, depth + 1)?;
                    xml.push_str(&format!("{indent}</stack>\n"));
                }
                crate::doc::LayerKind::Raster => {
                    let idx = *counter;
                    *counter += 1;
                    let src = format!("data/layer{idx:03}.png");
                    let px = child.pixels.as_ref().map(|p| p.as_ref());
                    if let Some(p) = px {
                        let img = image::RgbaImage::from_raw(p.w, p.h, p.data.clone())
                            .ok_or("layer image")?;
                        let mut png = Vec::new();
                        img.write_to(&mut Cursor::new(&mut png), ImageFormat::Png).map_err(|e| e.to_string())?;
                        zip.start_file(src.clone(), deflate).map_err(|e| e.to_string())?;
                        zip.write_all(&png).map_err(|e| e.to_string())?;
                        // mask
                        let mask_attr = if let Some(m) = &child.mask {
                            let msrc = format!("data/mask{idx:03}.png");
                            // mask in alpha channel, RGB white
                            let mut mdata = vec![255u8; (m.w * m.h * 4) as usize];
                            for i in 0..(m.w * m.h) as usize {
                                mdata[i * 4 + 3] = m.data[i];
                            }
                            let img = image::RgbaImage::from_raw(m.w, m.h, mdata).ok_or("mask image")?;
                            let mut png = Vec::new();
                            img.write_to(&mut Cursor::new(&mut png), ImageFormat::Png).map_err(|e| e.to_string())?;
                            zip.start_file(msrc.clone(), deflate).map_err(|e| e.to_string())?;
                            zip.write_all(&png).map_err(|e| e.to_string())?;
                            format!(" mask=\"{msrc}\"")
                        } else {
                            String::new()
                        };
                        xml.push_str(&format!(
                            "{indent}<layer src=\"{src}\" name=\"{}\" x=\"{}\" y=\"{}\" opacity=\"{:.4}\" composite-op=\"{}\"{}{}/>\n",
                            xml_escape(&child.name),
                            child.x, child.y,
                            child.opacity,
                            child.blend.to_ora(),
                            if child.visible { "" } else { " visibility=\"hidden\"" },
                            mask_attr,
                        ));
                    }
                }
            }
        }
        Ok(())
    }

    let mut counter = 0u32;
    write_stack(&doc.root, &mut xml, &mut zip, &mut counter, 0)?;
    xml.push_str("  </stack>\n");
    xml.push_str("</image>\n");

    zip.start_file("stack.xml", opts).map_err(|e| e.to_string())?;
    zip.write_all(xml.as_bytes()).map_err(|e| e.to_string())?;

    // merged image + thumbnail
    let mut comp = vec![0u8; (doc.w * doc.h * 4) as usize];
    doc.composite(crate::doc::Rect { x: 0, y: 0, w: doc.w, h: doc.h }, &mut comp);
    let img = image::RgbaImage::from_raw(doc.w, doc.h, comp).ok_or("merged")?;
    let deflate = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated);
    let mut png = Vec::new();
    img.write_to(&mut Cursor::new(&mut png), ImageFormat::Png).map_err(|e| e.to_string())?;
    zip.start_file("mergedimage.png", deflate).map_err(|e| e.to_string())?;
    zip.write_all(&png).map_err(|e| e.to_string())?;

    // thumbnail 256
    let (tw, th, tdata) = {
        let pb = PixelBuffer::from_raw(doc.w, doc.h, img.clone().into_raw());
        pb.thumbnail(256)
    };
    let timg = image::RgbaImage::from_raw(tw, th, tdata).ok_or("thumb")?;
    let mut tpng = Vec::new();
    timg.write_to(&mut Cursor::new(&mut tpng), ImageFormat::Png).map_err(|e| e.to_string())?;
    zip.start_file("Thumbnails/thumbnail.png", deflate).map_err(|e| e.to_string())?;
    zip.write_all(&tpng).map_err(|e| e.to_string())?;

    zip.finish().map_err(|e| e.to_string())?;
    Ok(())
}
