#pragma once
#include <QWidget>
#include <QColor>

class MainWindow;
class QSlider;
class QSpinBox;
class QLabel;
class QToolButton;
class QLineEdit;
class QGridLayout;

// Hue ring + SV square picker with swatch grid and HEX input (design reference).
class ColorWheel : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheel(QWidget *parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);

signals:
    void colorChanged(const QColor &c);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void pickAt(const QPointF &p);
    QImage m_ring;      // cached hue ring
    QImage m_square;    // cached SV square for current hue
    QColor m_color = QColor("#4A90E2");
    qreal m_hue = 210.0 / 360.0; // hue stored as 0..1 fraction
    qreal m_sat = 0.65;
    qreal m_val = 0.89;
    bool m_onRing = false;
    bool m_onSquare = false;
    QRectF m_squareRect() const;
    qreal m_ringRadius() const;
};

class SwatchGrid : public QWidget {
    Q_OBJECT
public:
    explicit SwatchGrid(QWidget *parent = nullptr);
    void addColor(const QColor &c);
    QVector<QColor> colors() const { return m_colors; }
    void setColors(const QVector<QColor> &cs);
signals:
    void colorPicked(const QColor &c);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
private:
    QVector<QColor> m_colors;
    int m_cols = 12;
};

// Full color panel: wheel + swatches + hex + FG/BG pairs
class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(MainWindow *mw, QWidget *parent = nullptr);
    void refresh();
private slots:
    void onFgChanged(const QColor &c);
    void onHexEdited();
    void swapFgBg();
    void resetFgBg();
    void savePalette();
    void loadPalette();
private:
    MainWindow *m_mw;
    ColorWheel *m_wheel = nullptr;
    SwatchGrid *m_swatches = nullptr;
    QLineEdit *m_hex = nullptr;
    QWidget *m_fgBgWidget = nullptr;
};
