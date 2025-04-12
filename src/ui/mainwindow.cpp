#include <QColor>
#include <libpq-fe.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QEventLoop>
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_settingsManager(new SettingsManager(this))
    , m_modelManager(nullptr)
    , m_faceDetectionController(nullptr)
    , m_faissManager(nullptr)
{
    ui->setupUi(this);
    
    // Initialize ModelManager after SettingsManager
    m_modelManager = new ModelManager(m_settingsManager, this);
    
    // Initialize FaissManager
    m_faissManager = new FaissManager(m_settingsManager, this);
    if (!m_faissManager->initialize()) {
        qDebug() << "Gagal menginisialisasi FaissManager";
    }
    
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
    connect(ui->loadModelButton, &QPushButton::clicked, this, &MainWindow::onLoadModelButtonClicked);
    connect(ui->modelListWidget, &QListWidget::itemSelectionChanged, this, &MainWindow::onModelSelectionChanged);
    connect(ui->sourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onSourceChanged);
    connect(ui->streamComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onStreamSelected);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::onStartButtonClicked);
    connect(ui->stopButton, &QPushButton::clicked, this, &MainWindow::onStopButtonClicked);
    connect(ui->addStreamButton, &QPushButton::clicked, this, &MainWindow::onAddStreamClicked);
    connect(ui->removeStreamButton, &QPushButton::clicked, this, &MainWindow::onRemoveStreamClicked);
    connect(ui->streamTable, &QTableWidget::cellChanged, this, &MainWindow::onStreamTableChanged);
    
    // Connect parameter change signals
    connect(ui->enableRecognitionCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onModelParameterChanged);
    connect(ui->enableLivenessCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onModelParameterChanged);
    connect(ui->enableMaskDetectCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onModelParameterChanged);
    connect(ui->enableFaceAttributeCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onModelParameterChanged);
    connect(ui->enableFaceQualityCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onModelParameterChanged);
    connect(ui->enableIrLivenessCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onModelParameterChanged);
    connect(ui->enableInteractionLivenessCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onModelParameterChanged);
    connect(ui->enableDetectModeLandmarkCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onModelParameterChanged);
    
    // Connect detection parameter change signals
    connect(ui->faceDetectThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onDetectionParameterChanged);
    connect(ui->trackModeSmoothRatioSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onDetectionParameterChanged);
    connect(ui->filterMinimumFacePixelSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onDetectionParameterChanged);
            
    // Connect Faiss settings signals
    connect(ui->faissCachePathButton, &QPushButton::clicked,
            this, &MainWindow::onFaissCachePathButtonClicked);
    connect(ui->saveAllSettingsButton, &QPushButton::clicked,
            this, &MainWindow::onSaveAllSettingsButtonClicked);
    connect(ui->postgresTestButton, &QPushButton::clicked,
            this, &MainWindow::onPostgresTestButtonClicked);
    connect(ui->supabaseTestButton, &QPushButton::clicked,
            this, &MainWindow::onSupabaseTestButtonClicked);
    
    // Load saved parameters
    loadModelParameters();
    loadDetectionParameters();
    loadFaissSettings();
    loadDatabaseSettings();
}

MainWindow::~MainWindow()
{
    delete ui;
    delete m_faceDetectionController;
    delete m_faissManager;
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

void MainWindow::onLoadModelButtonClicked()
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

    // Update model path in settings (only the directory path)
    m_settingsManager->setModelPath(modelPath);

    if (m_modelManager->loadModel()) {
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

void MainWindow::onModelParameterChanged()
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

void MainWindow::loadDetectionParameters()
{
    QJsonObject detectionParams = m_settingsManager->getDetectionParameters();
    
    // Block signals while setting values
    ui->faceDetectThresholdSpin->blockSignals(true);
    ui->trackModeSmoothRatioSpin->blockSignals(true);
    ui->filterMinimumFacePixelSizeSpin->blockSignals(true);
    
    // Set values from settings
    ui->faceDetectThresholdSpin->setValue(detectionParams.value("face_detect_threshold").toDouble(0.7));
    ui->trackModeSmoothRatioSpin->setValue(detectionParams.value("track_mode_smooth_ratio").toDouble(0.7));
    ui->filterMinimumFacePixelSizeSpin->setValue(detectionParams.value("filter_minimum_face_pixel_size").toInt(60));
    
    // Unblock signals
    ui->faceDetectThresholdSpin->blockSignals(false);
    ui->trackModeSmoothRatioSpin->blockSignals(false);
    ui->filterMinimumFacePixelSizeSpin->blockSignals(false);
}

void MainWindow::onDetectionParameterChanged()
{
    QJsonObject detectionParams;
    detectionParams["face_detect_threshold"] = ui->faceDetectThresholdSpin->value();
    detectionParams["track_mode_smooth_ratio"] = ui->trackModeSmoothRatioSpin->value();
    detectionParams["filter_minimum_face_pixel_size"] = ui->filterMinimumFacePixelSizeSpin->value();
    
    m_settingsManager->setDetectionParameters(detectionParams);
    
    // If model is loaded, reload it to apply new parameters
    if (m_modelManager->isModelLoaded()) {
        m_modelManager->unloadModel();
        m_modelManager->loadModel();
    }
}

void MainWindow::loadFaissSettings()
{
    ui->faissCachePathEdit->setText(m_settingsManager->getFaissCachePath());
}

void MainWindow::loadDatabaseSettings()
{
    // Load PostgreSQL settings
    QJsonObject postgresSettings = m_settingsManager->getPostgresSettings();
    ui->postgresHostEdit->setText(postgresSettings["host"].toString());
    ui->postgresPortSpin->setValue(postgresSettings["port"].toInt());
    ui->postgresDatabaseEdit->setText(postgresSettings["database"].toString());
    ui->postgresUsernameEdit->setText(postgresSettings["username"].toString());
    ui->postgresPasswordEdit->setText(postgresSettings["password"].toString());

    // Load Supabase settings
    QJsonObject supabaseSettings = m_settingsManager->getSupabaseSettings();
    ui->supabaseUrlEdit->setText(supabaseSettings["url"].toString());
    ui->supabaseKeyEdit->setText(supabaseSettings["key"].toString());
}

void MainWindow::onFaissCachePathButtonClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Faiss Cache Directory",
                                                  ui->faissCachePathEdit->text());
    if (!dir.isEmpty()) {
        ui->faissCachePathEdit->setText(dir);
    }
}

