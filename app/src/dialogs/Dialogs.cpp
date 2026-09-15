#include "Dialogs.h"
#include "CurvesWidget.h"
#include "../EditorTab.h"
#include "../CanvasView.h"
#include "../MainWindow.h"
#include "../Theme.h"
#include "../pf_engine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFontComboBox>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPainterPath>
#include <QTableWidget>
#include <QKeySequenceEdit>
#include <QSettings>
#include <QHeaderView>
#include <QFile>
#include <QColorDialog>
#include <QGridLayout>
#include <algorithm>

// ================= Histogram view =================

class HistogramView : public QWidget {
public:
    HistogramView(QWidget *p = nullptr) : QWidget(p) { setMinimumHeight(90); }
    void setBins(const uint32_t *bins) {
        memcpy(m_bins, bins, sizeof(m_bins));
        m_valid = true;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setPen(QPen(QColor(Theme::Border), 1));
        p.setBrush(QColor(Theme::CardBg));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
        if (!m_valid) return;
        uint32_t maxv = 1;
        for (uint32_t v : m_bins) maxv = qMax(maxv, v);
        const QColor cols[3] = {QColor(235, 87, 87, 150), QColor(86, 204, 242, 150), QColor(39, 174, 96, 150)};
        p.setClipRect(rect().adjusted(6, 6, -6, -6));
        for (int c = 0; c < 3; ++c) {
            QPainterPath hp;
            hp.moveTo(6, height() - 6);
            for (int i = 0; i < 256; ++i) {
                double x = 6 + (double)i / 255.0 * (width() - 12);
                double y = (height() - 6) - (double)m_bins[c * 256 + i] / maxv * (height() - 14);
                hp.lineTo(x, y);
            }
            hp.lineTo(width() - 6, height() - 6);
            hp.closeSubpath();
            p.fillPath(hp, cols[c]);
            p.setPen(QPen(cols[c].darker(120), 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(hp);
            p.setBrush(Qt::NoBrush);
        }
    }
private:
    uint32_t m_bins[768] = {0};
    bool m_valid = false;
};

// ================= PreviewDialog =================

PreviewDialog::PreviewDialog(QWidget *parent, EditorTab *tab, const QString &title)
    : QDialog(parent), m_tab(tab) {
    setWindowTitle(title);
    setModal(true);
    pf_preview_begin(m_tab->m_doc.handle());
}

PreviewDialog::~PreviewDialog() {
    if (!m_committed) pf_preview_cancel(m_tab->m_doc.handle());
    refreshCanvas();
}

void PreviewDialog::refreshCanvas() {
    m_tab->m_canvas->refreshComposite();
    m_tab->m_canvas->activateWindow();
    m_tab->m_canvas->repaint();
}

void PreviewDialog::restartAndApply() {
    // restore baseline, restart preview, apply current params
    pf_preview_cancel(m_tab->m_doc.handle());
    pf_preview_begin(m_tab->m_doc.handle());
    applyParams();
    refreshCanvas();
}

void PreviewDialog::accept() {
    m_committed = true;
    pf_preview_commit(m_tab->m_doc.handle(), historyLabel().toUtf8().constData());
    refreshCanvas();
    QDialog::accept();
    emit m_tab->editOccurred(m_tab);
    m_tab->m_mw->refreshAfterEdit(m_tab, true);
}

void PreviewDialog::reject() {
    QDialog::reject();
}

// ================= AdjustDialog (generic) =================

AdjustDialog::AdjustDialog(QWidget *parent, EditorTab *tab, const QString &title,
                           const QVector<ParamSpec> &params,
                           std::function<void(EditorTab *, const QVector<double> &)> apply)
    : PreviewDialog(parent, tab, title), m_specs(params), m_apply(std::move(apply)) {
    m_label = title;
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);
    for (const ParamSpec &s : params) {
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(s.name);
        label->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:110px;").arg(Theme::TextSecondary));
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange((int)(s.min / s.step), (int)(s.max / s.step));
        slider->setValue((int)(s.def / s.step));
        auto *spin = new QDoubleSpinBox;
        spin->setDecimals(s.decimals);
        spin->setRange(s.min, s.max);
        spin->setSingleStep(s.step);
        spin->setValue(s.def);
        spin->setFixedWidth(86);
        auto sync = [slider, spin, s](double v) {
            int sv = qRound(v / s.step);
            if (slider->value() != sv) slider->blockSignals(true), slider->setValue(sv), slider->blockSignals(false);
            if (qAbs(spin->value() - v) > s.step / 2) spin->blockSignals(true), spin->setValue(v), spin->blockSignals(false);
        };
        connect(slider, &QSlider::valueChanged, this, [this, spin, s, sync](int v) {
            double val = v * s.step;
            spin->blockSignals(true); spin->setValue(val); spin->blockSignals(false);
            Q_UNUSED(sync);
            restartAndApply();
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, s, sync](double val) {
            int sv = qRound(val / s.step);
            slider->blockSignals(true); slider->setValue(sv); slider->blockSignals(false);
            Q_UNUSED(sync);
            restartAndApply();
        });
        m_sliders << slider;
        m_spins << spin;
        row->addWidget(label);
        row->addWidget(slider, 1);
        row->addWidget(spin);
        lay->addLayout(row);
    }
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(btns->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_specs.size(); ++i) {
            m_sliders[i]->blockSignals(true);
            m_spins[i]->blockSignals(true);
            m_sliders[i]->setValue((int)(m_specs[i].def / m_specs[i].step));
            m_spins[i]->setValue(m_specs[i].def);
            m_sliders[i]->blockSignals(false);
            m_spins[i]->blockSignals(false);
        }
        restartAndApply();
    });
    lay->addWidget(btns);
    applyParams();
    refreshCanvas();
}

