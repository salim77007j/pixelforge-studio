//! C ABI surface (pf_* symbols) consumed by the Qt 6 C++ UI.
//! Every entry point is panic-guarded (catch_unwind) — a Rust panic never crosses the FFI boundary.

mod adjust;
mod blend;
mod brush;
mod doc;
mod filters;
mod geom;
mod io;
mod pixel;
mod select;

use doc::{Document, LayerKind, LayerNode, Rect};
use pixel::{MaskBuffer, PixelBuffer};
use std::ffi::{c_char, CStr, CString};
use std::sync::atomic::{AtomicPtr, Ordering};
use std::sync::Arc;

static LAST_ERROR: AtomicPtr<c_char> = AtomicPtr::new(std::ptr::null_mut());

fn set_error(msg: &str) {
    let c = CString::new(msg.replace('\0', " ")).unwrap_or_else(|_| CString::new("error").unwrap());
    let ptr = c.into_raw();
    let old = LAST_ERROR.swap(ptr, Ordering::SeqCst);
    if !old.is_null() {
        unsafe { drop(CString::from_raw(old)); }
    }
}

#[no_mangle]
pub extern "C" fn pf_last_error() -> *const c_char {
    let p = LAST_ERROR.load(Ordering::SeqCst);
    if p.is_null() {
        static EMPTY: &[u8] = b"\0";
        EMPTY.as_ptr() as *const c_char
    } else {
        p
    }
}

macro_rules! guard {
    ($body:expr) => {
        match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| -> Result<*mut std::ffi::c_void, String> {
            let __ret = { $body };
            Ok(__ret as *mut std::ffi::c_void)
        })) {
            Ok(Ok(v)) => v as _,
            Ok(Err(e)) => { set_error(&e); std::ptr::null_mut() },
            Err(_) => { set_error("engine panic (operation aborted)"); std::ptr::null_mut() },
        }
    };
}

macro_rules! guard_int {
    ($body:expr) => {
        match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| -> Result<i32, String> {
            let __ret = { $body };
            Ok(__ret)
        })) {
            Ok(Ok(v)) => v,
            Ok(Err(e)) => { set_error(&e); -1 },
            Err(_) => { set_error("engine panic (operation aborted)"); -1 },
        }
    };
}

macro_rules! guard_id {
    ($body:expr) => {
        match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| -> Result<u64, String> {
            let __ret = { $body };
            Ok(__ret)
        })) {
            Ok(Ok(v)) => v,
            Ok(Err(e)) => { set_error(&e); 0 },
            Err(_) => { set_error("engine panic (operation aborted)"); 0 },
        }
    };
}

unsafe fn cstr<'a>(p: *const c_char) -> Result<&'a str, String> {
    if p.is_null() { return Err("null string".into()); }
    CStr::from_ptr(p).to_str().map_err(|_| "invalid utf8".to_string())
}

// ============================ Document ============================

/// opaque handle
pub type PFDoc = *mut Document;

#[no_mangle]
pub extern "C" fn pf_version() -> i32 {
    1
}

/// fill: 0 transparent, 1 white, 2 black, 3 custom rgb
#[no_mangle]
pub extern "C" fn pf_document_new(w: u32, h: u32, fill: i32, r: u8, g: u8, b: u8) -> PFDoc {
    guard!({
        let bg = match fill {
            1 => [255, 255, 255, 255],
            2 => [0, 0, 0, 255],
            3 => [r, g, b, 255],
            _ => [0, 0, 0, 0],
        };
        match Document::new(w, h, bg) {
            Ok(mut d) => {
                d.history.states.clear();
                d.history.states.push(("New Document".into(), d.state()));
                d.history.index = 0;
                Box::into_raw(Box::new(d))
            }
            Err(e) => { set_error(&e); std::ptr::null_mut() }
        }
    })
}

#[no_mangle]
pub extern "C" fn pf_document_open(path: *const c_char) -> PFDoc {
    guard!({
        let p = unsafe { cstr(path)? };
        let ext = p.rsplit('.').next().unwrap_or("").to_string();
        let doc = match io::ext_to_format(&ext) {
            Some(io::Format::Ora) => io::open_ora(p),
            _ => {
                if ext.eq_ignore_ascii_case("psd") {
                    io::open_psd(p)
                } else if ext.eq_ignore_ascii_case("svg") {
                    match io::open_svg(p) {
                        Ok(buf) => {
                            let (w, h) = (buf.w, buf.h);
                            let mut root = LayerNode::new_group(1, "Root");
                            root.children.push(LayerNode::new_raster(2, "SVG", buf));
                            Ok(Document::empty_history_root(w, h, root, 2))
                        }
                        Err(e) => Err(e),
                    }
                } else if ext.eq_ignore_ascii_case("gif") {
                    match io::open_gif_frames(p) {
                        Ok(frames) => {
                            let (w, h) = (frames[0].w, frames[0].h);
                            let mut root = LayerNode::new_group(1, "Root");
                            let mut id = 2;
                            for (i, f) in frames.into_iter().enumerate() {
                                root.children.push(LayerNode::new_raster(id, format!("Frame {}", i + 1), f));
                                id += 1;
                            }
                            Ok(Document::empty_history_root(w, h, root, 2))
                        }
                        Err(e) => Err(e),
                    }
                } else {
                    match io::open_image(p) {
                        Ok(buf) => {
                            let (w, h) = (buf.w, buf.h);
                            let mut root = LayerNode::new_group(1, "Root");
                            root.children.push(LayerNode::new_raster(2, "Background", buf));
                            Ok(Document::empty_history_root(w, h, root, 2))
                        }
                        Err(e) => Err(e),
                    }
                }
            }
        };
        match doc {
            Ok(mut d) => {
                d.path = Some(p.to_string());
                d.modified = false;
                Box::into_raw(Box::new(d))
            }
            Err(e) => { set_error(&e); std::ptr::null_mut() }
        }
    })
}

#[no_mangle]
pub extern "C" fn pf_document_close(doc: PFDoc) {
    if !doc.is_null() {
        unsafe { drop(Box::from_raw(doc)); }
    }
}

#[no_mangle]
pub extern "C" fn pf_document_size(doc: PFDoc, w: *mut u32, h: *mut u32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        unsafe { *w = d.w; *h = d.h; }
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_document_modified(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        d.modified as i32
    })
}

#[no_mangle]
pub extern "C" fn pf_document_mark_saved(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        d.modified = false;
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_document_path(doc: PFDoc) -> *const c_char {
    guard!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        match &d.path {
            Some(p) => {
                let c = CString::new(p.clone()).unwrap();
                c.into_raw()
            }
            None => std::ptr::null_mut(),
        }
    })
}

#[no_mangle]
pub extern "C" fn pf_free_str(p: *mut c_char) {
    if !p.is_null() {
        unsafe { drop(CString::from_raw(p)); }
    }
}

/// Composite a region (doc coords) into out (region.w*region.h*4 bytes, RGBA8888).
#[no_mangle]
pub extern "C" fn pf_document_composite(doc: PFDoc, x: i32, y: i32, w: u32, h: u32, out: *mut u8, out_len: usize) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        let out = unsafe { std::slice::from_raw_parts_mut(out, out_len) };
        let region = Rect { x, y, w, h };
        d.composite(region, out);
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_document_memory(doc: PFDoc) -> u64 {
    guard_id!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        let mut n = d.root.memory_bytes();
        for (label, s) in &d.history.states {
            n += s.root.memory_bytes();
            let _ = label;
        }
        n
    })
}

