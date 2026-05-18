#include "TMOGUIAlignDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QTabWidget>
#include <QFileInfo>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFont>
#include <QtMath>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <opencv2/features2d.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

// AlignCanvas
AlignCanvas::AlignCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
}

void AlignCanvas::setReference(const QImage& img)
{
    m_reference = img;
    update();
}

void AlignCanvas::setOverlay(const QImage& img, AlignTransform* t)
{
    m_overlay = img;
    m_transform = t;
    update();
}

void AlignCanvas::clearOverlay()
{
    m_overlay = QImage();
    m_transform = nullptr;
    update();
}

float AlignCanvas::displayScale() const
{
    if (m_reference.isNull()) return 1.0f;
    float sx = width() / (float)m_reference.width();
    float sy = height() / (float)m_reference.height();
    return qMin(sx, sy) * m_viewScale;
}

void AlignCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(40, 40, 40));

    if (m_reference.isNull())
    {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, "No image");
        return;
    }

    float s = displayScale();
    float cx = width() / 2.0f + m_panOffset.x();
    float cy = height() / 2.0f + m_panOffset.y();
    float rw = m_reference.width() * s;
    float rh = m_reference.height() * s;

    p.drawImage(QRectF(cx - rw / 2, cy - rh / 2, rw, rh), m_reference);

    if (m_overlay.isNull() || !m_transform)
    {
        p.setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        p.drawRect(QRectF(cx - rw / 2, cy - rh / 2, rw, rh));
        return;
    }

    float ow = m_overlay.width();
    float oh = m_overlay.height();

    p.save();
    p.setOpacity(m_opacity);

    // tx/ty stored in original-image pixels
    // multiply by m_overlayOrigScale to get preview pixels
    p.translate(cx + m_transform->tx * m_overlayOrigScale * s,
                cy + m_transform->ty * m_overlayOrigScale * s);
    p.rotate(m_transform->angle);
    p.scale(m_transform->scale * s, m_transform->scale * s);
    p.drawImage(QRectF(-ow / 2.0, -oh / 2.0, ow, oh), m_overlay);
    p.restore();
}

void AlignCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton)
    {
        m_panning = true;
        m_panLastPos = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::LeftButton && m_transform)
    {
        m_dragging = true;
        m_lastPos = e->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void AlignCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning)
    {
        m_panOffset += QPointF(e->pos()) - m_panLastPos;
        m_panLastPos = e->pos();
        update();
        return;
    }
    if (!m_dragging || !m_transform) return;
    QPointF delta = QPointF(e->pos()) - m_lastPos;
    m_lastPos = e->pos();
    float s = displayScale();
    switch (m_mode)
    {
    case Translate:
        // Divide by m_overlayOrigScale to convert from preview pixels to original image pixels
        m_transform->tx += (float)(delta.x() / s) / m_overlayOrigScale;
        m_transform->ty += (float)(delta.y() / s) / m_overlayOrigScale;
        break;
    case Rotate:
        m_transform->angle += (float)(delta.x() * 0.3);
        m_transform->angle = fmodf(m_transform->angle, 360.0f);
        break;
    case Scale:
        m_transform->scale *= 1.0f + (float)(delta.y() * -0.003);
        m_transform->scale = qBound(0.05f, m_transform->scale, 10.0f);
        break;
    }
    update();
    emit transformChanged();
}

void AlignCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton)
    {
        m_panning = false;
        setCursor(Qt::CrossCursor);
        return;
    }
    if (e->button() == Qt::LeftButton)
    {
        m_dragging = false;
        setCursor(Qt::CrossCursor);
    }
}

