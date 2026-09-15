#include "BrushPanel.h"
#include "../MainWindow.h"
#include "../Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QPainter>
#include <QtMath>
#include <algorithm>

// ---------------- StrokePreview ----------------

StrokePreview::StrokePreview(MainWindow *mw, QWidget *m) : QWidget(m), m_mw(mw) {
    setMinimumHeight(56);
    regenerate();
}

void StrokePreview::regenerate() {
    const BrushSettings &b = m_mw->brushSettings();
    int w = qMax(240, width());
    int h = 56;
    m_stroke = QImage(w, h, QImage::Format_RGBA8888);
    m_stroke.fill(Qt::transparent);
    QColor col = m_mw->fgColor();
    // stamp along a tapered sine path (mirrors engine dab math)
    auto dab = [&](float cx, float cy, float radius, float alpha) {
        int x0 = qMax(0, int(cx - radius)), y0 = qMax(0, int(cy - radius));
        int x1 = qMin(w - 1, int(cx + radius)), y1 = qMin(h - 1, int(cy + radius));
        float hard = b.hardness / 100.0f;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                float d = sqrtf(dx * dx + dy * dy) / radius;
                if (d >= 1.0f) continue;
                float t = d <= hard ? 0.0f : (d - hard) / (1.0f - hard);
                t = t * t * (3 - 2 * t);
                float a = alpha * (1.0f - t);
                QRgb *px = (QRgb *)m_stroke.scanLine(y) + x;
                QColor cur = QColor::fromRgba(*px);
                float ca = cur.alphaF();
                float ta = qMin(b.opacity / 100.0f, ca + a * (1 - ca));
                if (ta <= 0) continue;
                float mix = ca <= 0.0001f ? 1.0f : (ta - ca) / ta;
                QColor nc(cur.redF() * (1 - mix) + col.redF() * mix,
                          cur.greenF() * (1 - mix) + col.greenF() * mix,
                          cur.blueF() * (1 - mix) + col.blueF() * mix);
                nc.setAlphaF(ta);
                *px = nc.rgba();
            }
    };
    float rad = qMax(1.5f, b.size / 2.0f);
    float spacing = qMax(0.75f, rad * 2.0f * qMax(0.02f, b.spacing / 100.0f));
    float x = 8;
    float midY = h / 2.0f;
    float prevX = 0, prevY = midY;
    bool first = true;
    for (x = 8; x < w - 8; x += spacing) {
        float t = (x - 8) / (float)(w - 16);
        float y = midY + sinf(t * M_PI * 1.2) * 10.0f;
        float taper = 0.25f + 0.75f * sinf(t * M_PI);
        if (first) { dab(x, y, rad, b.flow / 100.0f); first = false; }
        else {
            float dist = hypotf(x - prevX, y - prevY);
            for (float s = spacing; s <= dist; s += spacing) {
                float k = s / dist;
                dab(prevX + (x - prevX) * k, prevY + (y - prevY) * k, rad * taper, b.flow / 100.0f);
            }
        }
        prevX = x; prevY = y;
    }
    update();
}