// ============================ Layers ============================

#[no_mangle]
pub extern "C" fn pf_layers_json(doc: PFDoc) -> *const c_char {
    guard!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let j = d.layer_json();
        // leak a stable copy valid until next call
        let c = CString::new(j).unwrap();
        c.into_raw() as *const c_char
    })
}

/// kind: 0 raster, 1 group
#[no_mangle]
pub extern "C" fn pf_layer_add(doc: PFDoc, kind: i32, name: *const c_char) -> u64 {
    guard_id!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let name = unsafe { cstr(name)? }.to_string();
        let id = d.alloc_id();
        let node = if kind == 1 {
            LayerNode::new_group(id, name)
        } else {
            LayerNode::new_raster(id, name, PixelBuffer::new(d.w, d.h, [0, 0, 0, 0])?)
        };
        d.root.children.insert(0, node);
        d.active = id;
        d.push_history(if kind == 1 { "New Group" } else { "New Layer" });
        id
    })
}

/// New raster layer from an RGBA buffer (Qt-rendered text, pasted clipboard, etc.)
#[no_mangle]
pub extern "C" fn pf_layer_add_pixels(doc: PFDoc, w: u32, h: u32, data: *const u8, len: usize, name: *const c_char, x: i32, y: i32) -> u64 {
    guard_id!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        if len < (w as usize) * (h as usize) * 4 { return Err("buffer too small".into()); }
        let data = unsafe { std::slice::from_raw_parts(data, (w as usize) * (h as usize) * 4) };
        let mut px = PixelBuffer::new(w, h, [0, 0, 0, 0])?;
        px.data.copy_from_slice(data);
        let name = unsafe { cstr(name)? }.to_string();
        let id = d.alloc_id();
        let mut node = LayerNode::new_raster(id, name, px);
        node.x = x;
        node.y = y;
        d.root.children.insert(0, node);
        d.active = id;
        d.push_history("New Layer");
        id
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_remove(doc: PFDoc, id: u64) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        d.root.remove(id).ok_or("layer not found")?;
        if d.active == id {
            d.active = d.root.children.first().map(|l| l.id).unwrap_or(1);
        }
        d.push_history("Delete Layer");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_duplicate(doc: PFDoc, id: u64) -> u64 {
    guard_id!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let src = d.root.find(id).ok_or("layer not found")?.clone();
        let new_id = d.alloc_id();
        let mut dup = src;
        dup.id = new_id;
        dup.name = format!("{} copy", dup.name);
        // insert above original
        fn insert_after(root: &mut LayerNode, target: u64, node: LayerNode) -> bool {
            for i in 0..root.children.len() {
                if root.children[i].id == target {
                    root.children.insert(i, node);
                    return true;
                }
            }
            for c in root.children.iter_mut() {
                if insert_after(c, target, node.clone()) { return true; }
            }
            false
        }
        insert_after(&mut d.root, id, dup);
        d.active = new_id;
        d.push_history("Duplicate Layer");
        new_id
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_set_name(doc: PFDoc, id: u64, name: *const c_char) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let name = unsafe { cstr(name)? }.to_string();
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        l.name = name;
        d.cached_json = None;
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_set_visible(doc: PFDoc, id: u64, v: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        l.visible = v != 0;
        d.cached_json = None;
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_set_locked(doc: PFDoc, id: u64, v: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        l.locked = v != 0;
        d.cached_json = None;
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_set_opacity(doc: PFDoc, id: u64, opacity: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        l.opacity = opacity.clamp(0.0, 1.0);
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_set_blend(doc: PFDoc, id: u64, blend: *const c_char) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let name = unsafe { cstr(blend)? };
        let b = blend::BlendMode::from_name(name);
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        l.blend = b;
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_set_offset(doc: PFDoc, id: u64, x: i32, y: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        l.x = x;
        l.y = y;
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_offset(doc: PFDoc, id: u64, x: *mut i32, y: *mut i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        let l = d.root.find(id).ok_or("layer not found")?;
        unsafe { *x = l.x; *y = l.y; }
        0
    })
}

/// Move a layer into target_parent (0 = root) at index (0 = top).
#[no_mangle]
pub extern "C" fn pf_layer_move(doc: PFDoc, id: u64, target_parent: u64, index: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let node = d.root.remove(id).ok_or("layer not found")?;
        let parent_id = if target_parent == 0 { 1 } else { target_parent };
        let parent = d.root.find_mut(parent_id).ok_or("target not found")?;
        if parent.kind != LayerKind::Group { return Err("target is not a group".into()); }
        let idx = index.clamp(0, parent.children.len() as i32) as usize;
        parent.children.insert(idx, node);
        d.push_history("Reorder Layer");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_merge_down(doc: PFDoc, id: u64) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let (w, h) = (d.w, d.h);
        fn merge_in(node: &mut LayerNode, id: u64, doc_w: u32, doc_h: u32) -> Result<bool, String> {
            for i in 0..node.children.len() {
                if node.children[i].id == id {
                    if i + 1 >= node.children.len() { return Err("no layer below".into()); }
                    let below_idx = i + 1;
                    if node.children[below_idx].kind != LayerKind::Raster {
                        return Err("layer below is a group".into());
                    }
                    // gather top-layer data (Arc clones, cheap)
                    let top = node.children[i].pixels.as_ref().ok_or("no pixels")?.clone();
                    let (tx, ty) = (node.children[i].x, node.children[i].y);
                    let (top_op, top_blend) = (node.children[i].opacity, node.children[i].blend);
                    let top_mask = node.children[i].mask.clone();
                    // gather below-layer data
                    let below_arc = node.children[below_idx].pixels.as_ref().ok_or("no pixels")?.clone();
                    let (bx, by) = (node.children[below_idx].x, node.children[below_idx].y);
                    // normalize below into doc-space buffer
                    let mut base = PixelBuffer::new(doc_w, doc_h, [0, 0, 0, 0])?;
                    for y in 0..below_arc.h {
                        for x in 0..below_arc.w {
                            let dx = bx + x as i32;
                            let dy = by + y as i32;
                            if dx >= 0 && dy >= 0 && dx < doc_w as i32 && dy < doc_h as i32 {
                                base.set(dx as u32, dy as u32, below_arc.get(x, y));
                            }
                        }
                    }
                    // blend top onto base
                    crate::doc::blend_pub(&mut base.data, doc_w, &Rect { x: 0, y: 0, w: doc_w, h: doc_h },
                        &top, tx, ty, top_op, top_blend, top_mask.as_deref());
                    // write back merged result
                    let below = &mut node.children[below_idx];
                    below.pixels = Some(Arc::new(base));
                    below.x = 0;
                    below.y = 0;
                    below.opacity = 1.0;
                    node.children.remove(i);
                    return Ok(true);
                }
            }
            for c in node.children.iter_mut() {
                if merge_in(c, id, doc_w, doc_h)? { return Ok(true); }
            }
            Ok(false)
        }
        let done = merge_in(&mut d.root, id, w, h)?;
        if !done { return Err("layer not found".into()); }
        if d.active == id { d.active = d.root.children.first().map(|l| l.id).unwrap_or(1); }
        d.push_history("Merge Down");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_flatten(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let mut comp = vec![0u8; (d.w * d.h * 4) as usize];
        let region = Rect { x: 0, y: 0, w: d.w, h: d.h };
        d.composite(region, &mut comp);
        let buf = PixelBuffer::from_raw(d.w, d.h, comp);
        let id = d.alloc_id();
        let mut root = LayerNode::new_group(1, "Root");
        root.children.push(LayerNode::new_raster(id, "Background", buf));
        d.root = root;
        d.active = id;
        d.push_history("Flatten Image");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_set_active(doc: PFDoc, id: u64) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        d.root.find(id).ok_or("layer not found")?;
        d.active = id;
        d.cached_json = None;
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_active(doc: PFDoc) -> u64 {
    guard_id!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        d.active
    })
}

/// Copy layer pixels (full buffer) into out.
#[no_mangle]
pub extern "C" fn pf_layer_pixels_get(doc: PFDoc, id: u64, out: *mut u8, out_len: usize) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        let l = d.root.find(id).ok_or("layer not found")?;
        let px = l.pixels.as_ref().ok_or("no pixels")?;
        if out_len < px.data.len() { return Err("output buffer too small".into()); }
        unsafe { std::ptr::copy_nonoverlapping(px.data.as_ptr(), out, px.data.len()); }
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_pixels_size(doc: PFDoc, id: u64, w: *mut u32, h: *mut u32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        let l = d.root.find(id).ok_or("layer not found")?;
        let px = l.pixels.as_ref().ok_or("no pixels")?;
        unsafe { *w = px.w; *h = px.h; }
        0
    })
}

/// Write layer pixels from a buffer (same size as existing).
#[no_mangle]
pub extern "C" fn pf_layer_pixels_set(doc: PFDoc, id: u64, data: *const u8, len: usize) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        let px = l.pixels.as_ref().ok_or("no pixels")?;
        if len != px.data.len() { return Err("size mismatch".into()); }
        let mut newpx = (**px).clone();
        unsafe { std::ptr::copy_nonoverlapping(data, newpx.data.as_mut_ptr(), len); }
        let l = d.root.find_mut(id).unwrap();
        l.pixels = Some(Arc::new(newpx));
        0
    })
}