void AlignCanvas::wheelEvent(QWheelEvent* e)
{
    float factor = (e->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
    m_viewScale = qBound(0.05f, m_viewScale * factor, 20.0f);
    update();
    emit viewScaleChanged(m_viewScale);
}

// FeaturePointCanvas
const QColor FeaturePointCanvas::kColors[10] = {
    {230, 80,  80},
    {80,  210, 80},
    {80,  150, 240},
    {240, 200, 50},
    {240, 100, 240},
    {50,  220, 220},
    {240, 140, 60},
    {160, 80,  240},
    {80,  240, 160},
    {240, 240, 80},
};

FeaturePointCanvas::FeaturePointCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(500, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
}

void FeaturePointCanvas::setImages(const QImage& reference, const QImage& overlay)
{
    m_reference = reference;
    m_overlay   = overlay;
    update();
}

void FeaturePointCanvas::clearImages()
{
    m_reference = QImage();
    m_overlay   = QImage();
    update();
}

void FeaturePointCanvas::beginAddPair()
{
    m_selectedIdx = -1;
    m_addState    = PlacingRef;
    setCursor(Qt::CrossCursor);
    emitStatus();
    update();
}

void FeaturePointCanvas::cancelAdd()
{
    m_addState = Idle;
    setCursor(Qt::CrossCursor);
    emitStatus();
    update();
}

void FeaturePointCanvas::deleteSelected()
{
    if (m_selectedIdx >= 0 && m_selectedIdx < m_pairs.size())
    {
        m_pairs.removeAt(m_selectedIdx);
        m_selectedIdx = -1;
        emit pairsChanged();
        emitStatus();
        update();
    }
}

void FeaturePointCanvas::clearAll()
{
    m_pairs.clear();
    m_selectedIdx = -1;
    m_addState    = Idle;
    m_viewScale   = 1.0f;
    m_panOffset   = {};
    emit pairsChanged();
    emitStatus();
    update();
}

FeaturePointCanvas::Layout FeaturePointCanvas::computeLayout() const
{
    Layout l;
    if (width() < 10 || height() < 10) return l;

    const int sep  = 6;
    const int halfW = (width() - sep) / 2;
    const int H = height();

    if (!m_reference.isNull())
    {
        float s = qMin(halfW / (float)m_reference.width(), H / (float)m_reference.height()) * m_viewScale;
        float w = m_reference.width() * s;
        float h = m_reference.height() * s;
        l.refRect = QRectF(halfW / 2.0f - w / 2.0f + m_panOffset.x(),
                           H / 2.0f - h / 2.0f + m_panOffset.y(), w, h);
    }

    int rightStart = halfW + sep;
    if (!m_overlay.isNull())
    {
        float s = qMin(halfW / (float)m_overlay.width(), H / (float)m_overlay.height()) * m_viewScale;
        float w = m_overlay.width() * s;
        float h = m_overlay.height() * s;
        l.ovRect = QRectF(rightStart + halfW / 2.0f - w / 2.0f + m_panOffset.x(),
                          H / 2.0f - h / 2.0f + m_panOffset.y(), w, h);
    }

    return l;
}

QPointF FeaturePointCanvas::refNormToScreen(QPointF n, const Layout& l) const
{
    return { l.refRect.x() + n.x() * l.refRect.width(),
             l.refRect.y() + n.y() * l.refRect.height() };
}

QPointF FeaturePointCanvas::ovNormToScreen(QPointF n, const Layout& l) const
{
    return { l.ovRect.x() + n.x() * l.ovRect.width(),
             l.ovRect.y() + n.y() * l.ovRect.height() };
}

QPointF FeaturePointCanvas::screenToRefNorm(QPointF p, const Layout& l) const
{
    if (l.refRect.width() == 0 || l.refRect.height() == 0) return {-1, -1};
    return { (p.x() - l.refRect.x()) / l.refRect.width(),
             (p.y() - l.refRect.y()) / l.refRect.height() };
}

QPointF FeaturePointCanvas::screenToOvNorm(QPointF p, const Layout& l) const
{
    if (l.ovRect.width() == 0 || l.ovRect.height() == 0) return {-1, -1};
    return { (p.x() - l.ovRect.x()) / l.ovRect.width(),
             (p.y() - l.ovRect.y()) / l.ovRect.height() };
}

int FeaturePointCanvas::hitTest(QPointF pos, const Layout& l, bool* hitRef) const
{
    for (int i = 0; i < m_pairs.size(); i++)
    {
        if (m_pairs[i].refPt.x() >= 0)
        {
            if (QLineF(pos, refNormToScreen(m_pairs[i].refPt, l)).length() <= kRadius + 4)
            {
                if (hitRef) *hitRef = true;
                return i;
            }
        }
        if (m_pairs[i].overlayPt.x() >= 0)
        {
            if (QLineF(pos, ovNormToScreen(m_pairs[i].overlayPt, l)).length() <= kRadius + 4)
            {
                if (hitRef) *hitRef = false;
                return i;
            }
        }
    }
    return -1;
}

void FeaturePointCanvas::emitStatus()
{
    int total    = m_pairs.size();
    int complete = 0;
    for (auto& p : m_pairs) if (p.complete()) complete++;

    QString msg;
    switch (m_addState)
    {
    case PlacingRef:
        msg = "Click on the REFERENCE (left) image to place the point";
        break;
    case PlacingOverlay:
        msg = "Click on the OVERLAY (right) image to place the matching point";
        break;
    default:
        if (total == 0)
            msg = "No pairs — click \"Add Pair\" or run SIFT detection";
        else
            msg = QString("%1 pair(s), %2 complete").arg(total).arg(complete);
        break;
    }
    emit statusMessage(msg);
}

void FeaturePointCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    p.fillRect(rect(), QColor(35, 35, 35));

    Layout l = computeLayout();

    // Panel labels
    auto drawPanelLabel = [&](const QString& text, float centerX)
    {
        p.setPen(QColor(160, 160, 160));
        QFont f = p.font();
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        QRectF r(centerX - 80, 4, 160, 16);
        p.drawText(r, Qt::AlignCenter, text);
    };

    int halfW = (width() - 6) / 2;
    drawPanelLabel("REFERENCE", halfW / 2.0f);
    drawPanelLabel("OVERLAY",   halfW + 6 + halfW / 2.0f);

    // Separator
    p.setPen(QPen(QColor(70, 70, 70), 2));
    p.drawLine(halfW + 2, 0, halfW + 2, height());

    // Images
    if (!m_reference.isNull() && !l.refRect.isEmpty())
        p.drawImage(l.refRect, m_reference);
    else if (m_reference.isNull())
    {
        p.setPen(Qt::gray);
        p.drawText(QRectF(0, 0, halfW, height()), Qt::AlignCenter, "No reference");
    }

    if (!m_overlay.isNull() && !l.ovRect.isEmpty())
        p.drawImage(l.ovRect, m_overlay);
    else if (m_overlay.isNull())
    {
        p.setPen(Qt::gray);
        p.drawText(QRectF(halfW + 6, 0, halfW, height()), Qt::AlignCenter, "No image");
    }

    // Connecting lines between complete pairs (drawn below circles)
    for (int i = 0; i < m_pairs.size(); i++)
    {
        const auto& pair = m_pairs[i];
        if (!pair.complete()) continue;
        QColor col = kColors[i % 10];
        col.setAlpha(80);
        p.setPen(QPen(col, 1.5, Qt::DashLine));
        p.drawLine(refNormToScreen(pair.refPt, l), ovNormToScreen(pair.overlayPt, l));
    }

    // Draw point pairs
    QFont numFont = p.font();
    numFont.setPixelSize(9);
    numFont.setBold(true);
    p.setFont(numFont);

    auto drawPoint = [&](QPointF screenPt, int idx, bool selected, bool incomplete)
    {
        QColor col = kColors[idx % 10];
        int r = selected ? kRadius + 3 : kRadius;

        p.setPen(QPen(selected ? Qt::white : col.darker(150), selected ? 2.5 : 1.5,
                      incomplete ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(incomplete ? QColor(col.red(), col.green(), col.blue(), 80) : col);
        p.drawEllipse(screenPt, r, r);

        p.setPen(Qt::black);
        p.drawText(QRectF(screenPt.x() - r, screenPt.y() - r, r * 2, r * 2),
                   Qt::AlignCenter, QString::number(idx + 1));
    };

    for (int i = 0; i < m_pairs.size(); i++)
    {
        const auto& pair = m_pairs[i];
        bool sel  = (i == m_selectedIdx);

        if (pair.refPt.x() >= 0)
            drawPoint(refNormToScreen(pair.refPt, l), i, sel, !pair.complete());
        if (pair.overlayPt.x() >= 0)
            drawPoint(ovNormToScreen(pair.overlayPt, l), i, sel, false);
    }

    // Pending ref point during PlacingOverlay state
    if (m_addState == PlacingOverlay && m_pendingRefPt.x() >= 0)
    {
        QColor col = kColors[m_pairs.size() % 10];
        p.setPen(QPen(col, 1.5, Qt::DashLine));
        p.setBrush(QColor(col.red(), col.green(), col.blue(), 80));
        p.drawEllipse(refNormToScreen(m_pendingRefPt, l), kRadius, kRadius);
        p.setPen(Qt::black);
        p.drawText(QRectF(refNormToScreen(m_pendingRefPt, l).x() - kRadius,
                          refNormToScreen(m_pendingRefPt, l).y() - kRadius,
                          kRadius * 2, kRadius * 2),
                   Qt::AlignCenter, QString::number(m_pairs.size() + 1));
    }

    if (m_hovering && m_addState != Idle)
    {
        p.setPen(QPen(Qt::white, 1, Qt::DotLine));
        p.setOpacity(0.5);
        p.drawLine(QPointF(m_hoverPos.x(), 0), QPointF(m_hoverPos.x(), height()));
        p.drawLine(QPointF(0, m_hoverPos.y()), QPointF(width(), m_hoverPos.y()));
    }
}

void FeaturePointCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton)
    {
        m_panning = true;
        m_panLastPos = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    Layout l = computeLayout();
    QPointF pos = e->pos();

    if (m_addState == PlacingRef)
    {
        if (l.refRect.contains(pos))
        {
            m_pendingRefPt = screenToRefNorm(pos, l);
            m_addState = PlacingOverlay;
            emitStatus();
            update();
        }
        return;
    }

    if (m_addState == PlacingOverlay)
    {
        if (l.ovRect.contains(pos))
        {
            PointPair pair;
            pair.refPt     = m_pendingRefPt;
            pair.overlayPt = screenToOvNorm(pos, l);
            m_pairs.append(pair);
            m_pendingRefPt = {-1, -1};
            m_addState = Idle;
            emit pairsChanged();
            emitStatus();
            update();
        }
        return;
    }

    // Idle: hit-test existing points
    bool hitRef = false;
    int hit = hitTest(pos, l, &hitRef);
    if (hit >= 0)
    {
        m_selectedIdx = hit;
        update();
        return;
    }

    // Click on empty area - deselect
    m_selectedIdx = -1;
    update();
}

void FeaturePointCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning)
    {
        m_panOffset += QPointF(e->pos()) - m_panLastPos;
        m_panLastPos = e->pos();
        m_hoverPos = e->pos();
        update();
        return;
    }
    m_hoverPos  = e->pos();
    m_hovering  = true;
    Layout l    = computeLayout();

    if (m_addState == PlacingRef && l.refRect.contains(e->pos()))
        setCursor(Qt::CrossCursor);
    else if (m_addState == PlacingOverlay && l.ovRect.contains(e->pos()))
        setCursor(Qt::CrossCursor);
    else if (m_addState == Idle)
    {
        bool dummy;
        setCursor(hitTest(e->pos(), l, &dummy) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    else
        setCursor(Qt::ForbiddenCursor);

    if (m_addState != Idle) update();
}

void FeaturePointCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton)
    {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
    }
}

