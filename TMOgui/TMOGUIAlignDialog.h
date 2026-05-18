#pragma once
#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QSize>
#include "TMOGUIHDRCreateWorker.h"

class QListWidget;
class QSlider;
class QPushButton;
class QLabel;
class QDoubleSpinBox;
class QTabWidget;

class AlignCanvas : public QWidget
{
    Q_OBJECT
public:
    enum Mode { Translate, Rotate, Scale };

    explicit AlignCanvas(QWidget* parent = nullptr);

    void setReference(const QImage& img);
    void setOverlay(const QImage& img, AlignTransform* t);
    void setOverlayOrigScale(float s) { m_overlayOrigScale = s; }
    void clearOverlay();
    void setOpacity(float o) { m_opacity = o; update(); }
    void setMode(Mode m) { m_mode = m; }
    void setViewScale(float s) { m_viewScale = qBound(0.05f, s, 20.0f); update(); emit viewScaleChanged(m_viewScale); }
    float viewScale() const { return m_viewScale; }

signals:
    void transformChanged();
    void viewScaleChanged(float scale);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    float displayScale() const;

    QImage m_reference;
    QImage m_overlay;
    AlignTransform* m_transform = nullptr;
    float m_opacity = 0.5f;
    float m_overlayOrigScale = 1.0f;
    Mode m_mode = Translate;
    bool m_dragging = false;
    QPointF m_lastPos;
    float m_viewScale = 1.0f;
    QPointF m_panOffset;
    bool m_panning = false;
    QPointF m_panLastPos;
};

class FeaturePointCanvas : public QWidget
{
    Q_OBJECT
public:
    struct PointPair {
        QPointF refPt = {-1, -1};
        QPointF overlayPt = {-1, -1};
        bool autoDetected = false;
        bool complete() const { return refPt.x() >= 0 && overlayPt.x() >= 0; }
    };

    enum AddState { Idle, PlacingRef, PlacingOverlay };

    explicit FeaturePointCanvas(QWidget* parent = nullptr);

    void setImages(const QImage& reference, const QImage& overlay);
    void clearImages();

    void beginAddPair();
    void cancelAdd();
    void deleteSelected();
    void clearAll();

    QVector<PointPair>& pairs() { return m_pairs; }
    const QVector<PointPair>& pairs() const { return m_pairs; }
    bool hasSelection() const { return m_selectedIdx >= 0; }
    AddState addState() const { return m_addState; }

    void setViewScale(float s) { m_viewScale = qBound(0.05f, s, 20.0f); update(); emit viewScaleChanged(m_viewScale); }
    float viewScale() const { return m_viewScale; }

signals:
    void pairsChanged();
    void statusMessage(QString msg);
    void viewScaleChanged(float scale);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    struct Layout {
        QRectF refRect;
        QRectF ovRect;
    };

    Layout computeLayout() const;
    QPointF refNormToScreen(QPointF n, const Layout& l) const;
    QPointF ovNormToScreen(QPointF n, const Layout& l) const;
    QPointF screenToRefNorm(QPointF p, const Layout& l) const;
    QPointF screenToOvNorm(QPointF p, const Layout& l) const;
    int hitTest(QPointF screenPos, const Layout& l, bool* hitRef) const;
    void emitStatus();

    QImage m_reference;
    QImage m_overlay;
    QVector<PointPair> m_pairs;
    int m_selectedIdx = -1;
    AddState m_addState = Idle;
    QPointF m_pendingRefPt;
    QPointF m_hoverPos;
    bool m_hovering = false;
    float m_viewScale = 1.0f;
    QPointF m_panOffset;
    bool m_panning = false;
    QPointF m_panLastPos;

    static const int kRadius = 8;
    static const QColor kColors[10];
};

// Main dialog
class TMOGUIAlignDialog : public QDialog
{
    Q_OBJECT
public:
    TMOGUIAlignDialog(const QStringList& imagePaths,
                      QVector<AlignTransform>& transforms,
                      QVector<QVector<FeaturePointCanvas::PointPair>>& pointPairs,
                      QWidget* parent = nullptr);

private slots:
    void onImageSelected(int row);
    void onSetReference();

    // Transform tab
    void onReset();
    void onOpacityChanged(int value);
    void onTransformChanged();
    void onSpinChanged();

    // Feature Points tab
    void onAddPair();
    void onDeletePair();
    void onClearPairs();
    void onDetectSIFT();
    void onPairsChanged();
    void onTabChanged(int index);

private:
    void buildUI();
    void loadPreviewImages();
    void selectImage(int idx);
    void updateList();
    void updateSpinBoxes();
    void updateModeButtons(AlignCanvas::Mode m);
    void updateFeatureControls();
    void tryComputeHomography();

    QStringList m_imagePaths;
    QVector<AlignTransform>& m_transforms;
    QVector<QVector<FeaturePointCanvas::PointPair>>& m_pointPairsRef;
    QVector<QImage> m_previews;
    QVector<QSize> m_originalSizes;
    int m_referenceIdx = 0;
    int m_currentIdx = -1;

    // Left panel
    QListWidget* m_list = nullptr;
    QPushButton* m_setRefBtn = nullptr;

    // Right panel tabs
    QTabWidget* m_tabs = nullptr;

    // Transform tab
    AlignCanvas* m_canvas = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QSlider* m_zoomSlider = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_translateBtn = nullptr;
    QPushButton* m_rotateBtn = nullptr;
    QPushButton* m_scaleBtn = nullptr;
    QDoubleSpinBox* m_txSpin = nullptr;
    QDoubleSpinBox* m_tySpin = nullptr;
    QDoubleSpinBox* m_angleSpin = nullptr;
    QDoubleSpinBox* m_scaleSpin = nullptr;
    QLabel* m_transformModeLabel = nullptr;

    // Feature Points tab
    FeaturePointCanvas* m_fpCanvas = nullptr;
    QSlider* m_fpZoomSlider = nullptr;
    QLabel*  m_fpZoomLabel  = nullptr;
    QPushButton* m_addPairBtn = nullptr;
    QPushButton* m_deletePairBtn = nullptr;
    QPushButton* m_clearPairsBtn = nullptr;
    QPushButton* m_detectSIFTBtn = nullptr;
    QLabel* m_fpStatusLabel = nullptr;

    bool m_suppressSpinSignals = false;
    bool m_suppressPairsSave  = false;
};