/// Thumbnail RGBA into out (max_px*max_px*4), returns actual w/h.
#[no_mangle]
pub extern "C" fn pf_layer_thumbnail(doc: PFDoc, id: u64, max_px: u32, out: *mut u8, out_len: usize, out_w: *mut u32, out_h: *mut u32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        let l = d.root.find(id).ok_or("layer not found")?;
        match &l.pixels {
            Some(px) => {
                let (tw, th, data) = px.thumbnail(max_px);
                if out_len < data.len() { return Err("thumbnail buffer too small".into()); }
                unsafe {
                    std::ptr::copy_nonoverlapping(data.as_ptr(), out, data.len());
                    *out_w = tw;
                    *out_h = th;
                }
                0
            }
            None => {
                // group: composite whole doc as approximation of group content
                let mut tmp = vec![0u8; (d.w * d.h * 4) as usize];
                let region = Rect { x: 0, y: 0, w: d.w, h: d.h };
                d.composite(region, &mut tmp);
                let pb = PixelBuffer::from_raw(d.w, d.h, tmp);
                let (tw, th, data) = pb.thumbnail(max_px);
                if out_len < data.len() { return Err("thumbnail buffer too small".into()); }
                unsafe {
                    std::ptr::copy_nonoverlapping(data.as_ptr(), out, data.len());
                    *out_w = tw;
                    *out_h = th;
                }
                0
            }
        }
    })
}

// ---------------- Masks ----------------

/// from_selection: 1 = initialize from current selection
#[no_mangle]
pub extern "C" fn pf_layer_mask_add(doc: PFDoc, id: u64, from_selection: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let (w, h) = (d.w, d.h);
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        if l.kind != LayerKind::Raster { return Err("groups cannot have masks".into()); }
        let mask = if from_selection != 0 {
            match &d.selection {
                Some(s) => (**s).clone(),
                None => MaskBuffer::new(w, h, 255)?,
            }
        } else {
            MaskBuffer::new(w, h, 255)?
        };
        l.mask = Some(Arc::new(mask));
        l.mask_enabled = true;
        d.push_history("Add Layer Mask");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_mask_remove(doc: PFDoc, id: u64) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        l.mask = None;
        d.push_history("Remove Layer Mask");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_mask_set_enabled(doc: PFDoc, id: u64, enabled: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        l.mask_enabled = enabled != 0;
        d.cached_json = None;
        0
    })
}

/// Paint into the mask with a soft round brush dab (used by UI mask painting via brush).
#[no_mangle]
pub extern "C" fn pf_layer_mask_paint(doc: PFDoc, id: u64, x: f32, y: f32, radius: f32, value: u8) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        let mask = l.mask.as_mut().ok_or("no mask")?;
        let mask = Arc::get_mut(mask).ok_or("mask busy")?;
        let lx = x - l.x as f32;
        let ly = y - l.y as f32;
        let x0 = (lx - radius).floor().max(0.0) as i32;
        let y0 = (ly - radius).floor().max(0.0) as i32;
        let x1 = (lx + radius).ceil() as i32;
        let y1 = (ly + radius).ceil() as i32;
        for py in y0..y1 {
            for px in x0..x1 {
                if px < 0 || py < 0 || px >= mask.w as i32 || py >= mask.h as i32 { continue; }
                let dx = px as f32 + 0.5 - lx;
                let dy = py as f32 + 0.5 - ly;
                let d = (dx * dx + dy * dy).sqrt() / radius.max(0.1);
                if d < 1.0 {
                    let cov = (1.0 - d).min(1.0);
                    let cur = mask.get(px as u32, py as u32) as f32;
                    let target = if value > cur as u8 { value as f32 } else { cur };
                    let v = cur * (1.0 - cov) + target * cov;
                    mask.set(px as u32, py as u32, v.round().clamp(0.0, 255.0) as u8);
                }
            }
        }
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_fill(doc: PFDoc, id: u64, r: u8, g: u8, b: u8, a: u8) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let prev_active = d.active;
        d.active = id;
        let res = geom::fill_selection(d, [r, g, b, a]);
        d.active = prev_active;
        res?;
        0
    })
}

// ============================ Stroke (brush engine) ============================

/// tool: 0 brush, 1 pencil, 2 eraser
#[no_mangle]
pub extern "C" fn pf_stroke_begin(
    doc: PFDoc, tool: i32, size: f32, hardness: f32, opacity: f32, flow: f32, spacing: f32,
    r: u8, g: u8, b: u8, a: u8, pressure_size: i32, pressure_opacity: i32,
    x: f32, y: f32, pressure: f32,
) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let params = brush::BrushParams {
            tool: match tool {
                1 => brush::BrushTool::Pencil,
                2 => brush::BrushTool::Eraser,
                _ => brush::BrushTool::Brush,
            },
            size: size.max(1.0),
            hardness: hardness.clamp(0.0, 1.0),
            opacity: opacity.clamp(0.01, 1.0),
            flow: flow.clamp(0.01, 1.0),
            spacing: spacing.clamp(0.02, 4.0),
            color: [r, g, b, a],
            pressure_size: pressure_size != 0,
            pressure_opacity: pressure_opacity != 0,
        };
        let mut s = brush::StrokeSession::new(d, params)?;
        s.stroke_to(d, x, y, pressure);
        d.stroke = Some(s);
        0
    })
}

