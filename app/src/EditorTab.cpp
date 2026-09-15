#include "EditorTab.h"
#include "MainWindow.h"
#include <QVBoxLayout>
#include <QFileInfo>

EditorTab::EditorTab(MainWindow *mw, PFDoc doc, const QString &fallbackTitle, QWidget *parent)
    : QWidget(parent), m_mw(mw), m_doc(doc), m_fallbackTitle(fallbackTitle) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_canvas = new CanvasView(this, this);
    lay->addWidget(m_canvas);
    connect(m_canvas, &CanvasView::interactionFinished, this, [this]() {
        emit editOccurred(this);
        m_mw->refreshAfterEdit(this, true);
    });
    connect(m_canvas, &CanvasView::colorPicked, m_mw, &MainWindow::applyPickedColor);
    connect(m_canvas, &CanvasView::cursorMoved, m_mw, &MainWindow::showCursorPos);
    connect(m_canvas, &CanvasView::zoomChanged, m_mw, &MainWindow::showZoom);
    connect(m_canvas, &CanvasView::statusMessage, m_mw, &MainWindow::showStatus);
    m_canvas->fitToWindow();
}

EditorTab::~EditorTab() = default;

QString EditorTab::title() const {
    QString base = m_fallbackTitle;
    QString p = m_doc.path();
    if (!p.isEmpty()) base = QFileInfo(p).fileName();
    return base;
}

bool EditorTab::isModified() const { return m_doc.modified(); }

void EditorTab::markSaved() {
    m_doc.markSaved();
    refreshTitle();
}

const QVector<float> &EditorTab::selectionOutline() {
    if (!m_antsValid) {
        m_antsCache.clear();
        int count = 0;
        const float *segs = pf_selection_outline(m_doc.handle(), &count);
        if (segs && count > 0)
            m_antsCache = QVector<float>(segs, segs + count * 4);
        m_antsValid = true;
    }
    return m_antsCache;
}

void EditorTab::afterEdit() {
    m_antsValid = false;
    m_canvas->refreshComposite();
    refreshTitle();
    m_mw->refreshAfterEdit(this, false);
}

void EditorTab::afterStructureEdit() {
    m_antsValid = false;
    m_canvas->refreshComposite();
    refreshTitle();
    m_mw->refreshAfterEdit(this, true);
}

void EditorTab::refreshTitle() { emit titleChanged(this); }
