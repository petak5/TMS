#pragma once
#include <QDialog>
#include <QString>
#include <QDragEnterEvent>
#include <QCloseEvent>
#include <QDropEvent>
#include <QEvent>
#include <QVector>
#include "TMOGUIHDRCreateWorker.h"
#include "TMOGUIAlignDialog.h"

class QWidget;
class QTableWidget;
class QComboBox;
class QStackedWidget;
class QGroupBox;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QProgressBar;
class QGraphicsOpacityEffect;
class QLabel;
class QPushButton;

class TMOGUIHDRCreate : public QDialog
{
    Q_OBJECT
public:
    explicit TMOGUIHDRCreate(QWidget* parent = nullptr);

    // Valid only after accepted() signal; contains the output path of the created file.
    QString resultPath() const { return m_resultPath; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void addImages();
    void removeImage();
    void onAlgorithmChanged(int index);
    void onRegistrationToggled(bool checked);
    void onRegModeChanged();
    void onRegMethodChanged(int index);
    void openAlignDialog();
    void onCreate();
    void onCancel();
    void onProgress(int percent, QString stage);
    void onDone(QString path);
    void onFailed(QString message);
    void updateTableExposureVisibility();

private:
    void buildUI();
    void setProcessing(bool processing);
    void updateCreateButton();
    void updateDropLabel();
    TMOGUIHDRCreateParams collectParams() const;
    QDoubleSpinBox* makeExposureSpinBox();
    void addImagePaths(const QStringList& paths);

    QGroupBox*      m_imagesGroup;
    QGroupBox*      m_algoGroup;
    QTableWidget*   m_imageTable;
    QComboBox*      m_algorithmCombo;
    QStackedWidget* m_paramStack;

    // HDR+ params
    QDoubleSpinBox* m_hdrplusCSpinBox;

    // DIS params
    QDoubleSpinBox* m_disNoiseSpinBox;

    // SAFNet params
    QLineEdit*  m_safnetModelEdit;
    QSpinBox*   m_safnetTileSpinBox;
    QSpinBox*   m_safnetOverlapSpinBox;

    // Registration
    QGroupBox*   m_regGroup;
    QPushButton* m_regAutoBtn = nullptr;
    QPushButton* m_regManualBtn = nullptr;
    QWidget*     m_regAutoContainer = nullptr;
    QComboBox*   m_regMethodCombo;
    QLabel*      m_regBuiltinLabel = nullptr;

    // Progress
    QProgressBar*           m_progressBar;
    QLabel*                 m_statusLabel;
    QGraphicsOpacityEffect* m_progressEffect = nullptr;

    QPushButton* m_createButton;
    QPushButton* m_cancelButton;
    QPushButton* m_removeButton;

    QLabel* m_dropLabel = nullptr;

    // Manual alignment
    QWidget*     m_alignContainer = nullptr;
    QPushButton* m_alignBtn = nullptr;
    QLabel*      m_alignStatusLabel = nullptr;
    QVector<AlignTransform> m_manualTransforms;
    QVector<QVector<FeaturePointCanvas::PointPair>> m_pointPairs;

    TMOGUIHDRCreateWorker* m_worker = nullptr;
    QString m_resultPath;
};