void AdjustDialog::applyParams() {
    QVector<double> vals;
    for (auto *s : m_spins) vals << s->value();
    m_apply(m_tab, vals);
}

// ================= HueSatDialog =================

HueSatDialog::HueSatDialog(QWidget *parent, EditorTab *tab)
    : PreviewDialog(parent, tab, QStringLiteral("Hue / Saturation")) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);
    auto mk = [&](const QString &name, int min, int max, int def, QSlider *&sl, QLabel *v) {
        auto *row = new QHBoxLayout;
        auto *l = new QLabel(name);
        l->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:110px;").arg(Theme::TextSecondary));
        sl = new QSlider(Qt::Horizontal);
        sl->setRange(min, max);
        sl->setValue(def);
        v = new QLabel(QString::number(def));
        v->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:34px;").arg(Theme::TextSecondary));
        connect(sl, &QSlider::valueChanged, this, [this, v](int val) {
            v->setNum(val);
            restartAndApply();
        });
        row->addWidget(l);
        row->addWidget(sl, 1);
        row->addWidget(v);
        lay->addLayout(row);
    };
    mk(tr("Hue"), -180, 180, 0, m_hue, m_hueV);
    mk(tr("Saturation"), -100, 100, 0, m_sat, m_satV);
    mk(tr("Lightness"), -100, 100, 0, m_light, m_lightV);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(btns->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this]() {
        for (auto s : {m_hue, m_sat, m_light}) { s->blockSignals(true); s->setValue(0); s->blockSignals(false); }
        m_hueV->setNum(0); m_satV->setNum(0); m_lightV->setNum(0);
        restartAndApply();
    });
    lay->addWidget(btns);
    applyParams();
    refreshCanvas();
}

void HueSatDialog::applyParams() {
    pf_adjust_hue_saturation(m_tab->m_doc.handle(), m_hue->value(), m_sat->value(), m_light->value());
}

// ================= LevelsDialog =================