void MainWindow::onSaveAllSettingsButtonClicked()
{
    // Save PostgreSQL settings
    QJsonObject postgresSettings;
    postgresSettings["host"] = ui->postgresHostEdit->text();
    postgresSettings["port"] = ui->postgresPortSpin->value();
    postgresSettings["database"] = ui->postgresDatabaseEdit->text();
    postgresSettings["username"] = ui->postgresUsernameEdit->text();
    postgresSettings["password"] = ui->postgresPasswordEdit->text();
    m_settingsManager->setPostgresSettings(postgresSettings);

    // Save Supabase settings
    QJsonObject supabaseSettings;
    supabaseSettings["url"] = ui->supabaseUrlEdit->text();
    supabaseSettings["key"] = ui->supabaseKeyEdit->text();
    m_settingsManager->setSupabaseSettings(supabaseSettings);

    // Save Faiss settings
    QString cachePath = ui->faissCachePathEdit->text();
    m_settingsManager->setFaissCachePath(cachePath);
    
    // Save all settings to file
    m_settingsManager->saveSettings();
    
    // Show success message
    QMessageBox::information(this, "Settings Saved", "All settings have been saved successfully.");
}

void MainWindow::onPostgresTestButtonClicked()
{
    // Get PostgreSQL connection settings
    QString host = ui->postgresHostEdit->text();
    int port = ui->postgresPortSpin->value();
    QString database = ui->postgresDatabaseEdit->text();
    QString username = ui->postgresUsernameEdit->text();
    QString password = ui->postgresPasswordEdit->text();

    // Create PostgreSQL connection string
    QString connStr = QString(
        "host='%1' port='%2' dbname='%3' user='%4' password='%5'")
        .arg(host)
        .arg(port)
        .arg(database)
        .arg(username)
        .arg(password);

    // Test connection using PQconnectdb
    PGconn *conn = PQconnectdb(connStr.toStdString().c_str());

    if (PQstatus(conn) == CONNECTION_OK) {
        QMessageBox::information(this, "Connection Test",
            "Successfully connected to PostgreSQL database!");
        PQfinish(conn);
    } else {
        QString errorMessage = QString::fromStdString(PQerrorMessage(conn));
        QMessageBox::critical(this, "Connection Test",
            "Failed to connect to PostgreSQL database:\n" + errorMessage);
        PQfinish(conn);
    }
}

void MainWindow::onSupabaseTestButtonClicked()
{
    QString url = ui->supabaseUrlEdit->text();
    QString apiKey = ui->supabaseKeyEdit->text();

    if (url.isEmpty() || apiKey.isEmpty()) {
        QMessageBox::warning(this, "Validation Error",
            "Please enter both Project URL and API Key");
        return;
    }

    // Create network manager and request
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(url + "/rest/v1/"));
    
    // Add Supabase headers
    request.setRawHeader("apikey", apiKey.toUtf8());
    request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    
    // Send request
    QNetworkReply *reply = manager->get(request);
    
    // Create event loop to wait for response
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() == QNetworkReply::NoError) {
        QMessageBox::information(this, "Connection Test",
            "Successfully connected to Supabase!");
    } else {
        QMessageBox::critical(this, "Connection Test",
            "Failed to connect to Supabase:\n" + reply->errorString());
    }
    
    reply->deleteLater();
    manager->deleteLater();
} 