/// Returns dirty rect via out ints (doc coords). w=0 means no dirty.
#[no_mangle]
pub extern "C" fn pf_stroke_move(doc: PFDoc, x: f32, y: f32, pressure: f32, dx_out: *mut i32, dy_out: *mut i32, dw_out: *mut u32, dh_out: *mut u32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let mut s = d.stroke.take().ok_or("no active stroke")?;
        s.stroke_to(d, x, y, pressure);
        let (x0, y0, x1, y1) = s.dirty;
        d.stroke = Some(s);
        unsafe {
            if x0 <= x1 {
                *dx_out = x0.max(0);
                *dy_out = y0.max(0);
                *dw_out = (x1 - x0.max(0)).max(0) as u32;
                *dh_out = (y1 - y0.max(0)).max(0) as u32;
            } else {
                *dw_out = 0;
                *dh_out = 0;
            }
        }
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_stroke_end(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let s = d.stroke.take().ok_or("no active stroke")?;
        let label = match s.params.tool {
            brush::BrushTool::Pencil => "Pencil Stroke",
            brush::BrushTool::Eraser => "Eraser",
            brush::BrushTool::Brush => "Brush Stroke",
        };
        let _ = &s.dirty;
        s.commit(d);
        d.push_history(label);
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_stroke_cancel(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        d.stroke = None;
        0
    })
}

// ============================ Fills / shapes / gradients ============================

#[no_mangle]
pub extern "C" fn pf_flood_fill(doc: PFDoc, x: i32, y: i32, r: u8, g: u8, b: u8, a: u8, tolerance: u8, contiguous: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        geom::flood_fill(d, x, y, [r, g, b, a], tolerance, contiguous != 0)?;
        d.push_history("Bucket Fill");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_fill_selection(doc: PFDoc, r: u8, g: u8, b: u8, a: u8) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        geom::fill_selection(d, [r, g, b, a])?;
        d.push_history("Fill");
        0
    })
}

/// kind: 0 linear, 1 radial
#[no_mangle]
pub extern "C" fn pf_gradient_fill(
    doc: PFDoc, kind: i32, x0: f64, y0: f64, x1: f64, y1: f64,
    r0: u8, g0: u8, b0: u8, a0: u8, r1: u8, g1: u8, b1: u8, a1: u8, dither: i32,
) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let k = if kind == 1 { geom::GradientKind::Radial } else { geom::GradientKind::Linear };
        geom::gradient_fill(d, k, x0, y0, x1, y1, [r0, g0, b0, a0], [r1, g1, b1, a1], dither != 0)?;
        d.push_history("Gradient");
        0
    })
}

/// kind: 0 line, 1 rect, 2 ellipse, 3 polygon. stroke_a==0 -> no stroke; fill_a==0 -> no fill.
#[no_mangle]
pub extern "C" fn pf_shape_draw(
    doc: PFDoc, kind: i32, pts: *const f64, npts: i32,
    sr: u8, sg: u8, sb: u8, sa: u8, stroke_w: f32,
    fr: u8, fg: u8, fb: u8, fa: u8,
) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        if npts < 2 { return Err("not enough points".into()); }
        let pts = unsafe { std::slice::from_raw_parts(pts, npts as usize * 2) };
        let k = match kind {
            1 => geom::ShapeKind::Rect,
            2 => geom::ShapeKind::Ellipse,
            3 => geom::ShapeKind::Polygon,
            _ => geom::ShapeKind::Line,
        };
        let stroke = if sa == 0 { None } else { Some(([sr, sg, sb, sa], stroke_w.max(0.5))) };
        let fill = if fa == 0 { None } else { Some([fr, fg, fb, fa]) };
        geom::draw_shape(d, k, pts, stroke, fill)?;
        d.push_history("Shape");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_pick_color(doc: PFDoc, x: i32, y: i32, sample_composite: i32, r: *mut u8, g: *mut u8, b: *mut u8, a: *mut u8) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        if x < 0 || y < 0 || x >= d.w as i32 || y >= d.h as i32 { return Err("outside canvas".into()); }
        let px = if sample_composite != 0 {
            let mut buf = [0u8; 4];
            let region = Rect { x, y, w: 1, h: 1 };
            let mut tmp = vec![0u8; 4];
            d.composite(region, &mut tmp);
            buf.copy_from_slice(&tmp);
            buf
        } else {
            let l = d.active_layer().ok_or("no active layer")?;
            let p = l.pixels.as_ref().ok_or("no pixels")?;
            let lx = x - l.x;
            let ly = y - l.y;
            if lx < 0 || ly < 0 || lx >= p.w as i32 || ly >= p.h as i32 { return Err("outside layer".into()); }
            p.get(lx as u32, ly as u32)
        };
        unsafe { *r = px[0]; *g = px[1]; *b = px[2]; *a = px[3]; }
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_histogram(doc: PFDoc, layer_id: u64, out: *mut u32, out_len: usize) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        if out_len < 1024 { return Err("histogram buffer too small".into()); }
        let out = unsafe { std::slice::from_raw_parts_mut(out, 1024) };
        d.histogram(layer_id, out.try_into().map_err(|_| "bad len")?);
        0
    })
}

// ============================ Selections ============================

/// mode: 0 replace, 1 add, 2 subtract, 3 intersect
#[no_mangle]
pub extern "C" fn pf_selection_rect(doc: PFDoc, x: f64, y: f64, w: f64, h: f64, mode: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let m = sel_mode(mode)?;
        select::select_rect(d, x, y, w, h, m);
        d.push_history("Rectangular Select");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_selection_ellipse(doc: PFDoc, cx: f64, cy: f64, rx: f64, ry: f64, mode: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let m = sel_mode(mode)?;
        select::select_ellipse(d, cx, cy, rx, ry, m);
        d.push_history("Elliptical Select");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_selection_lasso(doc: PFDoc, pts: *const f64, npts: i32, mode: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        if npts < 3 { return Err("not enough points".into()); }
        let pts = unsafe { std::slice::from_raw_parts(pts, npts as usize * 2) };
        let m = sel_mode(mode)?;
        select::select_lasso(d, pts, m);
        d.push_history("Lasso Select");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_selection_wand(doc: PFDoc, x: i32, y: i32, tolerance: u8, contiguous: i32, mode: i32, sample_composite: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let m = sel_mode(mode)?;
        let mask = select::select_wand(d, x, y, tolerance, contiguous != 0, m, sample_composite != 0);
        d.selection = Some(Arc::new(mask));
        d.cached_outline = None;
        d.push_history("Magic Wand");
        0
    })
}

fn sel_mode(m: i32) -> Result<select::SelMode, String> {
    match m {
        0 => Ok(select::SelMode::Replace),
        1 => Ok(select::SelMode::Add),
        2 => Ok(select::SelMode::Subtract),
        3 => Ok(select::SelMode::Intersect),
        _ => Err("bad mode".into()),
    }
}

#[no_mangle]
pub extern "C" fn pf_selection_all(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        d.selection = Some(Arc::new(MaskBuffer::new(d.w, d.h, 255)?));
        d.cached_outline = None;
        d.push_history("Select All");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_selection_none(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        d.selection = None;
        d.cached_outline = None;
        d.push_history("Deselect");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_selection_invert(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        match &d.selection {
            Some(m) => {
                let mut inv = m.as_ref().clone();
                for v in inv.data.iter_mut() { *v = 255 - *v; }
                d.selection = Some(Arc::new(inv));
            }
            None => {
                let mut m = MaskBuffer::new(d.w, d.h, 0)?;
                for v in m.data.iter_mut() { *v = 255; }
                d.selection = Some(Arc::new(m));
            }
        }
        d.cached_outline = None;
        d.push_history("Invert Selection");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_selection_feather(doc: PFDoc, radius: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        select::feather(d, radius);
        d.push_history("Feather Selection");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_selection_bounds(doc: PFDoc, x: *mut i32, y: *mut i32, w: *mut u32, h: *mut u32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        match select::bounds(d) {
            Some((bx, by, bw, bh)) => {
                unsafe { *x = bx; *y = by; *w = bw; *h = bh; }
                1
            }
            None => 0,
        }
    })
}

/// Returns pointer to segment array [x0,y0,x1,y1]*count, valid until next call.
#[no_mangle]
pub extern "C" fn pf_selection_outline(doc: PFDoc, count: *mut i32) -> *const f32 {
    guard!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        if d.cached_outline.is_none() {
            let o = select::outline(d);
            d.cached_outline = Some(o);
        }
        let o = d.cached_outline.as_ref().unwrap();
        unsafe { *count = (o.len() / 4) as i32; }
        o.as_ptr()
    })
}

#[no_mangle]
pub extern "C" fn pf_selection_stroke(doc: PFDoc, r: u8, g: u8, b: u8, a: u8, width: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let segs = {
            if d.cached_outline.is_none() {
                let o = select::outline(d);
                d.cached_outline = Some(o);
            }
            d.cached_outline.clone().unwrap()
        };
        if segs.is_empty() { return Err("no selection".into()); }
        for s in segs.chunks_exact(4) {
            let pts = [s[0] as f64, s[1] as f64, s[2] as f64, s[3] as f64];
            geom::draw_shape(d, geom::ShapeKind::Line, &pts, Some(([r, g, b, a], width)), None)?;
        }
        d.push_history("Stroke Selection");
        0
    })
}