LevelsDialog::LevelsDialog(QWidget *parent, EditorTab *tab)
    : PreviewDialog(parent, tab, QStringLiteral("Levels")) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);

    m_hist = new HistogramView;
    uint32_t bins[1024];
    pf_histogram(m_tab->m_doc.handle(), 0, bins, sizeof(bins));
    m_hist->setBins(bins);

    m_channel = new QComboBox;
    m_channel->addItems({tr("RGB"), tr("Red"), tr("Green"), tr("Blue")});

    auto mk = [&](const QString &name, int min, int max, int def, QSlider *&sl, QLabel *v) {
        auto *row = new QHBoxLayout;
        auto *l = new QLabel(name);
        l->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:110px;").arg(Theme::TextSecondary));
        sl = new QSlider(Qt::Horizontal);
        sl->setRange(min, max);
        sl->setValue(def);
        v = new QLabel(QString::number(def));
        v->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:34px;").arg(Theme::TextSecondary));
        connect(sl, &QSlider::valueChanged, this, [this, v](int val) { v->setNum(val); restartAndApply(); });
        row->addWidget(l);
        row->addWidget(sl, 1);
        row->addWidget(v);
        lay->addLayout(row);
    };
    mk(tr("Input Black"), 0, 254, 0, m_inBlack, new QLabel);
    mk(tr("Input White"), 1, 255, 255, m_inWhite, new QLabel);
    auto *gammaRow = new QHBoxLayout;
    auto *gl = new QLabel(tr("Gamma"));
    gl->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:110px;").arg(Theme::TextSecondary));
    m_gamma = new QSlider(Qt::Horizontal);
    m_gamma->setRange(10, 400);
    m_gamma->setValue(100);
    auto *gv = new QLabel("1.00");
    gv->setStyleSheet(QStringLiteral("color:%1;font-size:12px;min-width:34px;").arg(Theme::TextSecondary));
    connect(m_gamma, &QSlider::valueChanged, this, [this, gv](int val) {
        gv->setText(QString::number(val / 100.0, 'f', 2));
        restartAndApply();
    });
    gammaRow->addWidget(gl);
    gammaRow->addWidget(m_gamma, 1);
    gammaRow->addWidget(gv);

    mk(tr("Output Black"), 0, 255, 0, m_outBlack, new QLabel);
    mk(tr("Output White"), 0, 255, 255, m_outWhite, new QLabel);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(btns->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this]() {
        for (auto s : {m_inBlack, m_inWhite, m_outBlack, m_outWhite}) { s->blockSignals(true); s->setValue(s == m_inWhite || s == m_outWhite ? 255 : 0); s->blockSignals(false); }
        m_gamma->blockSignals(true); m_gamma->setValue(100); m_gamma->blockSignals(false);
        restartAndApply();
    });
    auto *chRow = new QHBoxLayout;
    chRow->addWidget(new QLabel(tr("Channel")));
    chRow->addWidget(m_channel);
    chRow->addStretch(1);
    connect(m_channel, &QComboBox::currentIndexChanged, this, [this]() { restartAndApply(); });

    lay->addWidget(m_hist);
    lay->addLayout(chRow);
    lay->addLayout(gammaRow);
    lay->addWidget(btns);
    // reorder: put sliders between histogram and buttons is already ok
    applyParams();
    refreshCanvas();
}

void LevelsDialog::applyParams() {
    pf_adjust_levels(m_tab->m_doc.handle(), m_inBlack->value(), m_inWhite->value(),
                     m_gamma->value() / 100.0, m_outBlack->value(), m_outWhite->value(),
                     m_channel->currentIndex());
}

// ================= CurvesDialog =================

CurvesDialog::CurvesDialog(QWidget *parent, EditorTab *tab)
    : PreviewDialog(parent, tab, QStringLiteral("Curves")) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);
    m_curve = new CurvesWidget;
    uint32_t bins[1024];
    pf_histogram(m_tab->m_doc.handle(), 0, bins, sizeof(bins));
    m_curve->setHistogram(bins + 768); // luma
    connect(m_curve, &CurvesWidget::curveChanged, this, [this]() { restartAndApply(); });
    m_channel = new QComboBox;
    m_channel->addItems({tr("RGB"), tr("Red"), tr("Green"), tr("Blue")});
    connect(m_channel, &QComboBox::currentIndexChanged, this, [this]() { restartAndApply(); });
    auto *chRow = new QHBoxLayout;
    chRow->addWidget(new QLabel(tr("Channel")));
    chRow->addWidget(m_channel);
    chRow->addStretch(1);
    auto *resetBtn = new QPushButton(tr("Reset Curve"));
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_curve->reset();
        restartAndApply();
    });
    chRow->addWidget(resetBtn);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(m_curve);
    lay->addLayout(chRow);
    lay->addWidget(btns);
    applyParams();
    refreshCanvas();
}

