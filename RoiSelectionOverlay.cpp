#include "RoiSelectionOverlay.h"
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QMouseEvent>
#include <algorithm>

RoiSelectionOverlay::RoiSelectionOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(false);
    setCursor(Qt::CrossCursor);
}

void RoiSelectionOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Draw the last confirmed ROI as a thin green guide
    if (m_lastRoi.isValid() && !m_lastRoi.isEmpty()) {
        QRectF guide(m_lastRoi.x() * width(),
                     m_lastRoi.y() * height(),
                     m_lastRoi.width() * width(),
                     m_lastRoi.height() * height());
        p.setPen(QPen(QColor(0, 200, 0, 160), 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(guide);
    }

    // Draw the active rubber-band selection
    if (m_selecting) {
        QRectF sel = QRectF(m_origin, m_current).normalized();
        p.setBrush(QColor(70, 130, 230, 50));
        p.setPen(QPen(QColor(70, 130, 230, 220), 2.0));
        p.drawRect(sel);

        // Crosshair at center
        QPointF center = sel.center();
        p.setPen(QPen(QColor(255, 255, 255, 180), 1.0, Qt::DotLine));
        p.drawLine(QPointF(sel.left(), center.y()), QPointF(sel.right(), center.y()));
        p.drawLine(QPointF(center.x(), sel.top()), QPointF(center.x(), sel.bottom()));
    }
}

void RoiSelectionOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_selecting = true;
        m_origin = event->position();
        m_current = m_origin;
        update();
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        m_lastRoi = QRectF();
        m_selecting = false;
        update();
        emit roiReset();
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void RoiSelectionOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting) {
        m_current = event->position();
        update();
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}

void RoiSelectionOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        m_current = event->position();

        QRectF sel = QRectF(m_origin, m_current).normalized();

        // Ignore tiny accidental clicks (< 10px)
        if (sel.width() < 10.0 || sel.height() < 10.0) {
            update();
            event->accept();
            return;
        }

        // Normalise to [0..1]
        qreal w = static_cast<qreal>(width());
        qreal h = static_cast<qreal>(height());
        QRectF norm(sel.x() / w, sel.y() / h,
                    sel.width() / w, sel.height() / h);
        norm = norm.intersected(QRectF(0.0, 0.0, 1.0, 1.0));

        m_lastRoi = norm;
        update();
        emit roiSelected(norm);
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}
