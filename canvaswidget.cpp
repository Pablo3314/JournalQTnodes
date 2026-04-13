#include "canvaswidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTabletEvent>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDirIterator>
#include <QFileInfo>
#include <QTimer>
#include <QtMath>
#include <algorithm>

static QPointF mousePosFromMouseEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

static QPointF mousePosFromWheelEvent(QWheelEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->posF();
#endif
}

static QPointF posFromTabletEvent(QTabletEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->posF();
#endif
}

CanvasWidget::CanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);

    m_projectFolder = defaultProjectFolder();
    QDir().mkpath(m_projectFolder + "/strokes");

    m_metaSaveTimer = new QTimer(this);
    m_metaSaveTimer->setSingleShot(true);
    connect(m_metaSaveTimer, &QTimer::timeout, this, [this]() {
        saveMeta();
    });

    QFile metaFile(m_projectFolder + "/meta.json");
    if (metaFile.exists()) {
        loadProject(m_projectFolder);
    } else {
        saveMeta();
    }
}

QString CanvasWidget::defaultProjectFolder() const
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return docs + "/InfiniteCanvasProject";
}

QString CanvasWidget::strokeFilePath(int strokeId) const
{
    const int bucket = strokeId / 1000;
    return QString("%1/strokes/%2/stroke_%3.json")
        .arg(m_projectFolder)
        .arg(bucket, 4, 10, QChar('0'))
        .arg(strokeId, 6, 10, QChar('0'));
}

qint64 CanvasWidget::cellKey(int cx, int cy)
{
    return (static_cast<qint64>(cx) << 32) ^ static_cast<quint32>(cy);
}

QPointF CanvasWidget::screenToWorld(const QPointF &screen) const
{
    return QPointF((screen.x() - m_pan.x()) / m_zoom,
                   (screen.y() - m_pan.y()) / m_zoom);
}

void CanvasWidget::invalidateCache()
{
    m_cacheValid = false;
    m_cache = QPixmap();
}

bool CanvasWidget::isCacheValid(const QRectF &visibleWorld) const
{
    Q_UNUSED(visibleWorld);
    if (!m_cacheValid || m_cache.isNull()) {
        return false;
    }
    // Cache is valid if zoom and pan haven't changed
    return (qAbs(m_cacheZoom - m_zoom) < 0.001) && 
           (qAbs(m_cachePan.x() - m_pan.x()) < 0.5) &&
           (qAbs(m_cachePan.y() - m_pan.y()) < 0.5);
}

void CanvasWidget::renderStrokes(QPainter &p, const QRectF &visibleWorld)
{
    const QVector<int> candidates = queryCandidateStrokes(visibleWorld);
    QTransform worldToScreen;
    worldToScreen.translate(m_pan.x(), m_pan.y());
    worldToScreen.scale(m_zoom, m_zoom);

    auto drawStroke = [&p, &worldToScreen](const Stroke &s) {
        if (s.points.size() < 2 || s.path.isEmpty()) {
            return;
        }

        QPen pen(s.color);
        pen.setWidthF(s.width);
        pen.setCosmetic(false);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.drawPath(worldToScreen.map(s.path));
    };

    for (int idx : candidates) {
        if (idx < 0 || idx >= m_strokes.size()) {
            continue;
        }

        const Stroke &s = m_strokes[idx];
        if (s.bounds.intersects(visibleWorld)) {
            drawStroke(s);
        }
    }
}

bool CanvasWidget::isEraserButton(Qt::MouseButton button)
{
    return button == Qt::ExtraButton1 || button == Qt::BackButton || button == Qt::ForwardButton;
}

bool CanvasWidget::isAnyEraserButtonPressed(Qt::MouseButtons buttons)
{
    return buttons.testFlag(Qt::ExtraButton1) || buttons.testFlag(Qt::BackButton) || buttons.testFlag(Qt::ForwardButton);
}

bool CanvasWidget::isEraserMode() const
{
    return m_eraserKeyHeld || m_eraserButtonHeld;
}

qreal CanvasWidget::currentEraserRadiusPixels() const
{
    const qreal clampedPressure = qBound<qreal>(0.0, m_lastTabletPressure, 1.0);
    return 10.0 + (30.0 * clampedPressure);
}

