#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QApplication>
#include <QPalette>
#include <QColor>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_modelManager(new ModelManager(this))
    , m_settingsManager(new SettingsManager(this))
    , m_faceDetectionController(nullptr)
{
    ui->setupUi(this);
    
    // Hide stream combobox by default since camera is the default source
    ui->streamComboBox->setVisible(false);
    
    // Load settings from file
    m_settingsManager->loadSettings();
    ui->modelPathEdit->setText(m_settingsManager->getModelPath());
    
    // Scan model directory if path exists
    if (!ui->modelPathEdit->text().isEmpty()) {
        QList<QString> models = m_modelManager->scanModelDirectory(ui->modelPathEdit->text());
        ui->modelListWidget->clear();
        for (const QString &model : models) {
            QListWidgetItem *item = new QListWidgetItem(model);
            ui->modelListWidget->addItem(item);
        }
    }
    
    updateStreamComboBox();
    updateStreamTable();
    
    // Initialize face detection controller with the VideoWidget from UI
    m_faceDetectionController = new FaceDetectionController(m_modelManager, ui->videoWidget, this);
    
    // Connect signals and slots
    connect(ui->modelPathButton, &QPushButton::clicked, this, &MainWindow::onModelPathButtonClicked);
    connect(ui->loadModelButton, &QPushButton::clicked, this, &MainWindow::onLoadModelClicked);
    connect(ui->modelListWidget, &QListWidget::itemSelectionChanged, this, &MainWindow::onModelSelectionChanged);
    connect(ui->sourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onSourceChanged);
    connect(ui->streamComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onStreamSelected);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::onStartButtonClicked);
    connect(ui->stopButton, &QPushButton::clicked, this, &MainWindow::onStopButtonClicked);
    connect(ui->addStreamButton, &QPushButton::clicked, this, &MainWindow::onAddStreamClicked);
    connect(ui->removeStreamButton, &QPushButton::clicked, this, &MainWindow::onRemoveStreamClicked);
    connect(ui->streamTable, &QTableWidget::cellChanged, this, &MainWindow::onStreamTableChanged);
    
    // Connect parameter change signals
    connect(ui->enableRecognitionCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onParameterChanged);
    connect(ui->enableLivenessCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onParameterChanged);
    connect(ui->enableMaskDetectCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onParameterChanged);
    connect(ui->enableFaceAttributeCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onParameterChanged);
    connect(ui->enableFaceQualityCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onParameterChanged);
    connect(ui->enableIrLivenessCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onParameterChanged);
    connect(ui->enableInteractionLivenessCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onParameterChanged);
    connect(ui->enableDetectModeLandmarkCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onParameterChanged);
            
    // Load saved parameters
    loadModelParameters();
}

MainWindow::~MainWindow()
{
    delete ui;
    delete m_faceDetectionController;
}

void MainWindow::updateStreamComboBox()
{
    ui->streamComboBox->clear();
    for (int i = 0; i < m_settingsManager->getStreamCount(); ++i) {
        QJsonObject stream = m_settingsManager->getStream(i);
        ui->streamComboBox->addItem(stream["name"].toString());
    }
}

void MainWindow::updateStreamTable()
{
    ui->streamTable->blockSignals(true);
    ui->streamTable->setRowCount(m_settingsManager->getStreamCount());
    for (int i = 0; i < m_settingsManager->getStreamCount(); ++i) {
        QJsonObject stream = m_settingsManager->getStream(i);
        QTableWidgetItem *nameItem = new QTableWidgetItem(stream["name"].toString());
        QTableWidgetItem *urlItem = new QTableWidgetItem(stream["url"].toString());
        ui->streamTable->setItem(i, 0, nameItem);
        ui->streamTable->setItem(i, 1, urlItem);
    }
    ui->streamTable->blockSignals(false);
}

void MainWindow::updateModelControls()
{
    ui->modelPathEdit->setEnabled(!m_modelManager->isModelLoaded());
    ui->modelPathButton->setEnabled(!m_modelManager->isModelLoaded());
    ui->modelListWidget->setEnabled(!m_modelManager->isModelLoaded());
    ui->loadModelButton->setText(m_modelManager->isModelLoaded() ? "Unload Model" : "Load Selected Model");
    ui->startButton->setEnabled(m_modelManager->isModelLoaded());
}

void MainWindow::onStartButtonClicked()
{
    if (!m_modelManager->isModelLoaded()) {
        QMessageBox::warning(this, "Warning", "Please load a model first");
        return;
    }

    int sourceIndex = ui->sourceComboBox->currentIndex();
    QString streamUrl;

    if (sourceIndex == 1) { // RTSP
        int selectedIndex = ui->streamComboBox->currentIndex();
        if (selectedIndex < 0 || selectedIndex >= m_settingsManager->getStreamCount()) {
            QMessageBox::warning(this, "Warning", "Please select a stream first");
            return;
        }

        QJsonObject stream = m_settingsManager->getStream(selectedIndex);
        streamUrl = stream["url"].toString();
    }

    if (m_faceDetectionController->startDetection(sourceIndex, streamUrl)) {
        ui->startButton->setEnabled(false);
        ui->stopButton->setEnabled(true);
        ui->sourceComboBox->setEnabled(false);
        ui->streamComboBox->setEnabled(false);
    }
}

void MainWindow::onStopButtonClicked()
{
    m_faceDetectionController->stopDetection();
    ui->startButton->setEnabled(true);
    ui->stopButton->setEnabled(false);
    ui->sourceComboBox->setEnabled(true);
    ui->streamComboBox->setEnabled(true);
}