void FeaturePointCanvas::wheelEvent(QWheelEvent* e)
{
    float factor = (e->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
    m_viewScale = qBound(0.05f, m_viewScale * factor, 20.0f);
    update();
    emit viewScaleChanged(m_viewScale);
}

void FeaturePointCanvas::leaveEvent(QEvent*)
{
    m_hovering = false;
    update();
}

void FeaturePointCanvas::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape)
    {
        m_pendingRefPt = {-1, -1};
        m_addState = Idle;
        emitStatus();
        update();
    }
    else if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
    {
        deleteSelected();
    }
}

// TMOGUIAlignDialog
TMOGUIAlignDialog::TMOGUIAlignDialog(const QStringList& imagePaths,
                                     QVector<AlignTransform>& transforms,
                                     QVector<QVector<FeaturePointCanvas::PointPair>>& pointPairs,
                                     QWidget* parent)
    : QDialog(parent)
    , m_imagePaths(imagePaths)
    , m_transforms(transforms)
    , m_pointPairsRef(pointPairs)
{
    setWindowTitle("Image Alignment");
    setMinimumSize(980, 660);
    m_transforms.resize(m_imagePaths.size());
    m_pointPairsRef.resize(m_imagePaths.size());
    buildUI();
    loadPreviewImages();
    updateList();
    if (m_imagePaths.size() > 1)
        selectImage(1);
}

