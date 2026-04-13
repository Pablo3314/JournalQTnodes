#pragma once

#include <QWidget>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QPainterPath>

class QTimer;

class CanvasWidget : public QWidget
{
    Q_OBJECT

private:
    QPixmap m_cache;
    QRectF m_cacheWorldBounds;
    qreal m_cacheZoom = 0.0;
    QPointF m_cachePan;
    bool m_cacheValid = false;

public:
    explicit CanvasWidget(QWidget *parent = nullptr);

    bool loadProject(const QString &folderPath);
    bool saveMeta() const;
    void clearAll();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct Stroke {
        QVector<QPointF> points;
        QColor color = Qt::black;
        qreal width = 2.0;
        QRectF bounds;
        QPainterPath path;
    };

    QVector<Stroke> m_strokes;
    Stroke m_currentStroke;

    bool m_drawing = false;
    bool m_panning = false;

    QPointF m_pan {0.0, 0.0};
    qreal m_zoom = 1.0;

    QPointF m_lastMousePos;

    QString m_projectFolder;
    int m_nextStrokeId = 1;
    QTimer *m_metaSaveTimer = nullptr;

    static constexpr qreal GRID_SIZE = 1200.0;
    QHash<qint64, QVector<int>> m_gridIndex;

    static qint64 cellKey(int cx, int cy);

    QPointF screenToWorld(const QPointF &screen) const;
    QRectF computeStrokeBounds(const Stroke &stroke) const;

    void rebuildIndex();
    void addStrokeToIndex(int strokeIndex);
    QVector<int> queryCandidateStrokes(const QRectF &visibleWorld) const;

    void appendPointToCurrentStroke(const QPointF &worldPoint);
    void finishCurrentStroke();
    void scheduleMetaSave();
    QPainterPath buildSmoothPath(const QVector<QPointF> &points) const;

    void saveStrokeFile(int strokeId, const Stroke &stroke) const;
    bool loadStrokeFile(const QString &filePath, Stroke &stroke) const;

    QString defaultProjectFolder() const;
    QString strokeFilePath(int strokeId) const;

    void invalidateCache();
    bool isCacheValid(const QRectF &visibleWorld) const;
    void renderStrokes(QPainter &p, const QRectF &visibleWorld);
};