void CurvesDialog::applyParams() {
    QVector<QPointF> pts = m_curve->points();
    QVector<float> flat;
    for (const QPointF &p : pts) flat << (float)p.x() << (float)p.y();
    pf_adjust_curves(m_tab->m_doc.handle(), flat.constData(), flat.size() / 2, m_channel->currentIndex());
}

// ================= NewDocDialog =================

NewDocDialog::NewDocDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("New Document"));
    setMinimumWidth(340);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 16, 18, 14);
    lay->setSpacing(10);
    auto *form = new QFormLayout;
    auto *preset = new QComboBox;
    preset->addItems({tr("Custom"), tr("Fullscreen 1920 × 1080"), tr("Square 2048 × 2048"),
                      tr("A4 @150dpi 1240 × 1754"), tr("Post 1080 × 1350"), tr("Story 1080 × 1920"),
                      tr("Icon 512 × 512"), tr("Thumbnail 1280 × 720")});
    m_w = new QSpinBox; m_w->setRange(1, 16384); m_w->setValue(1280);
    m_h = new QSpinBox; m_h->setRange(1, 16384); m_h->setValue(800);
    m_fill = new QComboBox;
    m_fill->addItems({tr("Transparent"), tr("White"), tr("Black"), tr("Custom color…")});
    m_colorBtn = new QPushButton(tr("Choose…"));
    m_colorBtn->setEnabled(false);
    connect(m_fill, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_colorBtn->setEnabled(i == 3);
    });
    connect(m_colorBtn, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_custom, this, tr("Background color"));
        if (c.isValid()) m_custom = c;
    });
    form->addRow(tr("Preset"), preset);
    form->addRow(tr("Width"), m_w);
    form->addRow(tr("Height"), m_h);
    form->addRow(tr("Background"), m_fill);
    form->addRow(QString(), m_colorBtn);
    connect(preset, &QComboBox::currentIndexChanged, this, [this, preset](int i) {
        static const QList<QPair<int, int>> sizes = {
            {}, {1920, 1080}, {2048, 2048}, {1240, 1754}, {1080, 1350}, {1080, 1920}, {512, 512}, {1280, 720}};
        if (i > 0 && i < sizes.size()) { m_w->setValue(sizes[i].first); m_h->setValue(sizes[i].second); }
        Q_UNUSED(preset);
    });
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    btns->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addLayout(form);
    lay->addWidget(btns);
}

uint32_t NewDocDialog::width() const { return m_w->value(); }
uint32_t NewDocDialog::height() const { return m_h->value(); }
int NewDocDialog::fillMode() const { return m_fill->currentIndex(); }
QColor NewDocDialog::customColor() const { return m_custom; }

// ================= ImageSizeDialog =================

ImageSizeDialog::ImageSizeDialog(QWidget *parent, uint32_t w, uint32_t h) : QDialog(parent) {
    setWindowTitle(tr("Image Size"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 16, 18, 14);
    lay->setSpacing(10);
    auto *form = new QFormLayout;
    m_w = new QSpinBox; m_w->setRange(1, 16384); m_w->setValue(w);
    m_h = new QSpinBox; m_h->setRange(1, 16384); m_h->setValue(h);
    m_ratio = (double)w / (double)h;
    m_resample = new QComboBox;
    m_resample->addItems({tr("Nearest (hard pixels)"), tr("Bilinear"), tr("Bicubic (best)")});
    m_resample->setCurrentIndex(2);
    form->addRow(tr("Width"), m_w);
    form->addRow(tr("Height"), m_h);
    form->addRow(tr("Resample"), m_resample);
    auto *keep = new QCheckBox(tr("Constrain proportions"));
    keep->setChecked(true);
    connect(keep, &QCheckBox::toggled, this, [this](bool on) { m_sync = on; });
    connect(m_w, &QSpinBox::valueChanged, this, &ImageSizeDialog::onW);
    connect(m_h, &QSpinBox::valueChanged, this, &ImageSizeDialog::onH);
    auto *info = new QLabel(tr("Current: %1 × %2 px").arg(w).arg(h));
    info->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecondary));
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addLayout(form);
    lay->addWidget(keep);
    lay->addWidget(info);
    lay->addWidget(btns);
}

