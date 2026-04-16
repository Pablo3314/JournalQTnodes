#pragma once

#include <QPointF>
#include <QVector>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QRectF>

struct Stroke {
    QVector<QPointF> points;
    QColor color = Qt::black;
    qreal width = 2.0;
    QRectF bounds;
    QPainterPath path;
};

class EraserTool
{
public:
    EraserTool();
    ~EraserTool() = default;

    void startErase(const QPointF &pos, qreal radius);
    void updateErase(const QPointF &pos, qreal radius);
    void endErase();

    void drawPreview(QPainter &p, const QPointF &pos, qreal radius) const;

    QVector<int> getStrokesToErase(const QVector<Stroke> &strokes, const QPointF &center, qreal radius) const;

    bool isActive() const { return m_active; }

private:
    bool m_active = false;
    QPointF m_currentPos;
    qreal m_currentRadius = 20.0;
};