void TMOGUIAlignDialog::buildUI()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(8);

    // Left panel: image list
    auto* leftPanel = new QWidget(this);
    leftPanel->setFixedWidth(200);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);
    leftLayout->addWidget(new QLabel("Images:", leftPanel));
    m_list = new QListWidget(leftPanel);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_list, 1);
    m_setRefBtn = new QPushButton("Set as Reference", leftPanel);
    leftLayout->addWidget(m_setRefBtn);
    mainLayout->addWidget(leftPanel);

    // Right panel
    auto* rightPanel = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    m_tabs = new QTabWidget(rightPanel);
    rightLayout->addWidget(m_tabs, 1);

    // Tab 0: Transform
    auto* transformPage = new QWidget;
    auto* transformLayout = new QVBoxLayout(transformPage);
    transformLayout->setSpacing(6);

    m_canvas = new AlignCanvas(transformPage);
    transformLayout->addWidget(m_canvas, 1);

    auto* opacityRow = new QHBoxLayout;
    opacityRow->addWidget(new QLabel("Overlay opacity:", transformPage));
    m_opacitySlider = new QSlider(Qt::Horizontal, transformPage);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(50);
    opacityRow->addWidget(m_opacitySlider, 1);
    m_opacityLabel = new QLabel("50%", transformPage);
    m_opacityLabel->setFixedWidth(36);
    opacityRow->addWidget(m_opacityLabel);
    transformLayout->addLayout(opacityRow);

    auto* zoomRow = new QHBoxLayout;
    zoomRow->addWidget(new QLabel("Zoom:", transformPage));
    auto* zoomOutBtn = new QPushButton("-", transformPage);
    auto* zoomInBtn  = new QPushButton("+", transformPage);
    m_zoomSlider = new QSlider(Qt::Horizontal, transformPage);
    m_zoomSlider->setRange(5, 500);   // 5% – 500%
    m_zoomSlider->setValue(100);
    m_zoomLabel = new QLabel("100%", transformPage);
    m_zoomLabel->setFixedWidth(42);
    zoomRow->addWidget(zoomOutBtn);
    zoomRow->addWidget(m_zoomSlider, 1);
    zoomRow->addWidget(zoomInBtn);
    zoomRow->addWidget(m_zoomLabel);
    transformLayout->addLayout(zoomRow);

    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        m_zoomLabel->setText(QString("%1%").arg(v));
        m_canvas->setViewScale(v / 100.0f);
    });
    connect(zoomOutBtn, &QPushButton::clicked, this, [this]() {
        m_canvas->setViewScale(m_canvas->viewScale() / 1.25f);
    });
    connect(zoomInBtn, &QPushButton::clicked, this, [this]() {
        m_canvas->setViewScale(m_canvas->viewScale() * 1.25f);
    });
    connect(m_canvas, &AlignCanvas::viewScaleChanged, this, [this](float s) {
        int v = qBound(5, qRound(s * 100.0f), 500);
        m_zoomSlider->blockSignals(true);
        m_zoomSlider->setValue(v);
        m_zoomSlider->blockSignals(false);
        m_zoomLabel->setText(QString("%1%").arg(v));
    });

    auto* txGroup = new QGroupBox("Affine Transform", transformPage);
    auto* txGroupLayout = new QVBoxLayout(txGroup);

    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel("Mode:", txGroup));
    m_translateBtn = new QPushButton("Translate", txGroup);
    m_rotateBtn    = new QPushButton("Rotate",    txGroup);
    m_scaleBtn     = new QPushButton("Scale",     txGroup);
    for (auto* btn : {m_translateBtn, m_rotateBtn, m_scaleBtn})
    {
        btn->setCheckable(true);
        btn->setFixedHeight(26);
    }
    m_translateBtn->setChecked(true);
    modeRow->addWidget(m_translateBtn);
    modeRow->addWidget(m_rotateBtn);
    modeRow->addWidget(m_scaleBtn);
    modeRow->addStretch();
    txGroupLayout->addLayout(modeRow);

    auto* numRow = new QHBoxLayout;
    auto makeField = [](double min, double max, double step, int dec, const QString& sfx) {
        auto* sb = new QDoubleSpinBox;
        sb->setRange(min, max);
        sb->setSingleStep(step);
        sb->setDecimals(dec);
        sb->setSuffix(sfx);
        sb->setFixedWidth(110);
        return sb;
    };
    m_txSpin    = makeField(-99999, 99999, 1.0, 1, " px");
    m_tySpin    = makeField(-99999, 99999, 1.0, 1, " px");
    m_angleSpin = makeField(-360,   360,   0.1, 2, "°");
    m_scaleSpin = makeField(0.01,   10.0,  0.01, 3, "x");
    m_scaleSpin->setValue(1.0);
    numRow->addWidget(new QLabel("dX:"));    numRow->addWidget(m_txSpin);
    numRow->addSpacing(8);
    numRow->addWidget(new QLabel("dY:"));    numRow->addWidget(m_tySpin);
    numRow->addSpacing(8);
    numRow->addWidget(new QLabel("Angle:")); numRow->addWidget(m_angleSpin);
    numRow->addSpacing(8);
    numRow->addWidget(new QLabel("Scale:")); numRow->addWidget(m_scaleSpin);
    numRow->addStretch();
    txGroupLayout->addLayout(numRow);

    auto* txBtnRow = new QHBoxLayout;
    m_resetBtn = new QPushButton("Reset Transform", txGroup);
    txBtnRow->addWidget(m_resetBtn);
    m_transformModeLabel = new QLabel(txGroup);
    m_transformModeLabel->setStyleSheet("font-style: italic; color: orange;");
    txBtnRow->addWidget(m_transformModeLabel, 1);
    txGroupLayout->addLayout(txBtnRow);

    transformLayout->addWidget(txGroup);
    // Tab 1: Feature Points
    auto* fpPage = new QWidget;
    auto* fpLayout = new QVBoxLayout(fpPage);
    fpLayout->setSpacing(6);

    m_fpCanvas = new FeaturePointCanvas(fpPage);
    fpLayout->addWidget(m_fpCanvas, 1);

    auto* fpZoomRow = new QHBoxLayout;
    fpZoomRow->addWidget(new QLabel("Zoom:", fpPage));
    auto* fpZoomOutBtn = new QPushButton("-", fpPage);
    auto* fpZoomInBtn  = new QPushButton("+", fpPage);
    m_fpZoomSlider = new QSlider(Qt::Horizontal, fpPage);
    m_fpZoomSlider->setRange(5, 500);
    m_fpZoomSlider->setValue(100);
    m_fpZoomLabel = new QLabel("100%", fpPage);
    m_fpZoomLabel->setFixedWidth(42);
    fpZoomRow->addWidget(fpZoomOutBtn);
    fpZoomRow->addWidget(m_fpZoomSlider, 1);
    fpZoomRow->addWidget(fpZoomInBtn);
    fpZoomRow->addWidget(m_fpZoomLabel);
    fpLayout->addLayout(fpZoomRow);

    connect(m_fpZoomSlider, &QSlider::valueChanged, this, [this](int v) {
        m_fpZoomLabel->setText(QString("%1%").arg(v));
        m_fpCanvas->setViewScale(v / 100.0f);
    });
    connect(fpZoomOutBtn, &QPushButton::clicked, this, [this]() {
        m_fpCanvas->setViewScale(m_fpCanvas->viewScale() / 1.25f);
    });
    connect(fpZoomInBtn, &QPushButton::clicked, this, [this]() {
        m_fpCanvas->setViewScale(m_fpCanvas->viewScale() * 1.25f);
    });
    connect(m_fpCanvas, &FeaturePointCanvas::viewScaleChanged, this, [this](float s) {
        int v = qBound(5, qRound(s * 100.0f), 500);
        m_fpZoomSlider->blockSignals(true);
        m_fpZoomSlider->setValue(v);
        m_fpZoomSlider->blockSignals(false);
        m_fpZoomLabel->setText(QString("%1%").arg(v));
    });

    auto* fpControlGroup = new QGroupBox("Feature Point Correspondences", fpPage);
    auto* fpCtrlLayout = new QVBoxLayout(fpControlGroup);

    auto* fpBtnRow = new QHBoxLayout;
    m_addPairBtn    = new QPushButton("Add Pair",        fpControlGroup);
    m_deletePairBtn = new QPushButton("Delete Selected", fpControlGroup);
    m_clearPairsBtn = new QPushButton("Clear All",       fpControlGroup);
    m_detectSIFTBtn = new QPushButton("Detect SIFT...",  fpControlGroup);
    m_deletePairBtn->setEnabled(false);
    fpBtnRow->addWidget(m_addPairBtn);
    fpBtnRow->addWidget(m_deletePairBtn);
    fpBtnRow->addWidget(m_clearPairsBtn);
    fpBtnRow->addStretch();
    fpBtnRow->addWidget(m_detectSIFTBtn);
    fpCtrlLayout->addLayout(fpBtnRow);

    auto* fpBottomRow = new QHBoxLayout;
    m_fpStatusLabel = new QLabel(fpControlGroup);
    m_fpStatusLabel->setStyleSheet("font-style: italic; color: palette(mid);");
    fpBottomRow->addWidget(m_fpStatusLabel, 1);
    fpCtrlLayout->addLayout(fpBottomRow);

    fpLayout->addWidget(fpControlGroup);
    m_tabs->addTab(fpPage, "Feature Points");
    m_tabs->addTab(transformPage, "Transform");

    // Bottom bar
    auto* bottomRow = new QHBoxLayout;
    bottomRow->addStretch();
    auto* doneBtn = new QPushButton("Done", rightPanel);
    doneBtn->setDefault(true);
    bottomRow->addWidget(doneBtn);
    rightLayout->addLayout(bottomRow);

    mainLayout->addWidget(rightPanel, 1);

    // Connections
    connect(m_list,         &QListWidget::currentRowChanged, this, &TMOGUIAlignDialog::onImageSelected);
    connect(m_setRefBtn,    &QPushButton::clicked,           this, &TMOGUIAlignDialog::onSetReference);
    connect(m_resetBtn,     &QPushButton::clicked,           this, &TMOGUIAlignDialog::onReset);
    connect(m_opacitySlider,&QSlider::valueChanged,          this, &TMOGUIAlignDialog::onOpacityChanged);
    connect(m_canvas,       &AlignCanvas::transformChanged,  this, &TMOGUIAlignDialog::onTransformChanged);
    connect(doneBtn,        &QPushButton::clicked,           this, &QDialog::accept);
    connect(m_tabs,         &QTabWidget::currentChanged,     this, &TMOGUIAlignDialog::onTabChanged);

    connect(m_translateBtn, &QPushButton::clicked, this, [this]() { updateModeButtons(AlignCanvas::Translate); });
    connect(m_rotateBtn,    &QPushButton::clicked, this, [this]() { updateModeButtons(AlignCanvas::Rotate);    });
    connect(m_scaleBtn,     &QPushButton::clicked, this, [this]() { updateModeButtons(AlignCanvas::Scale);     });

    for (QDoubleSpinBox* sb : {m_txSpin, m_tySpin, m_angleSpin, m_scaleSpin})
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &TMOGUIAlignDialog::onSpinChanged);

    connect(m_addPairBtn,    &QPushButton::clicked, this, &TMOGUIAlignDialog::onAddPair);
    connect(m_deletePairBtn, &QPushButton::clicked, this, &TMOGUIAlignDialog::onDeletePair);
    connect(m_clearPairsBtn, &QPushButton::clicked, this, &TMOGUIAlignDialog::onClearPairs);
    connect(m_detectSIFTBtn, &QPushButton::clicked, this, &TMOGUIAlignDialog::onDetectSIFT);
    connect(m_fpCanvas, &FeaturePointCanvas::pairsChanged,   this, &TMOGUIAlignDialog::onPairsChanged);
    connect(m_fpCanvas, &FeaturePointCanvas::statusMessage,   m_fpStatusLabel, &QLabel::setText);
}