/// Delete (clear) selected pixels on active layer.
#[no_mangle]
pub extern "C" fn pf_selection_clear(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let sel = d.selection.clone();
        let layer = d.active_layer_mut().ok_or("no active layer")?;
        if layer.kind != LayerKind::Raster { return Err("group layer".into()); }
        if layer.locked { return Err("layer locked".into()); }
        let px = layer.pixels.as_mut().ok_or("no pixels")?;
        let buf = Arc::get_mut(px).ok_or("busy")?;
        let (lx, ly) = (layer.x, layer.y);
        match &sel {
            None => {
                for v in buf.data.chunks_exact_mut(4) { v[3] = 0; }
            }
            Some(m) => {
                for y in 0..buf.h {
                    for x in 0..buf.w {
                        let dx = x as i32 + lx;
                        let dy = y as i32 + ly;
                        let v = if dx < 0 || dy < 0 || dx >= m.w as i32 || dy >= m.h as i32 { 0.0 }
                            else { m.data[(dy as usize) * (m.w as usize) + (dx as usize)] as f32 / 255.0 };
                        if v > 0.0 {
                            let i = PixelBuffer::idx(x, y, buf.w);
                            let a = buf.data[i + 3] as f32 / 255.0 * (1.0 - v);
                            buf.data[i + 3] = a.round() as u8;
                        }
                    }
                }
            }
        }
        d.push_history("Clear");
        0
    })
}

// ============================ Transforms ============================

#[no_mangle]
pub extern "C" fn pf_layer_translate(doc: PFDoc, id: u64, dx: i32, dy: i32, commit: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        l.x += dx;
        l.y += dy;
        if commit != 0 {
            d.push_history("Move Layer");
        }
        0
    })
}

/// Affine bake. Matrix as [a,b,c,d,tx,ty] mapping OUTPUT -> INPUT? No: maps layer/doc coords.
/// We treat m as the FORWARD transform (dest = m(src)); engine inverts internally.
/// m: 6 floats: a b c d e f  (x' = a x + c y + e;  y' = b x + d y + f)
/// resample: 0 nearest, 1 bilinear, 2 bicubic
#[no_mangle]
pub extern "C" fn pf_layer_affine(doc: PFDoc, id: u64, m: *const f64, resample: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let m = unsafe { std::slice::from_raw_parts(m, 6) };
        let mat = geom::Mat3::affine(m[0], m[1], m[2], m[3], m[4], m[5]);
        let r = resample_mode(resample)?;
        geom::layer_affine(d, id, &mat, r)?;
        d.push_history("Transform");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_layer_flip(doc: PFDoc, id: u64, horizontal: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let l = d.root.find_mut(id).ok_or("layer not found")?;
        let px = l.pixels.as_ref().ok_or("no pixels")?.as_ref().clone();
        let mut out = px.clone();
        for y in 0..px.h {
            for x in 0..px.w {
                let sx = if horizontal != 0 { px.w - 1 - x } else { x };
                let sy = if horizontal != 0 { y } else { px.h - 1 - y };
                out.set(x, y, px.get(sx, sy));
            }
        }
        let l = d.root.find_mut(id).unwrap();
        l.pixels = Some(Arc::new(out));
        d.push_history("Flip Layer");
        0
    })
}

/// deg: 90, 180, 270 (clockwise)
#[no_mangle]
pub extern "C" fn pf_image_rotate(doc: PFDoc, deg: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let (w, h) = (d.w, d.h);
        let rot_layer = |l: &mut LayerNode| -> Result<(), String> {
            if l.kind != LayerKind::Raster { return Ok(()); }
            let px = l.pixels.as_ref().ok_or("no pixels")?.as_ref().clone();
            let (nw, nh) = match deg {
                90 | 270 => (h, w),
                _ => (w, h),
            };
            let mut out = PixelBuffer::new(nw, nh, [0, 0, 0, 0])?;
            for y in 0..px.h {
                for x in 0..px.w {
                    let (nx, ny) = match deg {
                        90 => (h as i64 - 1 - y as i64, x as i64),
                        180 => (w as i64 - 1 - x as i64, h as i64 - 1 - y as i64),
                        _ => (y as i64, w as i64 - 1 - x as i64),
                    };
                    if nx >= 0 && ny >= 0 {
                        out.set(nx as u32, ny as u32, px.get(x, y));
                    }
                }
            }
            l.pixels = Some(Arc::new(out));
            Ok(())
        };
        fn walk_rot(node: &mut LayerNode, f: &dyn Fn(&mut LayerNode) -> Result<(), String>) -> Result<(), String> {
            f(node)?;
            for c in node.children.iter_mut() {
                walk_rot(c, f)?;
            }
            Ok(())
        }
        walk_rot(&mut d.root, &rot_layer)?;
        match deg {
            90 | 270 => { d.w = h; d.h = w; }
            _ => {}
        }
        d.push_history("Rotate Image");
        0
    })
}

/// Flip the whole image (all layers, canvas dims unchanged).
/// horizontal != 0 → mirror left/right, else mirror top/bottom.
#[no_mangle]
pub extern "C" fn pf_image_flip(doc: PFDoc, horizontal: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let (w, h) = (d.w, d.h);
        let flip_layer = |l: &mut LayerNode| -> Result<(), String> {
            if l.kind != LayerKind::Raster { return Ok(()); }
            let px = l.pixels.as_ref().ok_or("no pixels")?.as_ref().clone();
            let mut out = PixelBuffer::new(px.w, px.h, [0, 0, 0, 0])?;
            for y in 0..px.h {
                for x in 0..px.w {
                    let sx = if horizontal != 0 { px.w - 1 - x } else { x };
                    let sy = if horizontal != 0 { y } else { px.h - 1 - y };
                    out.set(x, y, px.get(sx, sy));
                }
            }
            // layer offset mirrors inside the canvas: x' = w - (x + pw)
            if horizontal != 0 {
                l.x = (w as i64 - (l.x as i64 + px.w as i64)) as i32;
            } else {
                l.y = (h as i64 - (l.y as i64 + px.h as i64)) as i32;
            }
            l.pixels = Some(Arc::new(out));
            Ok(())
        };
        fn walk_flip(node: &mut LayerNode, f: &dyn Fn(&mut LayerNode) -> Result<(), String>) -> Result<(), String> {
            f(node)?;
            for c in node.children.iter_mut() {
                walk_flip(c, f)?;
            }
            Ok(())
        }
        walk_flip(&mut d.root, &flip_layer)?;
        d.push_history("Flip Image");
        0
    })
}