QVector<CanvasWidget::Stroke> CanvasWidget::eraseStrokeSegments(const Stroke &stroke,
                                                                const QPointF &center,
                                                                qreal radiusWorld,
                                                                bool &changed) const
{
    QVector<Stroke> segments;
    if (stroke.points.size() < 2) {
        changed = false;
        segments.append(stroke);
        return segments;
    }

    const qreal r2 = radiusWorld * radiusWorld;
    auto isOutside = [&](const QPointF &p) {
        const qreal dx = p.x() - center.x();
        const qreal dy = p.y() - center.y();
        return (dx * dx + dy * dy) > r2;
    };

    QVector<QPointF> current;
    bool removedAnyPoint = false;

    for (const QPointF &p : stroke.points) {
        if (isOutside(p)) {
            current.append(p);
            continue;
        }

        removedAnyPoint = true;
        if (current.size() >= 2) {
            Stroke piece;
            piece.color = stroke.color;
            piece.width = stroke.width;
            piece.points = current;
            piece.bounds = computeStrokeBounds(piece);
            piece.path = buildSmoothPath(piece.points);
            segments.append(piece);
        }
        current.clear();
    }

    if (current.size() >= 2) {
        Stroke piece;
        piece.color = stroke.color;
        piece.width = stroke.width;
        piece.points = current;
        piece.bounds = computeStrokeBounds(piece);
        piece.path = buildSmoothPath(piece.points);
        segments.append(piece);
    }

    changed = removedAnyPoint;
    if (!changed) {
        segments.clear();
        segments.append(stroke);
    }
    return segments;
}

bool CanvasWidget::eraseAt(const QPointF &worldCenter, qreal radiusWorld)
{
    const QRectF eraserRect(worldCenter.x() - radiusWorld,
                            worldCenter.y() - radiusWorld,
                            radiusWorld * 2.0,
                            radiusWorld * 2.0);

    const QVector<int> candidates = queryCandidateStrokes(eraserRect.adjusted(-radiusWorld, -radiusWorld,
                                                                               radiusWorld, radiusWorld));
    if (candidates.isEmpty()) {
        return false;
    }

    QSet<int> candidateSet;
    for (int idx : candidates) {
        candidateSet.insert(idx);
    }
    QVector<Stroke> rebuilt;
    rebuilt.reserve(m_strokes.size());

    bool changedAny = false;

    for (int i = 0; i < m_strokes.size(); ++i) {
        const Stroke &stroke = m_strokes[i];
        if (!candidateSet.contains(i) || !stroke.bounds.intersects(eraserRect)) {
            rebuilt.append(stroke);
            continue;
        }

        bool changed = false;
        QVector<Stroke> pieces = eraseStrokeSegments(stroke, worldCenter, radiusWorld, changed);
        if (changed) {
            changedAny = true;
            for (const Stroke &piece : pieces) {
                rebuilt.append(piece);
            }
        } else {
            rebuilt.append(stroke);
        }
    }

    if (!changedAny) {
        return false;
    }

    m_strokes = std::move(rebuilt);
    rebuildIndex();
    invalidateCache();
    return true;
}

void CanvasWidget::rewriteAllStrokeFiles()
{
    QDir strokesRoot(m_projectFolder + "/strokes");
    if (strokesRoot.exists()) {
        strokesRoot.removeRecursively();
    }
    QDir().mkpath(m_projectFolder + "/strokes");

    m_nextStrokeId = 1;
    for (const Stroke &stroke : m_strokes) {
        saveStrokeFile(m_nextStrokeId++, stroke);
    }
    saveMeta();
}

void CanvasWidget::clearAll()
{
    m_strokes.clear();
    m_gridIndex.clear();
    m_currentStroke = Stroke{};
    m_drawing = false;
    m_panning = false;
    m_nextStrokeId = 1;
    m_lastStrokeUpdateWorldRect = QRectF();
    invalidateCache();
    update();
}

QPainterPath CanvasWidget::buildSmoothPath(const QVector<QPointF> &points) const
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }

    path.moveTo(points.first());
    if (points.size() == 1) {
        return path;
    }
    if (points.size() == 2) {
        path.lineTo(points.last());
        return path;
    }

    for (int i = 1; i < points.size() - 1; ++i) {
        const QPointF mid = (points[i] + points[i + 1]) * 0.5;
        path.quadTo(points[i], mid);
    }
    path.lineTo(points.last());
    return path;
}

QRectF CanvasWidget::computeStrokeBounds(const Stroke &stroke) const
{
    if (stroke.points.isEmpty()) {
        return QRectF();
    }

    QRectF r(stroke.points.first(), QSizeF(0, 0));
    for (const QPointF &p : stroke.points) {
        r = r.united(QRectF(p, QSizeF(0, 0)));
    }

    const qreal margin = qMax<qreal>(8.0, stroke.width * 3.0);
    return r.adjusted(-margin, -margin, margin, margin);
}