void TMOGUIAlignDialog::loadPreviewImages()
{
    const int maxDim = 1200;
    m_previews.resize(m_imagePaths.size());
    m_originalSizes.resize(m_imagePaths.size());
    for (int i = 0; i < m_imagePaths.size(); i++)
    {
        QImage img(m_imagePaths[i]);
        m_originalSizes[i] = img.size();
        if (!img.isNull() && (img.width() > maxDim || img.height() > maxDim))
            img = img.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_previews[i] = img;
    }
}

void TMOGUIAlignDialog::updateList()
{
    m_list->blockSignals(true);
    int prevRow = m_list->currentRow();
    m_list->clear();

    for (int i = 0; i < m_imagePaths.size(); i++)
    {
        QString name = QFileInfo(m_imagePaths[i]).fileName();
        bool isRef   = (i == m_referenceIdx);
        const AlignTransform& t = m_transforms[i];
        bool hasTx   = !t.isIdentity();
        QString mode = t.useHomography ? " [H]" : "";

        QString text = (isRef ? "[REF] " : (hasTx ? "* " : "    ")) + name + mode;
        auto* item = new QListWidgetItem(text, m_list);

        if (isRef)
        {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            item->setForeground(QColor(80, 180, 80));
        }
        else if (hasTx)
        {
            item->setForeground(QColor(200, 180, 80));
        }
    }

    m_list->setCurrentRow(qBound(0, prevRow, m_list->count() - 1));
    m_list->blockSignals(false);
}

