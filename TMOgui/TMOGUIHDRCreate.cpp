#include "TMOGUIHDRCreate.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QStackedWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QProgressBar>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QMimeData>
#include <QSet>
#include <QUrl>
#include <QDateTime>
#include <QButtonGroup>
#include <QWheelEvent>


// Spin box that doesn't steal scroll events so the table scrolls normally + trims trailing zeros from the displayed value
class NoScrollSpinBox : public QDoubleSpinBox
{
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
    QString textFromValue(double val) const override
    {
        QString s = QString::number(val, 'f', decimals());
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
        return s;
    }
};

TMOGUIHDRCreate::TMOGUIHDRCreate(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Create HDR Image");
    setMinimumWidth(1000);
    setAcceptDrops(true);
    buildUI();
}

void TMOGUIHDRCreate::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // Image list
    m_imagesGroup = new QGroupBox("Input Images", this);
    auto* imagesGroup = m_imagesGroup;
    auto* imagesLayout = new QVBoxLayout(imagesGroup);

    m_imageTable = new QTableWidget(0, 2, this);
    m_imageTable->setHorizontalHeaderLabels({"File Name", "Exposure Time"});
    m_imageTable->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_imageTable->horizontalHeaderItem(1)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_imageTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_imageTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_imageTable->horizontalHeader()->resizeSection(1, 160);
    m_imageTable->horizontalHeader()->setSectionsClickable(false);
    m_imageTable->verticalHeader()->setStyleSheet(
        "QHeaderView::section {"
        "  border-top: none;"
        "  border-left: none;"
        "  border-bottom: 1px solid palette(mid);"
        "  border-right: 1px solid palette(dark);"
        "  padding: 0px 4px;"
        "}"
    );
    m_imageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_imageTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    m_imageTable->setAcceptDrops(false);
    m_imageTable->setMinimumHeight(140);

    m_dropLabel = new QLabel("Drop images here", m_imageTable->viewport());
    m_dropLabel->setAlignment(Qt::AlignCenter);
    m_dropLabel->setStyleSheet("color: palette(mid); font-size: 14px;");
    m_dropLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropLabel->setGeometry(m_imageTable->viewport()->rect());
    m_imageTable->viewport()->installEventFilter(this);

    imagesLayout->addWidget(m_imageTable);

    auto* imgButtonLayout = new QHBoxLayout;
    imgButtonLayout->setSpacing(18);
    imgButtonLayout->addStretch();
    auto* addButton = new QPushButton("Add Images...", this);
    m_removeButton = new QPushButton("Remove", this);
    m_removeButton->setEnabled(false);
    imgButtonLayout->addWidget(addButton);
    imgButtonLayout->addWidget(m_removeButton);
    imagesLayout->addLayout(imgButtonLayout);

    imagesGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(10);
    contentLayout->addWidget(imagesGroup, 1);
    auto* rightWidget = new QWidget(this);
    rightWidget->setMaximumWidth(440);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);
    contentLayout->addWidget(rightWidget, 0);
    mainLayout->addLayout(contentLayout);

    connect(addButton,      &QPushButton::clicked, this, &TMOGUIHDRCreate::addImages);
    connect(m_removeButton, &QPushButton::clicked, this, &TMOGUIHDRCreate::removeImage);
    connect(m_imageTable,   &QTableWidget::itemSelectionChanged, this, [this]() {
        int count = m_imageTable->selectionModel()->selectedRows().count();
        m_removeButton->setEnabled(count > 0);
        m_removeButton->setText(count > 1 ? QString("Remove (%1)").arg(count) : "Remove");
    });
    connect(m_imageTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        updateCreateButton();
    });

    // Algorithm selection
    m_algoGroup = new QGroupBox("Merge Algorithm", this);
    auto* algoGroup = m_algoGroup;
    auto* algoLayout = new QVBoxLayout(algoGroup);

    auto* algoRow = new QHBoxLayout;
    algoRow->addWidget(new QLabel("Method:", this));
    m_algorithmCombo = new QComboBox(this);
    m_algorithmCombo->addItem("Debevec (calibrated response)");
    m_algorithmCombo->addItem("HDR+ (Hasinoff 2016)");
    m_algorithmCombo->addItem("Exposure Fusion (DIS optical flow)");
    m_algorithmCombo->addItem("SAFNet (deep learning, 3 images)");
    algoRow->addWidget(m_algorithmCombo, 1);
    algoLayout->addLayout(algoRow);

    m_paramStack = new QStackedWidget(this);

    // Item 0 — Debevec
    auto* debevecPage = new QWidget;
    auto* debevecLayout = new QVBoxLayout(debevecPage);
    debevecLayout->addWidget(new QLabel("Requires images with <b>different</b> exposure times.\n"
                                        "Set the Exposure Time column to the actual shutter speeds (e.g. 0.004 s and 0.25 s).", debevecPage));
    debevecLayout->addStretch();
    m_paramStack->addWidget(debevecPage);

    // Item 1 — HDR+
    auto* hdrplusPage = new QWidget;
    auto* hdrplusForm = new QFormLayout(hdrplusPage);
    hdrplusForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    hdrplusForm->setLabelAlignment(Qt::AlignLeft);
    hdrplusForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_hdrplusCSpinBox = new NoScrollSpinBox(hdrplusPage);
    m_hdrplusCSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_hdrplusCSpinBox->setRange(0.001, 20.0);
    m_hdrplusCSpinBox->setDecimals(3);
    m_hdrplusCSpinBox->setSingleStep(0.1);
    m_hdrplusCSpinBox->setValue(2.0);
    m_hdrplusCSpinBox->setToolTip("Wiener filter aggressiveness — higher = more denoising");
    hdrplusForm->addRow("Denoise strength:", m_hdrplusCSpinBox);
    m_paramStack->addWidget(hdrplusPage);

    // Item 2 — DIS
    auto* disPage = new QWidget;
    auto* disForm = new QFormLayout(disPage);
    disForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    disForm->setLabelAlignment(Qt::AlignLeft);
    disForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_disNoiseSpinBox = new QDoubleSpinBox(disPage);
    m_disNoiseSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_disNoiseSpinBox->setRange(0.001, 1.0);
    m_disNoiseSpinBox->setSingleStep(0.005);
    m_disNoiseSpinBox->setDecimals(3);
    m_disNoiseSpinBox->setValue(0.05);
    m_disNoiseSpinBox->setToolTip("Wiener noise variance — higher = more smoothing");
    disForm->addRow("Noise variance:", m_disNoiseSpinBox);
    m_paramStack->addWidget(disPage);

    // Item 3 — SAFNet
    auto* safnetPage = new QWidget;
    auto* safnetForm = new QFormLayout(safnetPage);
    safnetForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    safnetForm->setLabelAlignment(Qt::AlignLeft);
    safnetForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_safnetModelEdit = new QLineEdit(QDir::currentPath() + "/SAFNet.onnx", safnetPage);
    m_safnetModelEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* modelRow = new QHBoxLayout;
    modelRow->setSpacing(6);
    modelRow->addWidget(m_safnetModelEdit);
    auto* browseBtn = new QPushButton("Browse...", safnetPage);
    browseBtn->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    modelRow->addWidget(browseBtn);
    safnetForm->addRow("Model (.onnx):", modelRow);

    m_safnetTileSpinBox = new QSpinBox(safnetPage);
    m_safnetTileSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_safnetTileSpinBox->setRange(256, 8192);
    m_safnetTileSpinBox->setSingleStep(128);
    m_safnetTileSpinBox->setValue(2048);
    safnetForm->addRow("Tile size (px):", m_safnetTileSpinBox);

    m_safnetOverlapSpinBox = new QSpinBox(safnetPage);
    m_safnetOverlapSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_safnetOverlapSpinBox->setRange(0, 512);
    m_safnetOverlapSpinBox->setSingleStep(8);
    m_safnetOverlapSpinBox->setValue(128);
    safnetForm->addRow("Overlap (px):", m_safnetOverlapSpinBox);

    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Select SAFNet Model",
                                                    QString(), "ONNX Model (*.onnx)");
        if (!path.isEmpty())
            m_safnetModelEdit->setText(path);
    });
    m_paramStack->addWidget(safnetPage);

    algoLayout->addWidget(m_paramStack);
    m_regBuiltinLabel = new QLabel(algoGroup);
    m_regBuiltinLabel->setStyleSheet("font-style: italic;");
    m_regBuiltinLabel->setContentsMargins(4, 0, 0, 0);
    algoLayout->addWidget(m_regBuiltinLabel);
    algoGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    rightLayout->addWidget(algoGroup);

    connect(m_algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TMOGUIHDRCreate::onAlgorithmChanged);

    // Registration
    m_regGroup = new QGroupBox("Register Images Before Merging", this);
    m_regGroup->setCheckable(true);
    m_regGroup->setChecked(true);
    auto* regVBox = new QVBoxLayout(m_regGroup);

    // Registration mode toggle (automatic or manual)
    auto* modeRow = new QHBoxLayout;
    modeRow->setSpacing(0);
    m_regAutoBtn = new QPushButton("Automatic", m_regGroup);
    m_regManualBtn = new QPushButton("Manual", m_regGroup);
    m_regAutoBtn->setCheckable(true);
    m_regManualBtn->setCheckable(true);
    m_regAutoBtn->setChecked(true);
    m_regAutoBtn->setStyleSheet(
        "QPushButton { border: 1px solid palette(mid); border-right: none;"
        "  border-top-left-radius: 3px; border-bottom-left-radius: 3px;"
        "  border-top-right-radius: 0; border-bottom-right-radius: 0; padding: 3px 14px; }"
        "QPushButton:checked { background: palette(highlight); color: palette(highlighted-text); }"
        "QPushButton:checked:disabled { background: palette(mid); color: palette(window); }"
    );
    m_regManualBtn->setStyleSheet(
        "QPushButton { border: 1px solid palette(mid);"
        "  border-top-left-radius: 0; border-bottom-left-radius: 0;"
        "  border-top-right-radius: 3px; border-bottom-right-radius: 3px; padding: 3px 14px; }"
        "QPushButton:checked { background: palette(highlight); color: palette(highlighted-text); }"
        "QPushButton:checked:disabled { background: palette(mid); color: palette(window); }"
    );
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_regAutoBtn);
    modeGroup->addButton(m_regManualBtn);
    modeGroup->setExclusive(true);
    modeRow->addWidget(m_regAutoBtn);
    modeRow->addWidget(m_regManualBtn);
    modeRow->addStretch();
    regVBox->addLayout(modeRow);

    // Automatic registration container
    m_regAutoContainer = new QWidget(m_regGroup);
    auto* regForm = new QFormLayout(m_regAutoContainer);
    regForm->setContentsMargins(0, 4, 0, 0);
    regForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    regForm->setLabelAlignment(Qt::AlignLeft);
    regForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_regMethodCombo = new QComboBox(m_regAutoContainer);
    m_regMethodCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_regMethodCombo->addItem("Median Threshold Bitmap");
    m_regMethodCombo->addItem("SIFT + Homography");
    m_regMethodCombo->addItem("DIS Optical Flow");
    regForm->addRow("Method:", m_regMethodCombo);
    regVBox->addWidget(m_regAutoContainer);

    // Manual alignment container
    m_alignContainer = new QWidget(m_regGroup);
    auto* alignRow = new QHBoxLayout(m_alignContainer);
    alignRow->setContentsMargins(0, 4, 0, 0);
    m_alignBtn = new QPushButton("Align Images...", m_alignContainer);
    m_alignBtn->setToolTip("Open the interactive alignment editor to manually adjust translation, rotation, and scale for each image");
    m_alignBtn->setEnabled(false);
    m_alignStatusLabel = new QLabel(m_alignContainer);
    m_alignStatusLabel->setStyleSheet("font-style: italic; color: palette(mid);");
    alignRow->addWidget(m_alignBtn);
    alignRow->addWidget(m_alignStatusLabel, 1);
    m_alignContainer->hide();
    regVBox->addWidget(m_alignContainer);

    m_regGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    rightLayout->addWidget(m_regGroup);

    connect(m_regGroup,      &QGroupBox::toggled, this, &TMOGUIHDRCreate::onRegistrationToggled);
    connect(m_regAutoBtn,    &QPushButton::toggled, this, &TMOGUIHDRCreate::onRegModeChanged);
    connect(m_regMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TMOGUIHDRCreate::onRegMethodChanged);
    connect(m_alignBtn, &QPushButton::clicked, this, &TMOGUIHDRCreate::openAlignDialog);

    rightLayout->addStretch();

    // Progress
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setStyleSheet(
        "QProgressBar {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 3px;"
        "  background: palette(base);"
        "  text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "  background: palette(highlight);"
        "  border-radius: 2px;"
        "}"
    );
    m_progressEffect = new QGraphicsOpacityEffect(m_progressBar);
    m_progressEffect->setOpacity(0.0);
    m_progressBar->setGraphicsEffect(m_progressEffect);
    mainLayout->addWidget(m_progressBar);

    // Buttons
    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(18);
    m_statusLabel = new QLabel(this);
    m_statusLabel->hide();
    buttonLayout->addWidget(m_statusLabel);
    buttonLayout->addStretch();
    m_cancelButton = new QPushButton("Cancel", this);
    m_createButton = new QPushButton("Create", this);
    m_createButton->setDefault(true);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_createButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_createButton, &QPushButton::clicked, this, &TMOGUIHDRCreate::onCreate);
    connect(m_cancelButton, &QPushButton::clicked, this, &TMOGUIHDRCreate::onCancel);

    updateCreateButton();
}