/// 4 dest corners (doc coords) for the layer's current rect.
#[no_mangle]
pub extern "C" fn pf_layer_perspective(doc: PFDoc, id: u64, corners: *const f64, resample: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let c = unsafe { std::slice::from_raw_parts(corners, 8) };
        let quad = [(c[0], c[1]), (c[2], c[3]), (c[4], c[5]), (c[6], c[7])];
        let r = resample_mode(resample)?;
        geom::layer_perspective(d, id, &quad, r)?;
        d.push_history("Perspective");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_image_resize(doc: PFDoc, nw: u32, nh: u32, resample: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let r = resample_mode(resample)?;
        let (w, h) = (d.w, d.h);
        let scale_x = nw as f64 / w as f64;
        let scale_y = nh as f64 / h as f64;
        fn walk_resize(node: &mut LayerNode, r: geom::Resample, nw: u32, nh: u32, sx: f64, sy: f64) -> Result<(), String> {
            if let Some(px) = &node.pixels {
                let resized = geom::resize_buffer(px, nw, nh, r)?;
                node.pixels = Some(Arc::new(resized));
                node.x = (node.x as f64 * sx).round() as i32;
                node.y = (node.y as f64 * sy).round() as i32;
            }
            if let Some(m) = &node.mask {
                // rescale mask via channel data
                let mut as_px = PixelBuffer::new(m.w, m.h, [0, 0, 0, 0])?;
                for i in 0..(m.w * m.h) as usize {
                    as_px.data[i * 4] = m.data[i];
                    as_px.data[i * 4 + 1] = m.data[i];
                    as_px.data[i * 4 + 2] = m.data[i];
                    as_px.data[i * 4 + 3] = 255;
                }
                let resized = geom::resize_buffer(&as_px, nw, nh, geom::Resample::Bilinear)?;
                let mut nm = MaskBuffer::new(nw, nh, 0)?;
                for i in 0..(nw * nh) as usize {
                    nm.data[i] = resized.data[i * 4];
                }
                node.mask = Some(Arc::new(nm));
            }
            for c in node.children.iter_mut() {
                walk_resize(c, r, nw, nh, sx, sy)?;
            }
            Ok(())
        }
        walk_resize(&mut d.root, r, nw, nh, scale_x, scale_y)?;
        d.w = nw;
        d.h = nh;
        if let Some(s) = &mut d.selection {
            let mut as_px = PixelBuffer::new(s.w, s.h, [0, 0, 0, 0])?;
            for i in 0..(s.w * s.h) as usize {
                as_px.data[i * 4] = s.data[i];
                as_px.data[i * 4 + 1] = s.data[i];
                as_px.data[i * 4 + 2] = s.data[i];
                as_px.data[i * 4 + 3] = 255;
            }
            let resized = geom::resize_buffer(&as_px, nw, nh, geom::Resample::Bilinear)?;
            let mut nm = MaskBuffer::new(nw, nh, 0)?;
            for i in 0..(nw * nh) as usize {
                nm.data[i] = resized.data[i * 4];
            }
            d.selection = Some(Arc::new(nm));
        }
        d.push_history("Image Size");
        0
    })
}

/// anchor: 0..8 (topleft..bottomright, row-major)
#[no_mangle]
pub extern "C" fn pf_canvas_resize(doc: PFDoc, nw: u32, nh: u32, anchor: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let a = match anchor {
            0 => geom::Anchor::TopLeft, 1 => geom::Anchor::Top, 2 => geom::Anchor::TopRight,
            3 => geom::Anchor::Left, 4 => geom::Anchor::Center, 5 => geom::Anchor::Right,
            6 => geom::Anchor::BottomLeft, 7 => geom::Anchor::Bottom, _ => geom::Anchor::BottomRight,
        };
        let (w, h) = (d.w, d.h);
        let (ox, oy) = geom::anchor_offset(w as i64, h as i64, nw as i64, nh as i64, a);
        fn walk_canvas(node: &mut LayerNode, nw: u32, nh: u32, ox: i64, oy: i64) -> Result<(), String> {
            if let Some(px) = &node.pixels {
                let mut out = PixelBuffer::new(nw, nh, [0, 0, 0, 0])?;
                for y in 0..px.h {
                    for x in 0..px.w {
                        let dx = x as i64 + node.x as i64 + ox;
                        let dy = y as i64 + node.y as i64 + oy;
                        if dx >= 0 && dy >= 0 && dx < nw as i64 && dy < nh as i64 {
                            out.set(dx as u32, dy as u32, px.get(x, y));
                        }
                    }
                }
                node.pixels = Some(Arc::new(out));
                node.x = 0;
                node.y = 0;
            }
            for c in node.children.iter_mut() {
                walk_canvas(c, nw, nh, ox, oy)?;
            }
            Ok(())
        }
        walk_canvas(&mut d.root, nw, nh, ox, oy)?;
        d.w = nw;
        d.h = nh;
        d.selection = None;
        d.cached_outline = None;
        d.push_history("Canvas Size");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_image_crop(doc: PFDoc, x: i32, y: i32, w: u32, h: u32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let (dw, dh) = (d.w, d.h);
        if w == 0 || h == 0 || x >= dw as i32 || y >= dh as i32 { return Err("bad crop".into()); }
        let cx0 = x.max(0);
        let cy0 = y.max(0);
        let cw = (w as i64).min(dw as i64 - cx0 as i64).max(0) as u32;
        let ch = (h as i64).min(dh as i64 - cy0 as i64).max(0) as u32;
        fn walk_crop(node: &mut LayerNode, cx0: i32, cy0: i32, cw: u32, ch: u32) -> Result<(), String> {
            if let Some(px) = &node.pixels {
                let mut out = PixelBuffer::new(cw, ch, [0, 0, 0, 0])?;
                for y in 0..out.h {
                    for x in 0..out.w {
                        let sx = x as i64 + cx0 as i64 - node.x as i64;
                        let sy = y as i64 + cy0 as i64 - node.y as i64;
                        if sx >= 0 && sy >= 0 && sx < px.w as i64 && sy < px.h as i64 {
                            out.set(x, y, px.get(sx as u32, sy as u32));
                        }
                    }
                }
                node.pixels = Some(Arc::new(out));
                node.x = 0;
                node.y = 0;
            }
            for c in node.children.iter_mut() {
                walk_crop(c, cx0, cy0, cw, ch)?;
            }
            Ok(())
        }
        walk_crop(&mut d.root, cx0, cy0, cw, ch)?;
        d.w = cw;
        d.h = ch;
        d.selection = None;
        d.cached_outline = None;
        d.push_history("Crop");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_image_trim(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        // compute opaque bbox of composite
        let mut comp = vec![0u8; (d.w * d.h * 4) as usize];
        d.composite(Rect { x: 0, y: 0, w: d.w, h: d.h }, &mut comp);
        let (mut x0, mut y0) = (i32::MAX, i32::MAX);
        let (mut x1, mut y1) = (i32::MIN, i32::MIN);
        let mut found = false;
        for y in 0..d.h {
            for x in 0..d.w {
                let a = comp[((y * d.w + x) * 4 + 3) as usize];
                if a > 0 {
                    found = true;
                    x0 = x0.min(x as i32);
                    y0 = y0.min(y as i32);
                    x1 = x1.max(x as i32);
                    y1 = y1.max(y as i32);
                }
            }
        }
        if !found { return Err("nothing to trim".into()); }
        let (w, h) = ((x1 - x0 + 1) as u32, (y1 - y0 + 1) as u32);
        let r = pf_image_crop(doc, x0, y0, w, h);
        r
    })
}

