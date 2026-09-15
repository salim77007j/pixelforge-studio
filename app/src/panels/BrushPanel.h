#pragma once
#include <QWidget>

class MainWindow;
class QSlider;
class QSpinBox;
class QLabel;
class QCheckBox;
class QComboBox;

// Brush Settings panel: size, opacity, flow, hardness sliders + pressure dynamics + live stroke preview.
class BrushPanel : public QWidget {
    Q_OBJECT
public:
    explicit BrushPanel(MainWindow *mw, QWidget *parent = nullptr);
    void refresh();
    void applyPreset(int preset); // public: bottom-bar preset buttons use it
signals:
    void settingsChanged();
private slots:
    void onChanged();
private:
    MainWindow *m_mw;
    QSpinBox *m_size = nullptr;
    QSlider *m_opacity = nullptr;  QLabel *m_opacityV = nullptr;
    QSlider *m_flow = nullptr;     QLabel *m_flowV = nullptr;
    QSlider *m_hardness = nullptr; QLabel *m_hardnessV = nullptr;
    QSlider *m_spacing = nullptr;  QLabel *m_spacingV = nullptr;
    QCheckBox *m_pressureSize = nullptr;
    QCheckBox *m_pressureOpacity = nullptr;
    class StrokePreview *m_preview = nullptr;
};

// Renders a sample stroke with current brush settings (real engine math mirrored).
class StrokePreview : public QWidget {
    Q_OBJECT
public:
    explicit StrokePreview(MainWindow *mw, QWidget *parent = nullptr);
    void regenerate();
protected:
    void paintEvent(QPaintEvent *) override;
private:
    MainWindow *m_mw;
    QImage m_stroke;
};