QDoubleSpinBox* TMOGUIHDRCreate::makeExposureSpinBox()
{
    auto* sb = new NoScrollSpinBox(m_imageTable);
    sb->setRange(0.001, 3600.0);
    sb->setSingleStep(1.0);
    sb->setDecimals(3);
    sb->setValue(1.0);
    sb->setSuffix(" s");
    sb->setFrame(false);
    sb->setFocusPolicy(Qt::StrongFocus);
    return sb;
}

void TMOGUIHDRCreate::addImagePaths(const QStringList& paths)
{
    QSet<QString> existing;
    for (int i = 0; i < m_imageTable->rowCount(); i++)
        existing.insert(m_imageTable->item(i, 0)->data(Qt::UserRole).toString());

    int row = m_imageTable->rowCount();
    for (const QString& path : paths)
    {
        if (existing.contains(path))
            continue;
        existing.insert(path);
        m_imageTable->setRowCount(row + 1);
        auto* pathItem = new QTableWidgetItem(QFileInfo(path).fileName());
        pathItem->setData(Qt::UserRole, path);
        pathItem->setToolTip(path);
        pathItem->setFlags(pathItem->flags() & ~Qt::ItemIsEditable);
        m_imageTable->setItem(row, 0, pathItem);
        m_imageTable->setCellWidget(row, 1, makeExposureSpinBox());
        row++;
    }

    updateTableExposureVisibility();
    updateDropLabel();
    updateCreateButton();
}