void CanvasWidget::addStrokeToIndex(int strokeIndex)
{
    if (strokeIndex < 0 || strokeIndex >= m_strokes.size()) {
        return;
    }

    const QRectF b = m_strokes[strokeIndex].bounds;
    const int x0 = qFloor(b.left() / GRID_SIZE);
    const int x1 = qFloor(b.right() / GRID_SIZE);
    const int y0 = qFloor(b.top() / GRID_SIZE);
    const int y1 = qFloor(b.bottom() / GRID_SIZE);

    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            m_gridIndex[cellKey(cx, cy)].append(strokeIndex);
        }
    }
}

void CanvasWidget::rebuildIndex()
{
    m_gridIndex.clear();
    for (int i = 0; i < m_strokes.size(); ++i) {
        addStrokeToIndex(i);
    }
}

QVector<int> CanvasWidget::queryCandidateStrokes(const QRectF &visibleWorld) const
{
    QSet<int> unique;
    QVector<int> result;

    const int x0 = qFloor(visibleWorld.left() / GRID_SIZE);
    const int x1 = qFloor(visibleWorld.right() / GRID_SIZE);
    const int y0 = qFloor(visibleWorld.top() / GRID_SIZE);
    const int y1 = qFloor(visibleWorld.bottom() / GRID_SIZE);

    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            const auto it = m_gridIndex.constFind(cellKey(cx, cy));
            if (it == m_gridIndex.constEnd()) {
                continue;
            }

            for (int idx : *it) {
                if (!unique.contains(idx)) {
                    unique.insert(idx);
                    result.append(idx);
                }
            }
        }
    }

    return result;
}

void CanvasWidget::appendPointToCurrentStroke(const QPointF &worldPoint)
{
    if (m_currentStroke.points.isEmpty()) {
        m_currentStroke.points.append(worldPoint);
        m_currentStroke.bounds = QRectF(worldPoint, QSizeF(0, 0));
        m_currentStroke.path = QPainterPath(worldPoint);
        return;
    }

    const QPointF last = m_currentStroke.points.last();
    const qreal minScreenDist = 0.75;
    const qreal minDist = qMax<qreal>(0.02, minScreenDist / qMax<qreal>(m_zoom, 0.001));
    const qreal dx = worldPoint.x() - last.x();
    const qreal dy = worldPoint.y() - last.y();

    if (dx * dx + dy * dy < minDist * minDist) {
        return;
    }

    m_currentStroke.points.append(worldPoint);
    m_currentStroke.bounds = m_currentStroke.bounds.united(QRectF(worldPoint, QSizeF(0, 0)));
    m_currentStroke.path.lineTo(worldPoint);
    
    // Invalidate cache when drawing to ensure real-time updates
    invalidateCache();
}

void CanvasWidget::finishCurrentStroke()
{
    if (m_currentStroke.points.size() < 2) {
        m_currentStroke = Stroke{};
        return;
    }

    m_currentStroke.bounds = computeStrokeBounds(m_currentStroke);
    m_currentStroke.path = buildSmoothPath(m_currentStroke.points);

    const int id = m_nextStrokeId++;
    m_strokes.append(m_currentStroke);
    addStrokeToIndex(m_strokes.size() - 1);
    saveStrokeFile(id, m_currentStroke);
    saveMeta();

    m_currentStroke = Stroke{};
    m_lastStrokeUpdateWorldRect = QRectF();
    
    // Invalidate cache after finishing stroke
    invalidateCache();
}

