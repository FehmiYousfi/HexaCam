#pragma once

#include <QWidget>
#include <QRectF>
#include <QPointF>

class RoiSelectionOverlay : public QWidget {
    Q_OBJECT
public:
    explicit RoiSelectionOverlay(QWidget *parent = nullptr);

signals:
    void roiSelected(const QRectF &normalizedRect);
    void roiReset();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool      m_selecting = false;
    QPointF   m_origin;
    QPointF   m_current;
    QRectF    m_lastRoi;
};