void TMOGUIHDRCreate::addImages()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, "Select Input Images", QString(),
        "Images (*.jpg *.jpeg *.png *.tif *.tiff *.bmp);;All Files (*)");

    if (paths.isEmpty())
        return;

    addImagePaths(paths);
}

void TMOGUIHDRCreate::removeImage()
{
    QList<QTableWidgetSelectionRange> ranges = m_imageTable->selectedRanges();
    QList<int> rows;
    for (const auto& r : ranges)
        for (int i = r.topRow(); i <= r.bottomRow(); i++)
            if (!rows.contains(i)) rows.append(i);
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows)
    {
        m_imageTable->removeRow(r);
        if (r < m_manualTransforms.size())
            m_manualTransforms.removeAt(r);
        if (r < m_pointPairs.size())
            m_pointPairs.removeAt(r);
    }

    updateDropLabel();
    updateCreateButton();
}

void TMOGUIHDRCreate::updateDropLabel()
{
    bool empty = m_imageTable->rowCount() == 0;
    m_dropLabel->setVisible(empty);
    if (empty)
        m_dropLabel->setGeometry(m_imageTable->viewport()->rect());
}

bool TMOGUIHDRCreate::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_imageTable->viewport() && event->type() == QEvent::Resize)
        m_dropLabel->setGeometry(m_imageTable->viewport()->rect());
    return QDialog::eventFilter(obj, event);
}

