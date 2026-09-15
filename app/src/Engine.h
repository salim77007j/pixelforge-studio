#pragma once
#include <QImage>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QVector>
#include <cstdint>
#include "pf_engine.h"

// RAII-ish wrapper over a PFDoc engine document handle.
class EngineDoc {
public:
    EngineDoc() = default;
    explicit EngineDoc(PFDoc d) : m_doc(d) {}
    ~EngineDoc() { close(); }
    EngineDoc(const EngineDoc &) = delete;
    EngineDoc &operator=(const EngineDoc &) = delete;
    EngineDoc(EngineDoc &&o) noexcept : m_doc(o.m_doc) { o.m_doc = nullptr; }
    EngineDoc &operator=(EngineDoc &&o) noexcept {
        if (this != &o) { close(); m_doc = o.m_doc; o.m_doc = nullptr; }
        return *this;
    }

    bool isValid() const { return m_doc != nullptr; }
    PFDoc handle() const { return m_doc; }
    void close() { if (m_doc) { pf_document_close(m_doc); m_doc = nullptr; } }

    // ---- info ----
    void size(uint32_t &w, uint32_t &h) const { w = h = 0; if (m_doc) pf_document_size(m_doc, &w, &h); }
    bool modified() const { return m_doc && pf_document_modified(m_doc); }
    void markSaved() { if (m_doc) pf_document_mark_saved(m_doc); }
    QString path() const {
        if (!m_doc) return {};
        const char *p = pf_document_path(m_doc);
        return p ? QString::fromUtf8(p) : QString();
    }
    void setPath(const QString &p) { m_path = p; }
    QString displayPath() const { QString p = path(); return p.isEmpty() ? m_path : p; }

    // ---- composite into a QImage ----
    void composite(QImage &img, int x, int y, uint32_t w, uint32_t h) {
        if (!m_doc) return;
        if (img.width() != (int)w || img.height() != (int)h || img.format() != QImage::Format_RGBA8888)
            img = QImage((int)w, (int)h, QImage::Format_RGBA8888);
        pf_document_composite(m_doc, x, y, w, h, img.bits(), (size_t)img.sizeInBytes());
    }
    QImage compositeFull() {
        uint32_t w, h; size(w, h);
        QImage img;
        composite(img, 0, 0, w, h);
        return img;
    }

    // ---- layers ----
    QString layersJson() const {
        if (!m_doc) return {};
        const char *j = pf_layers_json(m_doc);
        QString s = QString::fromUtf8(j);
        pf_free_str(const_cast<char *>(j));
        return s;
    }
    uint64_t activeLayer() const { return m_doc ? pf_layer_active(m_doc) : 0; }
    uint64_t addLayer(int kind, const QString &name) { return m_doc ? pf_layer_add(m_doc, kind, name.toUtf8().constData()) : 0; }
    uint64_t addLayerFromImage(const QImage &img, const QString &name, int x, int y) {
        if (!m_doc) return 0;
        QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
        return pf_layer_add_pixels(m_doc, rgba.width(), rgba.height(), rgba.constBits(),
                                   (size_t)rgba.sizeInBytes(), name.toUtf8().constData(), x, y);
    }
    void removeLayer(uint64_t id) { if (m_doc) pf_layer_remove(m_doc, id); }
    uint64_t duplicateLayer(uint64_t id) { return m_doc ? pf_layer_duplicate(m_doc, id) : 0; }
    void setLayerName(uint64_t id, const QString &n) { if (m_doc) pf_layer_set_name(m_doc, id, n.toUtf8().constData()); }
    void setLayerVisible(uint64_t id, bool v) { if (m_doc) pf_layer_set_visible(m_doc, id, v ? 1 : 0); }
    void setLayerLocked(uint64_t id, bool v) { if (m_doc) pf_layer_set_locked(m_doc, id, v ? 1 : 0); }
    void setLayerOpacity(uint64_t id, float o) { if (m_doc) pf_layer_set_opacity(m_doc, id, o); }
    void setLayerBlend(uint64_t id, const QString &b) { if (m_doc) pf_layer_set_blend(m_doc, id, b.toUtf8().constData()); }
    void moveLayer(uint64_t id, uint64_t parent, int index) { if (m_doc) pf_layer_move(m_doc, id, parent, index); }
    void mergeDown(uint64_t id) { if (m_doc) pf_layer_merge_down(m_doc, id); }
    void flatten() { if (m_doc) pf_flatten(m_doc); }
    void setActiveLayer(uint64_t id) { if (m_doc) pf_layer_set_active(m_doc, id); }
    void addMask(uint64_t id, bool fromSelection) { if (m_doc) pf_layer_mask_add(m_doc, id, fromSelection ? 1 : 0); }
    void removeMask(uint64_t id) { if (m_doc) pf_layer_mask_remove(m_doc, id); }
    QImage layerThumbnail(uint64_t id, int maxPx) {
        if (!m_doc) return QImage();
        QVector<uint8_t> buf(maxPx * maxPx * 4);
        uint32_t tw = 0, th = 0;
        if (pf_layer_thumbnail(m_doc, id, maxPx, buf.data(), (size_t)buf.size(), &tw, &th) != 0)
            return QImage();
        return QImage(buf.data(), (int)tw, (int)th, (int)tw * 4, QImage::Format_RGBA8888).copy();
    }
    bool layerPixels(uint64_t id, QImage &out) {
        if (!m_doc) return false;
        uint32_t w, h;
        if (pf_layer_pixels_size(m_doc, id, &w, &h) != 0) return false;
        out = QImage((int)w, (int)h, QImage::Format_RGBA8888);
        return pf_layer_pixels_get(m_doc, id, out.bits(), (size_t)out.sizeInBytes()) == 0;
    }

    // ---- history ----
    bool undo() { return m_doc && pf_history_undo(m_doc) == 1; }
    bool redo() { return m_doc && pf_history_redo(m_doc) == 1; }
    int historyCount() const { return m_doc ? pf_history_count(m_doc) : 0; }
    int historyIndex() const { return m_doc ? pf_history_index(m_doc) : 0; }
    QString historyLabel(int i) const {
        if (!m_doc) return {};
        const char *l = pf_history_label(m_doc, i);
        QString s = l ? QString::fromUtf8(l) : QString();
        pf_free_str(const_cast<char *>(l));
        return s;
    }
    void historyGoto(int i) { if (m_doc) pf_history_goto(m_doc, i); }

    // ---- misc ----
    static QString lastError() {
        const char *e = pf_last_error();
        return e ? QString::fromUtf8(e) : QStringLiteral("unknown error");
    }
    static QString rustInfo() {
        const char *e = pf_rust_engine_info();
        QString s = e ? QString::fromUtf8(e) : QString();
        pf_free_str(const_cast<char *>(e));
        return s;
    }

private:
    PFDoc m_doc = nullptr;
    QString m_path; // fallback title when doc not opened from file
};