void ImageSizeDialog::onW(int v) {
    if (!m_sync) return;
    m_h->blockSignals(true);
    m_h->setValue(qMax(1, (int)(v / m_ratio)));
    m_h->blockSignals(false);
}
void ImageSizeDialog::onH(int v) {
    if (!m_sync) return;
    m_w->blockSignals(true);
    m_w->setValue(qMax(1, (int)(v * m_ratio)));
    m_w->blockSignals(false);
}
int ImageSizeDialog::resample() const { return m_resample->currentIndex(); }

// ================= CanvasSizeDialog =================

CanvasSizeDialog::CanvasSizeDialog(QWidget *parent, uint32_t w, uint32_t h) : QDialog(parent) {
    setWindowTitle(tr("Canvas Size"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 16, 18, 14);
    lay->setSpacing(10);
    auto *form = new QFormLayout;
    m_w = new QSpinBox; m_w->setRange(1, 16384); m_w->setValue(w);
    m_h = new QSpinBox; m_h->setRange(1, 16384); m_h->setValue(h);
    form->addRow(tr("Width"), m_w);
    form->addRow(tr("Height"), m_h);
    // 3x3 anchor grid
    auto *anchorBox = new QWidget;
    auto *grid = new QGridLayout(anchorBox);
    grid->setSpacing(4);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            auto *b = new QPushButton;
            b->setFixedSize(34, 34);
            b->setCheckable(true);
            int idx = r * 3 + c;
            connect(b, &QPushButton::clicked, this, [this, idx]() {
                m_anchor = idx;
                for (auto *bb : m_anchorBtns) bb->setChecked(false);
                m_anchorBtns[idx]->setChecked(true);
            });
            m_anchorBtns << b;
            grid->addWidget(b, r, c);
        }
    }
    m_anchorBtns[4]->setChecked(true);
    auto *anchorRow = new QHBoxLayout;
    anchorRow->addWidget(new QLabel(tr("Anchor")));
    anchorRow->addSpacing(12);
    anchorRow->addWidget(anchorBox);
    anchorRow->addStretch(1);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addLayout(form);
    lay->addLayout(anchorRow);
    lay->addWidget(btns);
}

int CanvasSizeDialog::anchor() const { return m_anchor; }

// ================= ExportDialog =================

ExportDialog::ExportDialog(QWidget *parent, uint32_t w, uint32_t h) : QDialog(parent), m_w(w), m_h(h) {
    setWindowTitle(tr("Export Image"));
    setMinimumWidth(400);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 16, 18, 14);
    lay->setSpacing(10);
    auto *form = new QFormLayout;
    m_format = new QComboBox;
    m_format->addItems({"PNG", "JPEG", "WebP (lossless)", "BMP", "TIFF", "GIF"});
    form->addRow(tr("Format"), m_format);
    m_quality = new QSlider(Qt::Horizontal);
    m_quality->setRange(1, 100);
    m_quality->setValue(92);
    m_qualityV = new QLabel("92");
    connect(m_quality, &QSlider::valueChanged, this, [this](int v) {
        m_qualityV->setNum(v);
        updateSizeInfo();
    });
    auto *qrow = new QHBoxLayout;
    qrow->addWidget(m_quality, 1);
    qrow->addWidget(m_qualityV);
    form->addRow(tr("Quality (JPEG)"), qrow);
    m_lossless = new QCheckBox(tr("Lossless"));
    m_lossless->setChecked(true);
    m_lossless->setEnabled(false); // image-webp encoder is lossless-only
    m_lossless->setToolTip(tr("The bundled WebP encoder always uses lossless VP8L."));
    form->addRow(tr("WebP mode"), m_lossless);
    m_scale = new QSpinBox;
    m_scale->setRange(1, 400);
    m_scale->setValue(100);
    m_scale->setSuffix(" %");
    connect(m_scale, &QSpinBox::valueChanged, this, [this]() { updateSizeInfo(); });
    form->addRow(tr("Scale"), m_scale);
    auto *bgRow = new QHBoxLayout;
    m_bgBtn = new QPushButton(tr("White"));
    connect(m_bgBtn, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_bg, this, tr("Matte color"));
        if (c.isValid()) {
            m_bg = c;
            m_bgBtn->setText(c.name().toUpper());
        }
    });
    bgRow->addWidget(m_bgBtn);
    bgRow->addStretch(1);
    auto *bgLbl = new QLabel(tr("Matte (formats without alpha)"));
    bgLbl->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecondary));
    form->addRow(bgLbl, bgRow);
    m_sizeInfo = new QLabel;
    m_sizeInfo->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecondary));
    form->addRow(tr("Output size"), m_sizeInfo);
    connect(m_format, &QComboBox::currentIndexChanged, this, &ExportDialog::onFormatChanged);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    btns->button(QDialogButtonBox::Ok)->setText(tr("Export"));
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addLayout(form);
    lay->addWidget(btns);
    onFormatChanged(0);
}

