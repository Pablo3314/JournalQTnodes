#include "eraser.h"
#include "canvaswidget.h"  // For Stroke struct

EraserTool::EraserTool()
{
}

void EraserTool::startErase(const QPointF &pos, qreal radius)
{
    m_active = true;
    m_currentPos = pos;
    m_currentRadius = radius;
}

void EraserTool::updateErase(const QPointF &pos, qreal radius)
{
    m_currentPos = pos;
    m_currentRadius = radius;
}

void EraserTool::endErase()
{
    m_active = false;
}

void EraserTool::drawPreview(QPainter &p, const QPointF &pos, qreal radius) const
{
    if (!m_active) return;

    p.setPen(QPen(Qt::white, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(pos, radius, radius);
}

QVector<int> EraserTool::getStrokesToErase(const QVector<Stroke> &strokes, const QPointF &center, qreal radius) const
{
    QVector<int> toRemove;
    for (int i = 0; i < strokes.size(); ++i) {
        const Stroke &s = strokes[i];
        bool intersects = false;
        for (const QPointF &p : s.points) {
            const QPointF diff = p - center;
            if (diff.x() * diff.x() + diff.y() * diff.y() <= radius * radius) {
                intersects = true;
                break;
            }
        }
        if (intersects) {
            toRemove.append(i);
        }
    }
    return toRemove;
}