void TMOGUIHDRCreate::onAlgorithmChanged(int index)
{
    m_paramStack->setCurrentIndex(index);
    updateTableExposureVisibility();

    bool isDebevec = (index == 0);
    if (isDebevec)
    {
        m_regGroup->setEnabled(true);
        m_regGroup->setChecked(true);
        m_regBuiltinLabel->clear();
    }
    else
    {
        m_regGroup->setChecked(false);
        m_regGroup->setEnabled(false);
        m_regBuiltinLabel->setText("This method includes built-in registration.");
    }

    updateCreateButton();
}

void TMOGUIHDRCreate::updateTableExposureVisibility()
{
    // Exposure times are mandatory for Debevec
    bool debevec = (m_algorithmCombo->currentIndex() == 0);
    m_imageTable->horizontalHeaderItem(1)->setText(
        debevec ? "Exposure Time *" : "Exposure Time (optional)");
}

void TMOGUIHDRCreate::onRegistrationToggled(bool)
{
    updateCreateButton();
}

void TMOGUIHDRCreate::onRegModeChanged()
{
    bool manual = m_regManualBtn && m_regManualBtn->isChecked();
    if (m_regAutoContainer)
        m_regAutoContainer->setVisible(!manual);
    if (m_alignContainer)
        m_alignContainer->setVisible(manual);
    updateCreateButton();
}

