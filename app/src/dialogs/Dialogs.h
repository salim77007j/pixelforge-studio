#pragma once
#include <QDialog>
#include <QVector>
#include <QPointF>
#include <QColor>
#include <QSpinBox>
#include <QDoubleSpinBox>

class EditorTab;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QFontComboBox;
class QPlainTextEdit;
class CurvesWidget;
struct Histogram;

// Base: dialogs that live-preview through the engine (pf_preview_*).
class PreviewDialog : public QDialog {
    Q_OBJECT
public:
    PreviewDialog(QWidget *parent, EditorTab *tab, const QString &title);
    ~PreviewDialog() override;
protected slots:
    virtual void restartAndApply();
    void accept() override;
    void reject() override;
protected:
    virtual void applyParams() = 0;
    virtual QString historyLabel() const = 0;
    void refreshCanvas();
    EditorTab *m_tab;
    bool m_committed = false;
};

// Generic multi-parameter adjustment dialog (sliders).
struct ParamSpec {
    QString name;
    double min, max, step, def;
    int decimals;
    bool isInt;
};
class AdjustDialog : public PreviewDialog {
    Q_OBJECT
public:
    AdjustDialog(QWidget *parent, EditorTab *tab, const QString &title,
                 const QVector<ParamSpec> &params,
                 std::function<void(EditorTab *, const QVector<double> &)> apply);
protected:
    void applyParams() override;
    QString historyLabel() const override { return m_label; }
private:
    QVector<ParamSpec> m_specs;
    QVector<QDoubleSpinBox *> m_spins;
    QVector<QSlider *> m_sliders;
    std::function<void(EditorTab *, const QVector<double> &)> m_apply;
    QString m_label;
};

// Hue/Saturation
class HueSatDialog : public PreviewDialog {
    Q_OBJECT
public:
    HueSatDialog(QWidget *parent, EditorTab *tab);
protected:
    void applyParams() override;
    QString historyLabel() const override { return "Hue/Saturation"; }
private:
    QSlider *m_hue, *m_sat, *m_light;
    QLabel *m_hueV, *m_satV, *m_lightV;
};

// Levels with histogram
class LevelsDialog : public PreviewDialog {
    Q_OBJECT
public:
    LevelsDialog(QWidget *parent, EditorTab *tab);
protected:
    void applyParams() override;
    QString historyLabel() const override { return "Levels"; }
private:
    QComboBox *m_channel;
    QSlider *m_inBlack, *m_inWhite, *m_gamma, *m_outBlack, *m_outWhite;
    class HistogramView *m_hist;
    void syncLabels();
};

// Curves
class CurvesDialog : public PreviewDialog {
    Q_OBJECT
public:
    CurvesDialog(QWidget *parent, EditorTab *tab);
protected:
    void applyParams() override;
    QString historyLabel() const override { return "Curves"; }
private:
    CurvesWidget *m_curve;
    QComboBox *m_channel;
};

// ---------- plain (non-preview) dialogs ----------

class NewDocDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewDocDialog(QWidget *parent);
    uint32_t width() const;
    uint32_t height() const;
    int fillMode() const; // 0 transparent 1 white 2 black 3 custom
    QColor customColor() const;
private:
    QSpinBox *m_w, *m_h;
    QComboBox *m_fill;
    QPushButton *m_colorBtn;
    QColor m_custom{255, 255, 255};
};

class ImageSizeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImageSizeDialog(QWidget *parent, uint32_t w, uint32_t h);
    uint32_t newWidth() const { return m_w->value(); }
    uint32_t newHeight() const { return m_h->value(); }
    int resample() const;
private slots:
    void onW(int v);
    void onH(int v);
private:
    QSpinBox *m_w, *m_h;
    QComboBox *m_resample;
    double m_ratio;
    bool m_sync = true;
};

class CanvasSizeDialog : public QDialog {
    Q_OBJECT
public:
    explicit CanvasSizeDialog(QWidget *parent, uint32_t w, uint32_t h);
    uint32_t newWidth() const { return m_w->value(); }
    uint32_t newHeight() const { return m_h->value(); }
    int anchor() const;
private:
    QSpinBox *m_w, *m_h;
    int m_anchor = 4;
    QVector<QPushButton *> m_anchorBtns;
};

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(QWidget *parent, uint32_t w, uint32_t h);
    int format() const;         // engine format id
    int quality() const;
    bool lossless() const;
    int scalePct() const;
    QColor background() const;
private slots:
    void onFormatChanged(int i);
    void updateSizeInfo();
private:
    QComboBox *m_format;
    QSlider *m_quality;
    QLabel *m_qualityV;
    QCheckBox *m_lossless;
    QSpinBox *m_scale;
    QPushButton *m_bgBtn;
    QLabel *m_sizeInfo;
    uint32_t m_w, m_h;
    QColor m_bg{255, 255, 255};
};

class TextDialog : public QDialog {
    Q_OBJECT
public:
    explicit TextDialog(QWidget *parent);
    QString text() const;
    QImage renderedText() const;
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
private:
    QFontComboBox *m_font;
    QDoubleSpinBox *m_size;
    QCheckBox *m_bold, *m_italic;
    QPlainTextEdit *m_edit;
    QLabel *m_preview;
    QColor m_color{30, 30, 30};
    void updatePreview();
};

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget *parent);
private:
    void load();
    void save();
    class QTableWidget *m_table;
};

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent);
};