void ExportDialog::updateSizeInfo() {
    uint64_t ow = qMax<uint64_t>(1, (uint64_t)m_w * m_scale->value() / 100);
    uint64_t oh = qMax<uint64_t>(1, (uint64_t)m_h * m_scale->value() / 100);
    m_sizeInfo->setText(tr("%1 × %2 px").arg(ow).arg(oh));
}

void ExportDialog::onFormatChanged(int i) {
    bool jpeg = (i == 1); // JPEG
    m_quality->setEnabled(jpeg);
    m_qualityV->setEnabled(jpeg);
    updateSizeInfo();
}

int ExportDialog::format() const { return m_format->currentIndex(); }
int ExportDialog::quality() const { return m_quality->value(); }
bool ExportDialog::lossless() const { return true; }
int ExportDialog::scalePct() const { return m_scale->value(); }
QColor ExportDialog::background() const { return m_bg; }

// ================= TextDialog =================

TextDialog::TextDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Add Text"));
    setMinimumWidth(420);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 16, 18, 14);
    lay->setSpacing(10);
    m_font = new QFontComboBox;
    m_size = new QDoubleSpinBox;
    m_size->setRange(6, 999);
    m_size->setValue(72);
    m_bold = new QCheckBox(tr("Bold"));
    m_italic = new QCheckBox(tr("Italic"));
    auto *colorBtn = new QPushButton(tr("Color…"));
    connect(colorBtn, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_color, this, tr("Text color"));
        if (c.isValid()) { m_color = c; updatePreview(); }
    });
    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_font, 1);
    topRow->addWidget(m_size);
    topRow->addWidget(m_bold);
    topRow->addWidget(m_italic);
    topRow->addWidget(colorBtn);
    m_edit = new QPlainTextEdit;
    m_edit->setPlainText(tr("Your text"));
    m_edit->setFixedHeight(80);
    m_preview = new QLabel;
    m_preview->setMinimumHeight(56);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;border-radius:8px;").arg(Theme::CardBg, Theme::Border));
    auto apply = [this]() { updatePreview(); };
    connect(m_font, &QFontComboBox::currentFontChanged, this, apply);
    connect(m_size, &QDoubleSpinBox::valueChanged, this, apply);
    connect(m_bold, &QCheckBox::toggled, this, apply);
    connect(m_italic, &QCheckBox::toggled, this, apply);
    connect(m_edit, &QPlainTextEdit::textChanged, this, apply);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    btns->button(QDialogButtonBox::Ok)->setText(tr("Place"));
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addLayout(topRow);
    lay->addWidget(m_edit);
    lay->addWidget(m_preview);
    lay->addWidget(btns);
    updatePreview();
}