void CanvasWidget::scheduleMetaSave()
{
    if (m_metaSaveTimer) {
        m_metaSaveTimer->start(200);
    }
}

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = mousePosFromMouseEvent(event);
    const QPointF worldPos = screenToWorld(pos);
    m_pointerScreenPos = pos;
    m_pointerValid = true;

    if (isEraserButton(event->button())) {
        m_eraserButtonHeld = true;
        const qreal radiusWorld = currentEraserRadiusPixels() / qMax<qreal>(m_zoom, 0.001);
        if (eraseAt(worldPos, radiusWorld)) {
            m_eraseDirty = true;
            update();
        }
        event->accept();
        return;
    }

    if (m_eraserKeyHeld && event->button() == Qt::LeftButton) {
        const qreal radiusWorld = currentEraserRadiusPixels() / qMax<qreal>(m_zoom, 0.001);
        if (eraseAt(worldPos, radiusWorld)) {
            m_eraseDirty = true;
            update();
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        m_drawing = true;
        m_currentStroke = Stroke{};
        m_currentStroke.color = Qt::white;
        m_currentStroke.width = 2.0;
        appendPointToCurrentStroke(worldPos);
        m_lastStrokeUpdateWorldRect = QRectF(worldPos, QSizeF(0, 0));
        grabMouse();
        update(updateRectForWorld(m_lastStrokeUpdateWorldRect));
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastMousePos = pos;
        setCursor(Qt::ClosedHandCursor);
        grabMouse();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = mousePosFromMouseEvent(event);
    m_pointerScreenPos = pos;
    m_pointerValid = true;

    if (m_eraserButtonHeld || (m_eraserKeyHeld && (event->buttons() & Qt::LeftButton))) {
        const qreal radiusWorld = currentEraserRadiusPixels() / qMax<qreal>(m_zoom, 0.001);
        if (eraseAt(screenToWorld(pos), radiusWorld)) {
            m_eraseDirty = true;
            update();
        } else if (isEraserMode()) {
            update();
        }
        event->accept();
        return;
    }

    if (m_drawing && (event->buttons() & Qt::LeftButton)) {
        const QPointF world = screenToWorld(pos);
        const int oldCount = m_currentStroke.points.size();
        appendPointToCurrentStroke(world);
        if (m_currentStroke.points.size() != oldCount) {
            update();
        }
        event->accept();
        return;
    }

    if (m_panning && (event->buttons() & (Qt::RightButton | Qt::MiddleButton))) {
        const QPointF delta = pos - m_lastMousePos;
        m_pan += delta;
        m_lastMousePos = pos;
        update();
        scheduleMetaSave();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    m_pointerScreenPos = mousePosFromMouseEvent(event);
    m_pointerValid = true;

    if (isEraserButton(event->button()) || (m_eraserKeyHeld && event->button() == Qt::LeftButton)) {
        if (isEraserButton(event->button())) {
            m_eraserButtonHeld = false;
        }
        if (m_eraseDirty) {
            rewriteAllStrokeFiles();
            m_eraseDirty = false;
        }
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_drawing) {
        m_drawing = false;
        finishCurrentStroke();
        releaseMouse();
        update();
        event->accept();
        return;
    }

    if ((event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) && m_panning) {
        m_panning = false;
        unsetCursor();
        releaseMouse();
        saveMeta();
        update();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void CanvasWidget::wheelEvent(QWheelEvent *event)
{
    const QPointF cursor = mousePosFromWheelEvent(event);
    const QPointF worldBeforeZoom = screenToWorld(cursor);

    const qreal factor = std::pow(1.0015, event->angleDelta().y());
    m_zoom *= factor;
    m_zoom = qBound(0.05, m_zoom, 50.0);

    m_pan = cursor - QPointF(worldBeforeZoom.x() * m_zoom, worldBeforeZoom.y() * m_zoom);
    
    // Invalidate cache on zoom change
    invalidateCache();
    
    scheduleMetaSave();
    update();

    event->accept();
}

void CanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat() && event->key() == Qt::Key_B) {
        m_eraserKeyHeld = true;
        update();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat() && event->key() == Qt::Key_B) {
        m_eraserKeyHeld = false;
        if (m_eraseDirty) {
            rewriteAllStrokeFiles();
            m_eraseDirty = false;
        }
        update();
        event->accept();
        return;
    }

    QWidget::keyReleaseEvent(event);
}

void CanvasWidget::tabletEvent(QTabletEvent *event)
{
    const QPointF pos = posFromTabletEvent(event);
    m_pointerScreenPos = pos;
    m_pointerValid = true;
    m_lastTabletPressure = event->pressure();

    const bool eraserBtnDown = isAnyEraserButtonPressed(event->buttons()) || isEraserButton(static_cast<Qt::MouseButton>(event->button()));
    m_eraserButtonHeld = eraserBtnDown;

    const bool tipDown = event->buttons().testFlag(Qt::LeftButton);
    if (m_eraserButtonHeld || (m_eraserKeyHeld && tipDown)) {
        const qreal radiusWorld = currentEraserRadiusPixels() / qMax<qreal>(m_zoom, 0.001);
        if (eraseAt(screenToWorld(pos), radiusWorld)) {
            m_eraseDirty = true;
        }
        update();
        if (event->type() == QEvent::TabletRelease && m_eraseDirty) {
            rewriteAllStrokeFiles();
            m_eraseDirty = false;
        }
        event->accept();
        return;
    }

    QWidget::tabletEvent(event);
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF visibleWorld = QRectF(
                              screenToWorld(QPointF(0, 0)),
                              screenToWorld(QPointF(width(), height()))
                              ).normalized();
    const qreal viewPadWorld = qMax<qreal>(2000.0, 4000.0 / qMax<qreal>(m_zoom, 0.001));
    const QRectF paddedVisibleWorld = visibleWorld.adjusted(-viewPadWorld, -viewPadWorld,
                                                            viewPadWorld, viewPadWorld);

    // Render all strokes with optimized spatial indexing
    renderStrokes(p, paddedVisibleWorld);

    // Draw current stroke being drawn
    if (m_drawing) {
        QTransform worldToScreen;
        worldToScreen.translate(m_pan.x(), m_pan.y());
        worldToScreen.scale(m_zoom, m_zoom);

        auto drawStroke = [&p, &worldToScreen](const Stroke &s) {
            if (s.points.size() < 2 || s.path.isEmpty()) {
                return;
            }

            QPen pen(s.color);
            pen.setWidthF(s.width);
            pen.setCosmetic(false);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            p.drawPath(worldToScreen.map(s.path));
        };
        drawStroke(m_currentStroke);
    }

    if (m_pointerValid && isEraserMode()) {
        const qreal r = currentEraserRadiusPixels();
        QColor fill(255, 255, 255, 55);
        QColor edge(255, 255, 255, 170);
        p.setBrush(fill);
        p.setPen(QPen(edge, 1.5));
        p.drawEllipse(m_pointerScreenPos, r, r);
    }
}

bool CanvasWidget::saveMeta() const
{
    if (m_projectFolder.isEmpty()) {
        return false;
    }

    QDir().mkpath(m_projectFolder);

    QJsonObject meta;
    meta["version"] = 1;
    meta["zoom"] = m_zoom;

    QJsonObject pan;
    pan["x"] = m_pan.x();
    pan["y"] = m_pan.y();
    meta["pan"] = pan;

    meta["nextStrokeId"] = m_nextStrokeId;

    QSaveFile file(m_projectFolder + "/meta.json");
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));
    return file.commit();
}

