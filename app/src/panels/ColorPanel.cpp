#include "ColorPanel.h"
#include "../MainWindow.h"
#include "../Theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QtMath>
#include <algorithm>

// ---------------- ColorWheel ----------------

ColorWheel::ColorWheel(QWidget *parent) : QWidget(parent) {
    setMinimumSize(180, 180);
    setMouseTracking(true);
    m_color = QColor::fromHsvF(m_hue, m_sat, m_val);
}

QRectF ColorWheel::m_squareRect() const {
    qreal s = qMin(width(), height()) * 0.36;
    return QRectF((width() - s) / 2, (height() - s) / 2, s, s);
}

qreal ColorWheel::m_ringRadius() const {
    return qMin(width(), height()) / 2.0 - 2;
}

void ColorWheel::resizeEvent(QResizeEvent *) {
    // rebuild hue ring cache
    int side = qMin(width(), height());
    m_ring = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
    m_ring.fill(Qt::transparent);
    QPainter rp(&m_ring);
    rp.setRenderHint(QPainter::Antialiasing);
    rp.translate(side / 2.0, side / 2.0);
    for (int a = 0; a < 360; a += 2) {
        for (int r = 0; r < 2; ++r) {
            QColor c = QColor::fromHsvF((a + r) / 360.0, 1.0, 1.0);
            rp.setPen(QPen(c, 2.4));
            rp.drawArc(QRectF(-m_ringRadius() + 4 + r * 1.2, -m_ringRadius() + 4 + r * 1.2,
                              (m_ringRadius() - 4) * 2 - r * 2.4, (m_ringRadius() - 4) * 2 - r * 2.4),
                       -(a + r) * 16, -2 * 16);
        }
    }
    rp.end();
    // rebuild square cache
    m_square = QImage(m_squareRect().size().toSize(), QImage::Format_ARGB32_Premultiplied);
    QPainter sp(&m_square);
    int w = m_square.width(), h = m_square.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            QColor c = QColor::fromHsvF(m_hue, x / qreal(w - 1), 1.0 - y / qreal(h - 1));
            m_square.setPixel(x, y, c.rgba());
        }
    }
    sp.end();
    update();
}

void ColorWheel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // ring
    p.drawImage((width() - m_ring.width()) / 2, (height() - m_ring.height()) / 2, m_ring);
    // square
    p.drawImage(m_squareRect().topLeft(), m_square);
    // ring handle
    QPointF c(width() / 2.0, height() / 2.0);
    qreal rr = m_ringRadius() - 7;
    QPointF h = c + QPointF(rr * cos((m_hue * 360.0 - 90) * M_PI / 180.0),
                            rr * sin((m_hue * 360.0 - 90) * M_PI / 180.0));
    p.setPen(QPen(QColor("#FFFFFF"), 2.4));
    p.setBrush(QColor::fromHsvF(m_hue, 1, 1));
    p.drawEllipse(h, 7, 7);
    // square handle
    QRectF sq = m_squareRect();
    QPointF sp(sq.left() + m_sat * sq.width(), sq.top() + (1.0 - m_val) * sq.height());
    p.setBrush(m_color);
    p.drawEllipse(sp, 7, 7);
    p.setPen(QPen(QColor("#FFFFFF"), 2));
    p.drawEllipse(sp, 8.5, 8.5);
}

void ColorWheel::pickAt(const QPointF &pos) {
    QPointF c(width() / 2.0, height() / 2.0);
    qreal dist = QLineF(c, pos).length();
    qreal rr = m_ringRadius();
    QRectF sq = m_squareRect();
    if (m_onRing || (!sq.contains(pos) && dist > rr * 0.55)) {
        m_onRing = true;
        qreal angle = QLineF(c, pos).angle(); // degrees, 0 = right, CCW
        m_hue = fmod(angle + 360.0, 360.0) / 360.0;
    } else if (sq.contains(pos) || m_onSquare) {
        m_onSquare = true;
        m_sat = qBound(0.0, (pos.x() - sq.left()) / sq.width(), 1.0);
        m_val = qBound(0.0, 1.0 - (pos.y() - sq.top()) / sq.height(), 1.0);
    }
    // rebuild square on hue change
    if (!m_square.isNull()) {
        QPainter sp(&m_square);
        int w = m_square.width(), h = m_square.height();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                m_square.setPixel(x, y, QColor::fromHsvF(m_hue, x / qreal(w - 1), 1.0 - y / qreal(h - 1)).rgba());
        sp.end();
    }
    m_color = QColor::fromHsvF(m_hue, m_sat, m_val);
    emit colorChanged(m_color);
    update();
}