fn resample_mode(r: i32) -> Result<geom::Resample, String> {
    match r {
        0 => Ok(geom::Resample::Nearest),
        1 => Ok(geom::Resample::Bilinear),
        2 => Ok(geom::Resample::Bicubic),
        _ => Err("bad resample".into()),
    }
}

// ============================ Adjustments ============================

#[no_mangle]
pub extern "C" fn pf_adjust_brightness_contrast(doc: PFDoc, brightness: f32, contrast: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        adjust::brightness_contrast(d, brightness, contrast)?;
        d.push_history("Brightness/Contrast");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_adjust_hue_saturation(doc: PFDoc, hue: f32, sat: f32, light: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        adjust::hue_saturation(d, hue, sat, light)?;
        d.push_history("Hue/Saturation");
        0
    })
}

/// channel: 0 rgb, 1 r, 2 g, 3 b
#[no_mangle]
pub extern "C" fn pf_adjust_levels(doc: PFDoc, in_black: u8, in_white: u8, gamma: f32, out_black: u8, out_white: u8, channel: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let ch = match channel {
            1 => adjust::Channel::R,
            2 => adjust::Channel::G,
            3 => adjust::Channel::B,
            _ => adjust::Channel::Rgb,
        };
        adjust::levels(d, in_black, in_white, gamma, out_black, out_white, ch)?;
        d.push_history("Levels");
        0
    })
}

/// points: pairs of f32 (x,y), n = count of points
#[no_mangle]
pub extern "C" fn pf_adjust_curves(doc: PFDoc, points: *const f32, n: i32, channel: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let pts_raw = unsafe { std::slice::from_raw_parts(points, n as usize * 2) };
        let pts: Vec<(f32, f32)> = pts_raw.chunks_exact(2).map(|c| (c[0], c[1])).collect();
        let ch = match channel {
            1 => adjust::Channel::R,
            2 => adjust::Channel::G,
            3 => adjust::Channel::B,
            _ => adjust::Channel::Rgb,
        };
        adjust::apply_curves(d, &pts, ch)?;
        d.push_history("Curves");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_adjust_invert(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        adjust::invert(d)?;
        d.push_history("Invert");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_adjust_desaturate(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        adjust::desaturate(d)?;
        d.push_history("Desaturate");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_adjust_threshold(doc: PFDoc, t: u8) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        adjust::threshold(d, t)?;
        d.push_history("Threshold");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_adjust_posterize(doc: PFDoc, levels: u8) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        adjust::posterize(d, levels)?;
        d.push_history("Posterize");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_adjust_auto_contrast(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        adjust::auto_contrast(d)?;
        d.push_history("Auto Contrast");
        0
    })
}

// ============================ Filters ============================

#[no_mangle]
pub extern "C" fn pf_filter_blur(doc: PFDoc, radius: f32, gaussian: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        filters::blur(d, radius, gaussian != 0)?;
        d.push_history("Blur");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_filter_sharpen(doc: PFDoc, amount: f32, radius: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        filters::sharpen(d, amount, radius)?;
        d.push_history("Sharpen");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_filter_noise(doc: PFDoc, amount: u8, mono: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        filters::noise(d, amount, mono != 0)?;
        d.push_history("Noise");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_filter_pixelate(doc: PFDoc, size: u32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        filters::pixelate(d, size)?;
        d.push_history("Pixelate");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_filter_twirl(doc: PFDoc, angle: f32, radius: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        filters::twirl(d, angle, radius)?;
        d.push_history("Twirl");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_filter_wave(doc: PFDoc, amplitude: f32, wavelength: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        filters::wave(d, amplitude, wavelength)?;
        d.push_history("Wave");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_filter_edges(doc: PFDoc, strength: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        filters::edge_detect(d, strength)?;
        d.push_history("Edge Detect");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_filter_emboss(doc: PFDoc, strength: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        filters::emboss(d, strength)?;
        d.push_history("Emboss");
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_filter_vignette(doc: PFDoc, amount: f32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        filters::vignette(d, amount)?;
        d.push_history("Vignette");
        0
    })
}

// ============================ History ============================

#[no_mangle]
pub extern "C" fn pf_history_undo(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        if d.history.undo() {
            let s = d.history.current().clone();
            d.apply_state(&s);
            d.modified = true;
            1
        } else {
            0
        }
    })
}

#[no_mangle]
pub extern "C" fn pf_history_redo(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        if d.history.redo() {
            let s = d.history.current().clone();
            d.apply_state(&s);
            d.modified = true;
            1
        } else {
            0
        }
    })
}

#[no_mangle]
pub extern "C" fn pf_history_count(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        d.history.states.len() as i32
    })
}

#[no_mangle]
pub extern "C" fn pf_history_index(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_ref().ok_or("null doc")? };
        d.history.index as i32
    })
}

/// Owned by engine until next call.
#[no_mangle]
pub extern "C" fn pf_history_label(doc: PFDoc, i: i32) -> *const c_char {
    guard!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let i = i.max(0) as usize;
        if i >= d.history.states.len() { std::ptr::null_mut() }
        else {
            let label = d.history.states[i].0.clone();
            let c = CString::new(label).unwrap();
            c.into_raw()
        }
    })
}

#[no_mangle]
pub extern "C" fn pf_history_goto(doc: PFDoc, i: i32) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        if d.history.goto(i.max(0) as usize) {
            let s = d.history.current().clone();
            d.apply_state(&s);
            d.modified = true;
            1
        } else {
            0
        }
    })
}

#[no_mangle]
pub extern "C" fn pf_history_clear(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let cur = d.state();
        d.history.states.clear();
        d.history.states.push(("Document".into(), cur));
        d.history.index = 0;
        0
    })
}

// ============================ Preview (dialog live preview) ============================

#[no_mangle]
pub extern "C" fn pf_preview_begin(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        if d.previewing.is_some() { return Err("preview already active".into()); }
        d.previewing = Some(d.state());
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_preview_commit(doc: PFDoc, label: *const c_char) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        d.previewing = None;
        let label = unsafe { cstr(label)? };
        d.push_history(label);
        0
    })
}

#[no_mangle]
pub extern "C" fn pf_preview_cancel(doc: PFDoc) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        if let Some(base) = d.previewing.take() {
            d.apply_state(&base);
        }
        0
    })
}

// ============================ IO ============================