static cv::Mat qImageToGray(const QImage& img)
{
    QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
    return cv::Mat(gray.height(), gray.width(), CV_8UC1,
                   (void*)gray.constBits(), (size_t)gray.bytesPerLine()).clone();
}

void TMOGUIAlignDialog::selectImage(int idx)
{
    if (m_currentIdx >= 0 && m_currentIdx < m_pointPairsRef.size())
        m_pointPairsRef[m_currentIdx] = m_fpCanvas->pairs();

    m_currentIdx = idx;

    bool isRef = (idx == m_referenceIdx || idx < 0 || idx >= m_imagePaths.size());

    m_canvas->setReference(m_previews.value(m_referenceIdx));
    if (!isRef)
    {
        const QImage& preview = m_previews.value(idx);
        const QSize& orig = m_originalSizes.value(idx);
        float scale = (orig.width() > 0) ? preview.width() / (float)orig.width() : 1.0f;
        m_canvas->setOverlayOrigScale(scale);
        m_canvas->setOverlay(preview, &m_transforms[idx]);
    }
    else
    {
        m_canvas->setOverlayOrigScale(1.0f);
        m_canvas->clearOverlay();
    }

    bool canEdit = !isRef;
    for (auto* w : {m_resetBtn, m_translateBtn, m_rotateBtn, m_scaleBtn})
        w->setEnabled(canEdit);
    for (auto* sb : {m_txSpin, m_tySpin, m_angleSpin, m_scaleSpin})
        sb->setEnabled(canEdit);

    m_fpCanvas->setImages(m_previews.value(m_referenceIdx),
                          isRef ? QImage() : m_previews.value(idx));
    m_suppressPairsSave = true;
    m_fpCanvas->clearAll();
    m_suppressPairsSave = false;
    if (m_fpZoomSlider) { m_fpZoomSlider->blockSignals(true); m_fpZoomSlider->setValue(100); m_fpZoomSlider->blockSignals(false); }
    if (m_fpZoomLabel)  m_fpZoomLabel->setText("100%");
    if (!isRef && idx < m_pointPairsRef.size())
    {
        m_fpCanvas->pairs() = m_pointPairsRef[idx];
        m_fpCanvas->update();
    }

    updateSpinBoxes();
    updateFeatureControls();
    tryComputeHomography();
}