void ColorWheel::mousePressEvent(QMouseEvent *e) {
    m_onRing = m_onSquare = false;
    pickAt(e->position());
}

void ColorWheel::mouseMoveEvent(QMouseEvent *e) { pickAt(e->position()); }

void ColorWheel::setColor(const QColor &c) {
    if (c == m_color) return;
    m_color = c;
    m_hue = c.hsvHueF() < 0 ? m_hue : c.hsvHueF();
    m_sat = c.hsvSaturationF();
    m_val = c.valueF();
    if (!m_square.isNull()) {
        QPainter sp(&m_square);
        int w = m_square.width(), h = m_square.height();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                m_square.setPixel(x, y, QColor::fromHsvF(m_hue, x / qreal(w - 1), 1.0 - y / qreal(h - 1)).rgba());
        sp.end();
    }
    update();
}

// ---------------- SwatchGrid ----------------

SwatchGrid::SwatchGrid(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(52);
    // default palette
    QVector<QColor> def = {
        QColor("#4A90E2"), QColor("#2F80ED"), QColor("#56CCF2"), QColor("#219653"), QColor("#27AE60"),
        QColor("#F2994A"), QColor("#F2C94C"), QColor("#EB5757"), QColor("#BB6BD9"), QColor("#9B51E0"),
        QColor("#828FA3"), QColor("#3A4250"), QColor("#6B7280"), QColor("#B3BAC4"), QColor("#E5E7EB"),
        QColor("#FFFFFF"), QColor("#000000"), QColor("#1F2937"),
    };
    m_colors = def;
}

void SwatchGrid::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int cs = 17; // cell size
    int gap = 4;
    int x = 0, y = 0;
    int maxW = width();
    for (int i = 0; i < m_colors.size(); ++i) {
        if (x + cs > maxW) { x = 0; y += cs + gap; }
        QRectF r(x, y, cs, cs);
        p.setPen(QPen(QColor(Theme::Border), 1));
        p.setBrush(m_colors[i]);
        p.drawRoundedRect(r, 3, 3);
        x += cs + gap;
    }
    // "+" cell
    if (x + cs > maxW) { x = 0; y += cs + gap; }
    QRectF r(x, y, cs, cs);
    p.setPen(QPen(QColor(Theme::TextSecondary), 1.2));
    p.setBrush(QColor(Theme::CardBg));
    p.drawRoundedRect(r, 3, 3);
    p.drawLine(r.center() - QPointF(4, 0), r.center() + QPointF(4, 0));
    p.drawLine(r.center() - QPointF(0, 4), r.center() + QPointF(0, 4));
    m_cols = qMax(1, maxW / (cs + gap));
    setMinimumHeight(y + cs + 2);
}

void SwatchGrid::mousePressEvent(QMouseEvent *e) {
    int cs = 17, gap = 4;
    int col = (int)(e->position().x()) / (cs + gap);
    int row = (int)(e->position().y()) / (cs + gap);
    int idx = row * m_cols + col;
    if (idx >= 0 && idx < m_colors.size()) {
        emit colorPicked(m_colors[idx]);
    } else if (idx == m_colors.size()) {
        // "+" clicked — handled by parent via colorPicked of current? no-op here
    }
}

void SwatchGrid::addColor(const QColor &c) {
    if (m_colors.contains(c)) return;
    m_colors.prepend(c);
    if (m_colors.size() > 48) m_colors.removeLast();
    update();
}

void SwatchGrid::setColors(const QVector<QColor> &cs) {
    m_colors = cs;
    update();
}

// FG/BG swatch pair widget (design reference: bottom of tool strip / color panel)
class FgBgWidget : public QWidget {
    Q_OBJECT
public:
    FgBgWidget(MainWindow *m) : mw(m) { setMinimumSize(44, 44); }
    MainWindow *mw;
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF bg(12, 12, 26, 26);
        QRectF fg(4, 4, 26, 26);
        p.setPen(QPen(QColor("#B8C0CB"), 1));
        p.setBrush(mw->bgColor());
        p.drawRoundedRect(bg, 4, 4);
        p.setBrush(mw->fgColor());
        p.drawRoundedRect(fg, 4, 4);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && fgRect().contains(e->position())) emit picked(false);
        else if (e->button() == Qt::LeftButton && bgRect().contains(e->position())) emit picked(true);
        else if (e->button() == Qt::RightButton) emit swap();
    }
    QRectF fgRect() const { return QRectF(4, 4, 26, 26); }
    QRectF bgRect() const { return QRectF(12, 12, 26, 26); }
signals:
    void picked(bool bg);
    void swap();
};

// ---------------- ColorPanel ----------------