void TMOGUIHDRCreate::onRegMethodChanged(int)
{
    updateCreateButton();
}

void TMOGUIHDRCreate::updateCreateButton()
{
    int n = m_imageTable->rowCount();
    int algo = m_algorithmCombo->currentIndex();
    bool regEnabled = m_regGroup->isChecked();
    bool regManual = m_regManualBtn && m_regManualBtn->isChecked();
    int regMethod = m_regMethodCombo->currentIndex();

    bool ok = true;
    QString tip;

    if (n == 0)
    {
        ok = false;
        tip = "Add images to proceed.";
    }
    else if (n < 2)
    {
        ok = false;
        tip = "Add at least 2 images.";
    }
    else if (algo == 3 && n != 3)
    {
        ok = false;
        tip = "SAFNet requires exactly 3 images.";
    }


    m_createButton->setEnabled(ok);
    m_createButton->setToolTip(ok ? QString() : tip);
    m_statusLabel->setText(ok ? QString() : tip);
    m_statusLabel->setStyleSheet("color: red;");
    m_statusLabel->setVisible(!ok);

    if (m_alignBtn)
        m_alignBtn->setEnabled(n >= 2);

    if (m_manualTransforms.size() != n)
        m_manualTransforms.resize(n);
    if (m_pointPairs.size() != n)
        m_pointPairs.resize(n);

    if (m_alignStatusLabel)
    {
        int modified = 0;
        for (const auto& t : m_manualTransforms)
            if (!t.isIdentity()) modified++;
        if (modified > 0)
            m_alignStatusLabel->setText(QString("%1 image(s) manually adjusted").arg(modified));
        else
            m_alignStatusLabel->setText("No manual transforms set");
    }
}

void TMOGUIHDRCreate::setProcessing(bool processing)
{
    m_imagesGroup->setEnabled(!processing);
    m_algoGroup->setEnabled(!processing);
    m_cancelButton->setEnabled(true);
    if (processing)
    {
        m_regGroup->setEnabled(false);
        m_createButton->setEnabled(false);
    }
    else
    {
        onAlgorithmChanged(m_algorithmCombo->currentIndex());
    }
}

void TMOGUIHDRCreate::openAlignDialog()
{
    QStringList paths;
    for (int i = 0; i < m_imageTable->rowCount(); i++)
        paths.append(m_imageTable->item(i, 0)->data(Qt::UserRole).toString());

    m_manualTransforms.resize(paths.size());
    m_pointPairs.resize(paths.size());

    TMOGUIAlignDialog dlg(paths, m_manualTransforms, m_pointPairs, this);
    dlg.exec();

    updateCreateButton();
}

TMOGUIHDRCreateParams TMOGUIHDRCreate::collectParams() const
{
    TMOGUIHDRCreateParams p;

    int algo = m_algorithmCombo->currentIndex();
    switch (algo)
    {
    case 1: p.method = TMOGUIHDRCreateParams::HDRPLUS; break;
    case 2: p.method = TMOGUIHDRCreateParams::DIS;     break;
    case 3: p.method = TMOGUIHDRCreateParams::SAFNET;  break;
    default: p.method = TMOGUIHDRCreateParams::DEBEVEC; break;
    }

    for (int i = 0; i < m_imageTable->rowCount(); i++)
    {
        p.imagePaths.append(m_imageTable->item(i, 0)->data(Qt::UserRole).toString());
        auto* sb = qobject_cast<QDoubleSpinBox*>(m_imageTable->cellWidget(i, 1));
        p.exposureTimes.append(sb ? sb->value() : 1.0);
    }

    QString hdrTmpDir = QDir::tempPath() + "/tms_hdr";
    QDir().mkpath(hdrTmpDir);
    p.outputPath  = hdrTmpDir + "/tms_hdr_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".tiff";
    p.hdrplusC    = m_hdrplusCSpinBox->value();
    p.disNoise    = m_disNoiseSpinBox->value();
    p.safnetModel = m_safnetModelEdit->text();
    p.safnetTile  = m_safnetTileSpinBox->value();
    p.safnetOverlap = m_safnetOverlapSpinBox->value();

    int regMethod = m_regMethodCombo->currentIndex();
    bool regManual = m_regManualBtn && m_regManualBtn->isChecked();
    p.doRegistration = m_regGroup->isChecked() && !regManual;
    switch (regMethod)
    {
    case 1: p.registrationMethod = TMOGUIHDRCreateParams::REG_HOMOGRAPHY;   break;
    case 2: p.registrationMethod = TMOGUIHDRCreateParams::REG_OPTICAL_FLOW; break;
    default: p.registrationMethod = TMOGUIHDRCreateParams::REG_MTB;         break;
    }

    p.manualTransforms = m_manualTransforms;

    return p;
}