void StrokePreview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(Theme::CardBg));
    // border
    p.setPen(QPen(QColor(Theme::Border), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    if (m_stroke.isNull()) return;
    p.drawImage(0, 0, m_stroke.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

// ---------------- BrushPanel ----------------

BrushPanel::BrushPanel(MainWindow *mw, QWidget *parent) : QWidget(parent), m_mw(mw) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);

    auto row = [&](const QString &name, QSlider *&slider, QLabel *&val, int minv, int maxv, int def) {
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(name);
        label->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:56px;").arg(Theme::TextSecondary));
        val = new QLabel(QString::number(def));
        val->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:34px;").arg(Theme::TextSecondary));
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(minv, maxv);
        slider->setValue(def);
        connect(slider, &QSlider::valueChanged, this, &BrushPanel::onChanged);
        row->addWidget(label);
        row->addWidget(slider, 1);
        row->addWidget(val);
        lay->addLayout(row);
    };

    auto *sizeRow = new QHBoxLayout;
    auto *sizeLabel = new QLabel(tr("Size"));
    sizeLabel->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:56px;").arg(Theme::TextSecondary));
    m_size = new QSpinBox;
    m_size->setRange(1, 999);
    m_size->setValue(m_mw->brushSettings().size);
    m_size->setFixedWidth(64);
    connect(m_size, &QSpinBox::valueChanged, this, &BrushPanel::onChanged);
    sizeRow->addWidget(sizeLabel);
    sizeRow->addStretch(1);
    sizeRow->addWidget(m_size);
    lay->addLayout(sizeRow);

    row(tr("Opacity"), m_opacity, m_opacityV, 1, 100, 100);
    row(tr("Flow"), m_flow, m_flowV, 1, 100, 100);
    row(tr("Hardness"), m_hardness, m_hardnessV, 0, 100, 50);
    row(tr("Spacing"), m_spacing, m_spacingV, 2, 200, 12);

    auto *dynRow = new QHBoxLayout;
    m_pressureSize = new QCheckBox(tr("Pressure → Size"));
    m_pressureOpacity = new QCheckBox(tr("Pressure → Opacity"));
    connect(m_pressureSize, &QCheckBox::toggled, this, &BrushPanel::onChanged);
    connect(m_pressureOpacity, &QCheckBox::toggled, this, &BrushPanel::onChanged);
    dynRow->addWidget(m_pressureSize);
    dynRow->addWidget(m_pressureOpacity);
    lay->addLayout(dynRow);

    m_preview = new StrokePreview(mw, this);
    lay->addWidget(m_preview);

    refresh();
}

void BrushPanel::refresh() {
    const BrushSettings &b = m_mw->brushSettings();
    m_size->blockSignals(true);
    m_size->setValue(b.size);
    m_size->blockSignals(false);
    m_opacity->blockSignals(true); m_opacity->setValue(b.opacity); m_opacity->blockSignals(false);
    m_opacityV->setNum(b.opacity);
    m_flow->blockSignals(true); m_flow->setValue(b.flow); m_flow->blockSignals(false);
    m_flowV->setNum(b.flow);
    m_hardness->blockSignals(true); m_hardness->setValue(b.hardness); m_hardness->blockSignals(false);
    m_hardnessV->setNum(b.hardness);
    m_spacing->blockSignals(true); m_spacing->setValue(b.spacing); m_spacing->blockSignals(false);
    m_spacingV->setNum(b.spacing);
    m_pressureSize->blockSignals(true); m_pressureSize->setChecked(b.pressureSize); m_pressureSize->blockSignals(false);
    m_pressureOpacity->blockSignals(true); m_pressureOpacity->setChecked(b.pressureOpacity); m_pressureOpacity->blockSignals(false);
    m_preview->regenerate();
}

void BrushPanel::onChanged() {
    BrushSettings &b = m_mw->brushSettingsRef();
    b.size = m_size->value();
    b.opacity = m_opacity->value();
    b.flow = m_flow->value();
    b.hardness = m_hardness->value();
    b.spacing = m_spacing->value();
    b.pressureSize = m_pressureSize->isChecked();
    b.pressureOpacity = m_pressureOpacity->isChecked();
    m_opacityV->setNum(b.opacity);
    m_flowV->setNum(b.flow);
    m_hardnessV->setNum(b.hardness);
    m_spacingV->setNum(b.spacing);
    m_preview->regenerate();
    emit settingsChanged();
}

void BrushPanel::applyPreset(int preset) {
    BrushSettings &b = m_mw->brushSettingsRef();
    switch (preset) {
    case 0: b.size = 24; b.hardness = 10; b.flow = 60; b.opacity = 100; b.spacing = 8; break;   // soft round
    case 1: b.size = 16; b.hardness = 100; b.flow = 100; b.opacity = 100; b.spacing = 10; break; // hard round
    case 2: b.size = 30; b.hardness = 85; b.flow = 100; b.opacity = 100; b.spacing = 4;
            b.pressureSize = true; break;                                                        // tapered ink
    case 3: b.size = 40; b.hardness = 70; b.flow = 90; b.opacity = 80; b.spacing = 25; break;    // marker
    case 4: b.size = 36; b.hardness = 5; b.flow = 25; b.opacity = 90; b.spacing = 6; break;      // airbrush
    }
    refresh();
    emit settingsChanged();
}