void CanvasWidget::saveStrokeFile(int strokeId, const Stroke &stroke) const
{
    const QString filePath = strokeFilePath(strokeId);
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QJsonObject obj;
    obj["color"] = stroke.color.name(QColor::HexRgb);
    obj["width"] = stroke.width;

    QJsonArray points;
    for (const QPointF &p : stroke.points) {
        QJsonObject pt;
        pt["x"] = p.x();
        pt["y"] = p.y();
        points.append(pt);
    }

    obj["points"] = points;

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.commit();
}

bool CanvasWidget::loadStrokeFile(const QString &filePath, Stroke &stroke) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();

    stroke.color = QColor(obj["color"].toString("#000000"));
    stroke.width = obj["width"].toDouble(2.0);

    const QJsonArray points = obj["points"].toArray();
    stroke.points.clear();
    stroke.points.reserve(points.size());

    for (const QJsonValue &v : points) {
        const QJsonObject pt = v.toObject();
        stroke.points.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
    }

    stroke.bounds = computeStrokeBounds(stroke);
    return stroke.points.size() >= 2;
}

bool CanvasWidget::loadProject(const QString &folderPath)
{
    clearAll();
    m_projectFolder = folderPath;
    QDir().mkpath(m_projectFolder + "/strokes");

    QFile metaFile(m_projectFolder + "/meta.json");
    if (metaFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        if (doc.isObject()) {
            const QJsonObject meta = doc.object();
            m_zoom = meta["zoom"].toDouble(1.0);

            const QJsonObject pan = meta["pan"].toObject();
            m_pan = QPointF(pan["x"].toDouble(0.0), pan["y"].toDouble(0.0));

            m_nextStrokeId = meta["nextStrokeId"].toInt(1);
        }
    }

    QStringList files;
    QDirIterator it(m_projectFolder + "/strokes",
                    QStringList() << "stroke_*.json",
                    QDir::Files,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        files.append(it.next());
    }

    std::sort(files.begin(), files.end());

    for (const QString &filePath : files) {
        Stroke s;
        if (loadStrokeFile(filePath, s)) {
            s.path = buildSmoothPath(s.points);
            m_strokes.append(s);
        }
    }

    rebuildIndex();
    update();
    return true;
}