void TMOGUIHDRCreate::onCreate()
{
    m_cancelButton->setText("Stop");
    setProcessing(true);
    m_progressBar->setValue(0);
    m_progressEffect->setOpacity(1.0);
    m_statusLabel->setStyleSheet({});
    m_statusLabel->setText("Processing...");
    m_statusLabel->show();

    m_worker = new TMOGUIHDRCreateWorker(collectParams(), nullptr);
    connect(m_worker, &TMOGUIHDRCreateWorker::progress, this, &TMOGUIHDRCreate::onProgress);
    connect(m_worker, &TMOGUIHDRCreateWorker::done,     this, &TMOGUIHDRCreate::onDone);
    connect(m_worker, &TMOGUIHDRCreateWorker::failed,   this, &TMOGUIHDRCreate::onFailed);
    connect(m_worker, &QThread::finished, this, [this]() { m_worker = nullptr; });
    connect(m_worker, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}

void TMOGUIHDRCreate::closeEvent(QCloseEvent* event)
{
    if (m_worker && m_worker->isRunning())
    {
        QMessageBox box(QMessageBox::Question, "Stop Task",
            "Stop the running task and close?",
            QMessageBox::Yes | QMessageBox::No, this);
        for (auto* btn : box.buttons())
            if (auto* pb = qobject_cast<QPushButton *>(btn))
            {
                pb->setDefault(false);
                pb->setAutoDefault(false);
            }
        QTimer::singleShot(0, &box, [&box]() { box.setFocus(); });
        auto result = box.exec();
        if (result == QMessageBox::Yes)
        {
            m_worker->terminate();
            m_worker->wait();
            event->accept();
        }
        else
        {
            event->ignore();
        }
        return;
    }
    event->accept();
}

void TMOGUIHDRCreate::onCancel()
{
    if (m_worker && m_worker->isRunning())
    {
        m_worker->disconnect(this);
        m_worker->terminate();
        m_worker = nullptr;
        m_progressBar->setValue(0);
        m_progressEffect->setOpacity(0.0);
        m_statusLabel->hide();
        m_cancelButton->setText("Cancel");
        setProcessing(false);
        return;
    }
    reject();
}

void TMOGUIHDRCreate::onProgress(int percent, QString stage)
{
    m_progressBar->setValue(percent);
    if (!stage.isEmpty())
        m_statusLabel->setText(stage);
}

void TMOGUIHDRCreate::onDone(QString path)
{
    m_resultPath = path;
    m_progressBar->setValue(100);
    m_statusLabel->setText("Done.");
    accept();
}

void TMOGUIHDRCreate::onFailed(QString message)
{
    m_progressBar->setValue(0);
    m_progressEffect->setOpacity(0.0);
    m_statusLabel->hide();
    m_cancelButton->setText("Cancel");
    setProcessing(false);
    QMessageBox::critical(this, "HDR Creation Failed", message);
}

void TMOGUIHDRCreate::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
    {
        event->ignore();
        return;
    }
    static const QStringList imageExts = {"jpg", "jpeg", "png", "tif", "tiff", "bmp"};
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile() &&
            imageExts.contains(QFileInfo(url.toLocalFile()).suffix().toLower()))
        {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void TMOGUIHDRCreate::dropEvent(QDropEvent* event)
{
    static const QStringList imageExts = {"jpg", "jpeg", "png", "tif", "tiff", "bmp"};
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (!url.isLocalFile()) continue;
        QString path = url.toLocalFile();
        if (imageExts.contains(QFileInfo(path).suffix().toLower()))
            paths.append(path);
    }

    if (paths.isEmpty()) { event->ignore(); return; }

    addImagePaths(paths);
    event->acceptProposedAction();
}