/// format: 0 png, 1 jpeg, 2 webp, 3 bmp, 4 tiff, 5 gif, 6 ora
/// quality 0-100; lossless for webp; bg rgb used when format needs flatten (0 = white default);
/// scale_pct 1-400 (100 = original)
#[no_mangle]
pub extern "C" fn pf_export(
    doc: PFDoc, path: *const c_char, format: i32, quality: u8, lossless: i32,
    bg_r: u8, bg_g: u8, bg_b: u8, scale_pct: u32,
) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let p = unsafe { cstr(path)? };
        let fmt = match format {
            0 => io::Format::Png,
            1 => io::Format::Jpeg,
            2 => io::Format::Webp,
            3 => io::Format::Bmp,
            4 => io::Format::Tiff,
            5 => io::Format::Gif,
            6 => io::Format::Ora,
            _ => return Err("bad format".into()),
        };
        if fmt == io::Format::Ora {
            io::save_ora(d, p)?;
            d.modified = false;
        } else {
            let mut comp = vec![0u8; (d.w * d.h * 4) as usize];
            d.composite(Rect { x: 0, y: 0, w: d.w, h: d.h }, &mut comp);
            io::export_image(p, fmt, comp, d.w, d.h, quality, lossless != 0, Some([bg_r, bg_g, bg_b]), scale_pct)?;
        }
        0
    })
}

/// Save-as OpenRaster (layered project).
#[no_mangle]
pub extern "C" fn pf_save_ora(doc: PFDoc, path: *const c_char) -> i32 {
    guard_int!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let p = unsafe { cstr(path)? };
        io::save_ora(d, p)?;
        d.modified = false;
        d.path = Some(p.to_string());
        0
    })
}

/// Import an image file as a new layer; returns layer id.
#[no_mangle]
pub extern "C" fn pf_import_image_as_layer(doc: PFDoc, path: *const c_char, name: *const c_char) -> u64 {
    guard_id!({
        let d = unsafe { doc.as_mut().ok_or("null doc")? };
        let p = unsafe { cstr(path)? };
        let name = unsafe { cstr(name)? }.to_string();
        let buf = if p.rsplit('.').next().unwrap_or("").eq_ignore_ascii_case("svg") {
            io::open_svg(p)?
        } else {
            io::open_image(p)?
        };
        let id = d.alloc_id();
        d.root.children.insert(0, LayerNode::new_raster(id, name, buf));
        d.active = id;
        d.push_history("Import Layer");
        id
    })
}

#[no_mangle]
pub extern "C" fn pf_rust_engine_info() -> *const c_char {
    guard!({
        let info = format!(
            "PixelForge Rust Engine v{} | rayon threads: {} | blend modes: {}",
            env!("CARGO_PKG_VERSION"),
            rayon::current_num_threads(),
            blend::BLEND_MODES.len(),
        );
        CString::new(info).unwrap().into_raw() as *const c_char
    })
}

#[cfg(test)]
mod image_flip_tests {
    use super::*;

    fn make_doc_with_offcenter_layer() -> (Document, u64) {
        // 100x80 canvas; layer 40x30 placed at (20,10) with a recognizable pixel pattern.
        let mut d = Document::new(100, 80, [255, 255, 255, 255]).unwrap();
        let id = d.alloc_id();
        let mut buf = PixelBuffer::new(40, 30, [0, 0, 0, 0]).unwrap();
        // left column red, right column blue
        for y in 0..30 {
            buf.set(0, y, [255, 0, 0, 255]);
            buf.set(39, y, [0, 0, 255, 255]);
        }
        let mut node = LayerNode::new_raster(id, "L".to_string(), buf);
        node.x = 20;
        node.y = 10;
        d.root.children.insert(0, node);
        d.active = id;
        (d, id)
    }

    #[test]
    fn image_flip_horizontal_mirrors_layers_and_offsets() {
        let (mut d, id) = make_doc_with_offcenter_layer();
        let before = d.root.find(id).unwrap().pixels.as_ref().unwrap().clone();
        assert_eq!(pf_image_flip(&mut d as PFDoc, 1), 0);
        let l = d.root.find(id).unwrap();
        let after = l.pixels.as_ref().unwrap();
        // canvas dims unchanged
        assert_eq!((d.w, d.h), (100, 80));
        // offset mirrored: x' = 100 - (20 + 40) = 40
        assert_eq!(l.x, 40);
        assert_eq!(l.y, 10);
        // pixel content mirrored: old(0,y)=red -> new(39,y)=red
        let px = after.as_ref();
        assert_eq!(px.get(39, 5), [255, 0, 0, 255]);
        assert_eq!(px.get(0, 5), [0, 0, 255, 255]);
        // double flip restores original (content + offset)
        assert_eq!(pf_image_flip(&mut d as PFDoc, 1), 0);
        let l2 = d.root.find(id).unwrap();
        assert_eq!(l2.x, 20);
        let p2 = l2.pixels.as_ref().unwrap().as_ref();
        for y in 0..30 {
            for x in 0..40 {
                assert_eq!(p2.get(x, y), before.as_ref().get(x, y));
            }
        }
    }

    #[test]
    fn image_flip_vertical_mirrors_offsets() {
        let (mut d, id) = make_doc_with_offcenter_layer();
        assert_eq!(pf_image_flip(&mut d as PFDoc, 0), 0);
        let l = d.root.find(id).unwrap();
        assert_eq!((d.w, d.h), (100, 80));
        // y' = 80 - (10 + 30) = 40 ; x untouched
        assert_eq!(l.x, 20);
        assert_eq!(l.y, 40);
    }
}

#[cfg(test)]
mod perspective_tests {
    use super::*;

    #[test]
    fn perspective_actually_warps() {
        let mut d = Document::new(400, 300, [255, 255, 255, 255]).unwrap();
        let id = d.alloc_id();
        let mut buf = PixelBuffer::new(100, 80, [0, 0, 0, 0]).unwrap();
        for y in 0..80 {
            for x in 0..50 {
                buf.set(x, y, [255, 0, 0, 255]); // left half red
            }
        }
        let mut node = LayerNode::new_raster(id, "L".to_string(), buf);
        node.x = 50;
        node.y = 40;
        d.root.children.insert(0, node);
        d.active = id;

        let before = d.root.find(id).unwrap().pixels.as_ref().unwrap().as_ref().clone();
        // pull the top-left corner of the (50,40)-(150,120) rect to (10,10)
        let corners = [10.0, 10.0, 150.0, 40.0, 150.0, 120.0, 50.0, 120.0];
        let rc = pf_layer_perspective(&mut d as PFDoc, id, corners.as_ptr(), 2);
        if rc != 0 {
            let e = pf_last_error();
            let msg = unsafe {
                if !e.is_null() {
                    let s = std::ffi::CStr::from_ptr(e).to_string_lossy().to_string();
                    pf_free_str(e as *mut c_char);
                    s
                } else { "none".to_string() }
            };
            panic!("pf_layer_perspective rc={} err={}", rc, msg);
        }
        let after = d.root.find(id).unwrap().pixels.as_ref().unwrap().as_ref().clone();
        let mut diff = 0usize;
        if before.w == after.w && before.h == after.h {
            let n = (before.w as usize) * (before.h as usize);
            for i in 0..n {
                if before.data[i * 4] != after.data[i * 4] { diff += 1; }
            }
        }
        let l = d.root.find(id).unwrap();
        eprintln!("perspective: before {}x{} after {}x{} diff={} offset=({},{})",
                  before.w, before.h, after.w, after.h, diff, l.x, l.y);
        // the TL corner was pulled to (10,10): buffer must change, and the
        // tight bbox must start at the dst quad minimum
        assert!(before.w != after.w || before.h != after.h || diff > 100,
                "perspective warp produced identical pixels");
        assert_eq!((l.x, l.y), (10, 10), "perspective bbox offset");
    }
}