void TMOGUIAlignDialog::onImageSelected(int row)
{
    if (row >= 0) selectImage(row);
}

void TMOGUIAlignDialog::onSetReference()
{
    int row = m_list->currentRow();
    if (row < 0 || row == m_referenceIdx) return;
    m_transforms[row] = AlignTransform{};
    m_referenceIdx = row;
    updateList();
    selectImage(m_list->currentRow());
}

void TMOGUIAlignDialog::onReset()
{
    if (m_currentIdx < 0 || m_currentIdx >= m_transforms.size()) return;
    m_transforms[m_currentIdx] = AlignTransform{};
    m_suppressPairsSave = true;
    m_fpCanvas->clearAll();
    m_suppressPairsSave = false;
    if (m_currentIdx < m_pointPairsRef.size())
        m_pointPairsRef[m_currentIdx].clear();
    if (m_fpZoomSlider) { m_fpZoomSlider->blockSignals(true); m_fpZoomSlider->setValue(100); m_fpZoomSlider->blockSignals(false); }
    if (m_fpZoomLabel)  m_fpZoomLabel->setText("100%");
    updateSpinBoxes();
    m_canvas->update();
    m_transformModeLabel->clear();
    m_fpStatusLabel->clear();
    updateList();
}

void TMOGUIAlignDialog::onOpacityChanged(int value)
{
    m_opacityLabel->setText(QString::number(value) + "%");
    m_canvas->setOpacity(value / 100.0f);
}

void TMOGUIAlignDialog::onTransformChanged()
{
    if (m_currentIdx >= 0 && m_currentIdx < m_transforms.size())
        m_transforms[m_currentIdx].useHomography = false;
    m_transformModeLabel->clear();
    updateSpinBoxes();
    updateList();
}

void TMOGUIAlignDialog::updateSpinBoxes()
{
    if (m_currentIdx < 0 || m_currentIdx >= m_transforms.size()) return;
    const AlignTransform& t = m_transforms[m_currentIdx];
    m_suppressSpinSignals = true;
    m_txSpin->setValue(t.tx);
    m_tySpin->setValue(t.ty);
    m_angleSpin->setValue(t.angle);
    m_scaleSpin->setValue(t.scale);
    m_suppressSpinSignals = false;
}

void TMOGUIAlignDialog::onSpinChanged()
{
    if (m_suppressSpinSignals) return;
    if (m_currentIdx < 0 || m_currentIdx >= m_transforms.size()) return;
    AlignTransform& t = m_transforms[m_currentIdx];
    t.tx    = (float)m_txSpin->value();
    t.ty    = (float)m_tySpin->value();
    t.angle = (float)m_angleSpin->value();
    t.scale = (float)m_scaleSpin->value();
    t.useHomography = false;
    m_transformModeLabel->clear();
    m_canvas->update();
    updateList();
}

void TMOGUIAlignDialog::updateModeButtons(AlignCanvas::Mode m)
{
    m_translateBtn->setChecked(m == AlignCanvas::Translate);
    m_rotateBtn->setChecked(m == AlignCanvas::Rotate);
    m_scaleBtn->setChecked(m == AlignCanvas::Scale);
    m_canvas->setMode(m);
}

void TMOGUIAlignDialog::onTabChanged(int /*index*/)
{
    updateFeatureControls();
}

// Feature Points slots

void TMOGUIAlignDialog::updateFeatureControls()
{
    bool hasImage = (m_currentIdx >= 0 && m_currentIdx != m_referenceIdx
                     && m_currentIdx < m_imagePaths.size());
    m_addPairBtn->setEnabled(hasImage);
    m_detectSIFTBtn->setEnabled(hasImage);
    m_clearPairsBtn->setEnabled(hasImage);

    m_deletePairBtn->setEnabled(m_fpCanvas->hasSelection());
}