void TextDialog::updatePreview() {
    QImage img = renderedText();
    if (img.isNull()) { m_preview->clear(); return; }
    m_preview->setPixmap(QPixmap::fromImage(img).scaled(m_preview->width(), qMin(img.height(), 160),
                                                        Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QString TextDialog::text() const { return m_edit->toPlainText(); }

QImage TextDialog::renderedText() const {
    QString t = m_edit->toPlainText();
    if (t.isEmpty()) return QImage();
    QFont f = m_font->currentFont();
    f.setPointSizeF(m_size->value());
    f.setBold(m_bold->isChecked());
    f.setItalic(m_italic->isChecked());
    QFontMetricsF fm(f);
    QRectF br = fm.boundingRect(QRectF(0, 0, 100000, 100000), Qt::AlignLeft | Qt::AlignTop, t);
    QImage img(qMax(4, (int)br.width() + 20), qMax(4, (int)(br.height() + fm.descent() + 20)), QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(f);
    p.setPen(m_color);
    p.drawText(QPointF(10, 10 + fm.ascent()), t);
    p.end();
    return img;
}

void TextDialog::setColor(const QColor &c) {
    m_color = c;
    updatePreview();
}

// ================= ShortcutsDialog =================

ShortcutsDialog::ShortcutsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Keyboard Shortcuts"));
    setMinimumSize(520, 520);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_table->verticalHeader()->hide();
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    connect(btns, &QDialogButtonBox::accepted, this, [this]() { save(); accept(); });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(btns->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this]() {
        QSettings s;
        s.remove("shortcuts");
        load();
    });
    lay->addWidget(m_table, 1);
    lay->addWidget(btns);
    load();
}

void ShortcutsDialog::load() {
    // enumerate all QAction objects registered under MainWindow's name scheme
    auto actions = parentWidget()->findChildren<QAction *>();
    QVector<QPair<QString, QString>> rows;
    for (QAction *a : actions) {
        QString id = a->objectName();
        if (!id.startsWith("cmd_")) continue;
        QSettings s;
        QString ks = s.value("shortcuts/" + id, a->property("defaultShortcut").toString()).toString();
        rows.append({a->text().remove('&'), ks});
    }
    std::sort(rows.begin(), rows.end());
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        auto *nameItem = new QTableWidgetItem(rows[i].first);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 0, nameItem);
        auto *edit = new QKeySequenceEdit(QKeySequence(rows[i].second));
        m_table->setCellWidget(i, 1, edit);
        edit->setProperty("cmdId", rows[i].first);
    }
}

void ShortcutsDialog::save() {
    QSettings s;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        auto *edit = qobject_cast<QKeySequenceEdit *>(m_table->cellWidget(i, 1));
        if (!edit) continue;
        s.setValue("shortcuts/" + edit->property("cmdId").toString(),
                   edit->keySequence().toString());
    }
    if (auto *mw = qobject_cast<MainWindow *>(parentWidget()))
        mw->applyShortcuts();
}

// ================= AboutDialog =================

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("About PixelForge Studio"));
    setMinimumWidth(430);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 20, 24, 18);
    lay->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("<span style='font-size:22px;font-weight:700;color:#1F2937;'>PixelForge</span> "
                                             "<span style='font-size:22px;font-weight:300;color:#6B7280;'>Studio</span>"));
    title->setAlignment(Qt::AlignCenter);
    auto *ver = new QLabel(tr("Version 1.0.0 — Native desktop image editor"));
    ver->setAlignment(Qt::AlignCenter);
    ver->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecondary));
    auto *stack = new QLabel(
        tr("Core engine: <b>Rust</b> (pfengine, %1)<br>"
           "User interface: <b>Qt %2</b> (C++ / Qt Widgets)<br>"
           "Bridge: stable C ABI (%3 symbols)<br>"
           "GPU-independent parallel compositing via Rayon")
            .arg("multi-threaded")
            .arg(QString::fromLatin1(qVersion()))
            .arg(101));
    stack->setAlignment(Qt::AlignCenter);
    stack->setStyleSheet(QStringLiteral("color:%1;font-size:13px;line-height:170%;").arg(Theme::Text));
    auto *engine = new QLabel(EngineDoc::rustInfo());
    engine->setAlignment(Qt::AlignCenter);
    engine->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(Theme::TextSecondary));
    engine->setWordWrap(true);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    btns->button(QDialogButtonBox::Close)->setDefault(true);
    lay->addSpacing(8);
    lay->addWidget(title);
    lay->addWidget(ver);
    lay->addSpacing(10);
    lay->addWidget(stack);
    lay->addWidget(engine);
    lay->addSpacing(6);
    lay->addWidget(btns);
}