void MainWindow::onSourceChanged(int index)
{
    bool isRTSP = (index == 1);
    ui->streamComboBox->setVisible(isRTSP);
    ui->streamComboBox->setEnabled(isRTSP);
}

void MainWindow::onStreamSelected(int index)
{
    Q_UNUSED(index);
    // Tidak perlu melakukan apa-apa karena URL sekarang dikelola di tab Stream Management
}

void MainWindow::onAddStreamClicked()
{
    QString name = ui->streamNameEdit->text();
    QString url = ui->streamUrlEdit->text();

    if (name.isEmpty() || url.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Stream name and URL must be filled");
        return;
    }

    m_settingsManager->addStream(name, url);
    updateStreamComboBox();
    updateStreamTable();

    ui->streamNameEdit->clear();
    ui->streamUrlEdit->clear();
}

void MainWindow::onRemoveStreamClicked()
{
    int row = ui->streamTable->currentRow();
    if (row >= 0 && row < m_settingsManager->getStreamCount()) {
        m_settingsManager->removeStream(row);
        updateStreamComboBox();
        updateStreamTable();
    }
}

void MainWindow::onModelPathButtonClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Model Directory",
                                                  ui->modelPathEdit->text(),
                                                  QFileDialog::ShowDirsOnly);
    if (!dir.isEmpty()) {
        ui->modelPathEdit->setText(dir);
        m_settingsManager->setModelPath(dir);
        
        QList<QString> models = m_modelManager->scanModelDirectory(dir);
        ui->modelListWidget->clear();
        for (const QString &model : models) {
            QListWidgetItem *item = new QListWidgetItem(model);
            ui->modelListWidget->addItem(item);
        }
    }
}

void MainWindow::onLoadModelClicked()
{
    if (m_modelManager->isModelLoaded()) {
        m_modelManager->unloadModel();
        updateModelControls();
        return;
    }

    QList<QListWidgetItem*> selectedItems = ui->modelListWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select a model first");
        return;
    }

    QString modelPath = ui->modelPathEdit->text();
    QString modelName = selectedItems.first()->text();
    QString modelFullPath = modelPath + "/" + modelName;

    // Get parameters from settings and apply to model manager's parameters
    QJsonObject params = m_settingsManager->getModelParameters();
    HFSessionCustomParameter modelParams = m_modelManager->getParameters();
    
    modelParams.enable_recognition = params["enable_recognition"].toInt();
    modelParams.enable_liveness = params["enable_liveness"].toInt();
    modelParams.enable_mask_detect = params["enable_mask_detect"].toInt();
    modelParams.enable_face_attribute = params["enable_face_attribute"].toInt();
    modelParams.enable_face_quality = params["enable_face_quality"].toInt();
    modelParams.enable_ir_liveness = params["enable_ir_liveness"].toInt();
    modelParams.enable_interaction_liveness = params["enable_interaction_liveness"].toInt();
    modelParams.enable_detect_mode_landmark = params["enable_detect_mode_landmark"].toInt();

    if (m_modelManager->loadModel(modelFullPath)) {
        updateModelControls();
    }
}

void MainWindow::onModelSelectionChanged()
{
    ui->loadModelButton->setEnabled(ui->modelListWidget->selectedItems().count() > 0);
}

void MainWindow::onStreamTableChanged(int row, int column)
{
    if (row < 0 || row >= m_settingsManager->getStreamCount()) return;

    QJsonObject stream = m_settingsManager->getStream(row);
    QString newValue = ui->streamTable->item(row, column)->text();

    if (column == 0) { // Name column
        m_settingsManager->updateStream(row, newValue, stream["url"].toString());
    } else if (column == 1) { // URL column
        m_settingsManager->updateStream(row, stream["name"].toString(), newValue);
    }

    updateStreamComboBox();
}

void MainWindow::loadModelParameters()
{
    QJsonObject params = m_settingsManager->getModelParameters();
    
    ui->enableRecognitionCheck->setChecked(params["enable_recognition"].toInt() == 1);
    ui->enableLivenessCheck->setChecked(params["enable_liveness"].toInt() == 1);
    ui->enableMaskDetectCheck->setChecked(params["enable_mask_detect"].toInt() == 1);
    ui->enableFaceAttributeCheck->setChecked(params["enable_face_attribute"].toInt() == 1);
    ui->enableFaceQualityCheck->setChecked(params["enable_face_quality"].toInt() == 1);
    ui->enableIrLivenessCheck->setChecked(params["enable_ir_liveness"].toInt() == 1);
    ui->enableInteractionLivenessCheck->setChecked(params["enable_interaction_liveness"].toInt() == 1);
    ui->enableDetectModeLandmarkCheck->setChecked(params["enable_detect_mode_landmark"].toInt() == 1);
}

void MainWindow::onParameterChanged()
{
    QJsonObject params;
    params["enable_recognition"] = ui->enableRecognitionCheck->isChecked() ? 1 : 0;
    params["enable_liveness"] = ui->enableLivenessCheck->isChecked() ? 1 : 0;
    params["enable_mask_detect"] = ui->enableMaskDetectCheck->isChecked() ? 1 : 0;
    params["enable_face_attribute"] = ui->enableFaceAttributeCheck->isChecked() ? 1 : 0;
    params["enable_face_quality"] = ui->enableFaceQualityCheck->isChecked() ? 1 : 0;
    params["enable_ir_liveness"] = ui->enableIrLivenessCheck->isChecked() ? 1 : 0;
    params["enable_interaction_liveness"] = ui->enableInteractionLivenessCheck->isChecked() ? 1 : 0;
    params["enable_detect_mode_landmark"] = ui->enableDetectModeLandmarkCheck->isChecked() ? 1 : 0;
    
    m_settingsManager->setModelParameters(params);
} 