void TMOGUIAlignDialog::onAddPair()
{
    m_fpCanvas->beginAddPair();
    m_tabs->setCurrentIndex(0);
    m_fpCanvas->setFocus();
}

void TMOGUIAlignDialog::onDeletePair()
{
    m_fpCanvas->deleteSelected();
    updateFeatureControls();
}

void TMOGUIAlignDialog::onClearPairs()
{
    m_fpCanvas->clearAll();
    updateFeatureControls();
}

void TMOGUIAlignDialog::onPairsChanged()
{
    if (!m_suppressPairsSave && m_currentIdx >= 0 && m_currentIdx < m_pointPairsRef.size())
        m_pointPairsRef[m_currentIdx] = m_fpCanvas->pairs();
    tryComputeHomography();
    updateFeatureControls();
    updateList();
}

void TMOGUIAlignDialog::onDetectSIFT()
{
    if (m_currentIdx < 0 || m_currentIdx == m_referenceIdx) return;

    const QImage& refImg = m_previews.value(m_referenceIdx);
    const QImage& ovImg  = m_previews.value(m_currentIdx);
    if (refImg.isNull() || ovImg.isNull()) return;

    QProgressDialog prog("Detecting SIFT features...", QString(), 0, 0, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.show();
    QApplication::processEvents();

    cv::Mat refGray, ovGray;
    cv::equalizeHist(qImageToGray(refImg), refGray);
    cv::equalizeHist(qImageToGray(ovImg),  ovGray);

    auto sift = cv::SIFT::create(1000);
    std::vector<cv::KeyPoint> kp1, kp2;
    cv::Mat desc1, desc2;
    sift->detectAndCompute(refGray, cv::noArray(), kp1, desc1);
    sift->detectAndCompute(ovGray,  cv::noArray(), kp2, desc2);
    prog.hide();

    if (kp1.empty() || kp2.empty() || desc1.empty() || desc2.empty())
    {
        QMessageBox::warning(this, "SIFT", "Not enough keypoints detected.");
        return;
    }

    cv::BFMatcher matcher(cv::NORM_L2, false);
    std::vector<std::vector<cv::DMatch>> knnMatches;
    matcher.knnMatch(desc1, desc2, knnMatches, 2);

    // Lowe ratio test
    std::vector<cv::DMatch> good;
    for (auto& m : knnMatches)
        if (m.size() >= 2 && m[0].distance < 0.75f * m[1].distance)
            good.push_back(m[0]);

    std::sort(good.begin(), good.end());
    if (good.size() > 30) good.resize(30);

    if (good.empty())
    {
        QMessageBox::warning(this, "SIFT", "No good matches found.");
        return;
    }

    // Remove old auto-detected pairs but keep manual ones
    auto& pairs = m_fpCanvas->pairs();
    pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
        [](const FeaturePointCanvas::PointPair& p) { return p.autoDetected; }),
        pairs.end());

    // Add new SIFT matches as auto pairs
    for (const auto& m : good)
    {
        FeaturePointCanvas::PointPair pp;
        pp.refPt     = { kp1[m.queryIdx].pt.x / refGray.cols,
                         kp1[m.queryIdx].pt.y / refGray.rows };
        pp.overlayPt = { kp2[m.trainIdx].pt.x  / ovGray.cols,
                         kp2[m.trainIdx].pt.y   / ovGray.rows };
        pp.autoDetected = true;
        pairs.append(pp);
    }

    m_fpCanvas->update();
    emit m_fpCanvas->pairsChanged();
    updateFeatureControls();
}

void TMOGUIAlignDialog::tryComputeHomography()
{
    if (m_currentIdx < 0 || m_currentIdx >= m_transforms.size()) return;

    const QSize& refOrigSize = m_originalSizes.value(m_referenceIdx);
    const QSize& ovOrigSize  = m_originalSizes.value(m_currentIdx);

    std::vector<cv::Point2f> refPts, ovPts;
    for (const auto& pp : m_fpCanvas->pairs())
    {
        if (!pp.complete()) continue;
        refPts.push_back({ (float)(pp.refPt.x()     * refOrigSize.width()),
                           (float)(pp.refPt.y()     * refOrigSize.height()) });
        ovPts.push_back({  (float)(pp.overlayPt.x() * ovOrigSize.width()),
                           (float)(pp.overlayPt.y() * ovOrigSize.height()) });
    }

    AlignTransform& t = m_transforms[m_currentIdx];

    if ((int)refPts.size() < 4)
    {
        t.useHomography = false;
        m_transformModeLabel->clear();
        m_fpStatusLabel->setText(refPts.empty() ? QString()
            : QString("%1 pair(s) — need ≥4 for homography").arg(refPts.size()));
        return;
    }

    cv::Mat H = cv::findHomography(ovPts, refPts, cv::RANSAC, 3.0);
    if (H.empty())
    {
        t.useHomography = false;
        m_transformModeLabel->clear();
        m_fpStatusLabel->setText("Homography failed — check point quality");
        return;
    }

    t.useHomography = true;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            t.homography[i * 3 + j] = H.at<double>(i, j);

    m_fpStatusLabel->setText(QString("Homography active (%1 pairs)").arg(refPts.size()));
    m_transformModeLabel->setText("Homography active (from feature pairs)");
    updateList();
}