ColorPanel::ColorPanel(MainWindow *mw, QWidget *parent) : QWidget(parent), m_mw(mw) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(8);

    m_wheel = new ColorWheel;
    connect(m_wheel, &ColorWheel::colorChanged, this, &ColorPanel::onFgChanged);

    m_swatches = new SwatchGrid;
    connect(m_swatches, &SwatchGrid::colorPicked, m_mw, &MainWindow::setFgColor);

    auto *hexRow = new QHBoxLayout;
    auto *hexLabel = new QLabel(tr("HEX"));
    hexLabel->setStyleSheet(QStringLiteral("color:%1;font-size:11px;font-weight:600;").arg(Theme::TextSecondary));
    m_hex = new QLineEdit(m_wheel->color().name().toUpper());
    m_hex->setFixedWidth(90);
    connect(m_hex, &QLineEdit::editingFinished, this, &ColorPanel::onHexEdited);

    auto *miniRow = new QHBoxLayout;
    auto *swapBtn = new QToolButton;
    swapBtn->setText(QStringLiteral("⇄"));
    swapBtn->setToolTip(tr("Swap FG/BG (X)"));
    swapBtn->setStyleSheet("QToolButton{font-size:15px;border:none;color:#3A4250;}");
    connect(swapBtn, &QToolButton::clicked, this, &ColorPanel::swapFgBg);
    auto *resetBtn = new QToolButton;
    resetBtn->setText(QStringLiteral("⌂"));
    resetBtn->setToolTip(tr("Default colors (D)"));
    resetBtn->setStyleSheet("QToolButton{font-size:15px;border:none;color:#3A4250;}");
    connect(resetBtn, &QToolButton::clicked, this, &ColorPanel::resetFgBg);

    auto *fgbg = new FgBgWidget(m_mw);
    connect(fgbg, &FgBgWidget::picked, m_mw, [this](bool bg) {
        if (bg) { /* pick into bg: use wheel color */ m_mw->setBgColor(m_wheel->color()); }
        else m_mw->setFgColor(m_wheel->color());
    });
    connect(fgbg, &FgBgWidget::swap, this, &ColorPanel::swapFgBg);
    m_fgBgWidget = fgbg;

    miniRow->addWidget(fgbg);
    miniRow->addWidget(swapBtn);
    miniRow->addWidget(resetBtn);
    miniRow->addStretch(1);
    miniRow->addWidget(hexLabel);
    miniRow->addWidget(m_hex);

    auto *palRow = new QHBoxLayout;
    auto *saveBtn = new QPushButton(tr("Save"));
    auto *loadBtn = new QPushButton(tr("Load"));
    saveBtn->setFixedHeight(26);
    loadBtn->setFixedHeight(26);
    connect(saveBtn, &QPushButton::clicked, this, &ColorPanel::savePalette);
    connect(loadBtn, &QPushButton::clicked, this, &ColorPanel::loadPalette);
    palRow->addWidget(saveBtn);
    palRow->addWidget(loadBtn);
    palRow->addStretch(1);

    lay->addWidget(m_wheel, 1);
    lay->addWidget(m_swatches);
    lay->addLayout(miniRow);
    lay->addLayout(palRow);
}

void ColorPanel::refresh() {
    // sync wheel to FG
    m_wheel->setColor(m_mw->fgColor());
    m_hex->setText(m_mw->fgColor().name().toUpper());
}

void ColorPanel::onFgChanged(const QColor &c) {
    m_mw->setFgColor(c);
    m_hex->setText(c.name().toUpper());
}

void ColorPanel::onHexEdited() {
    QColor c(m_hex->text());
    if (c.isValid()) {
        m_wheel->setColor(c);
        m_mw->setFgColor(c);
    } else {
        m_hex->setText(m_wheel->color().name().toUpper());
    }
}

void ColorPanel::swapFgBg() { m_mw->swapColors(); }
void ColorPanel::resetFgBg() { m_mw->resetColors(); }

void ColorPanel::savePalette() {
    QString f = QFileDialog::getSaveFileName(this, tr("Save Palette"), QString(), "*.json");
    if (f.isEmpty()) return;
    QJsonArray arr;
    for (const QColor &c : m_swatches->colors()) arr.append(c.name());
    QFile file(f);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson());
        file.close();
    }
}

void ColorPanel::loadPalette() {
    QString f = QFileDialog::getOpenFileName(this, tr("Load Palette"), QString(), "*.json");
    if (f.isEmpty()) return;
    QFile file(f);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonArray arr = QJsonDocument::fromJson(file.readAll()).array();
        QVector<QColor> cs;
        for (const QJsonValue &v : arr) cs.append(QColor(v.toString()));
        m_swatches->setColors(cs);
        file.close();
    }
}

#include "ColorPanel.moc"
