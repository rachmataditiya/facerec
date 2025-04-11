#include "mainwindow.h"
#include <QMessageBox>
#include <QTimer>
#include <QImage>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTableWidgetItem>
#include <QFileDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , videoCapture(nullptr)
    , timer(new QTimer(this))
    , isRunning(false)
    , settings(new QSettings("FaceRec", "Settings", this))
{
    setupUI();
    connect(timer, &QTimer::timeout, this, &MainWindow::updateFrame);
    loadStreams();
    loadSettings();
}

MainWindow::~MainWindow()
{
    stopFaceDetection();
    if (videoCapture) {
        videoCapture->release();
        delete videoCapture;
    }
    HFReleaseInspireFaceSession(session);
    HFTerminateInspireFace();
    saveSettings();
}

void MainWindow::setupUI()
{
    // Set window properties
    setWindowTitle("Face Detection");
    resize(1280, 720);

    // Create central widget and layout
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Create tab widget
    tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget);

    // Create video tab
    QWidget *videoTab = new QWidget(this);
    QVBoxLayout *videoTabLayout = new QVBoxLayout(videoTab);

    // Model Group
    modelGroup = new QGroupBox("Model Settings", this);
    QHBoxLayout *modelLayout = new QHBoxLayout(modelGroup);

    modelPathEdit = new QLineEdit(this);
    modelPathEdit->setPlaceholderText("Path to model directory");
    modelPathButton = new QPushButton("Browse...", this);
    connect(modelPathButton, &QPushButton::clicked, this, &MainWindow::onModelPathButtonClicked);
    connect(modelPathEdit, &QLineEdit::textChanged, this, &MainWindow::onModelPathChanged);

    modelLayout->addWidget(new QLabel("Model Path:", this));
    modelLayout->addWidget(modelPathEdit);
    modelLayout->addWidget(modelPathButton);

    // Control Group
    controlGroup = new QGroupBox("Control", this);
    QHBoxLayout *controlLayout = new QHBoxLayout(controlGroup);

    // Source selection
    sourceComboBox = new QComboBox(this);
    sourceComboBox->addItem("Webcam");
    sourceComboBox->addItem("RTSP Stream");
    connect(sourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSourceChanged);

    // Stream selection
    streamComboBox = new QComboBox(this);
    connect(streamComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onStreamSelected);

    // RTSP URL input
    rtspUrlEdit = new QLineEdit(this);
    rtspUrlEdit->setPlaceholderText("rtsp://username:password@ip:port/stream");
    rtspUrlEdit->setEnabled(false);

    // Buttons
    startButton = new QPushButton("Start", this);
    stopButton = new QPushButton("Stop", this);
    stopButton->setEnabled(false);
    connect(startButton, &QPushButton::clicked, this, &MainWindow::onStartButtonClicked);
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::onStopButtonClicked);

    // Add widgets to control layout
    controlLayout->addWidget(new QLabel("Source:", this));
    controlLayout->addWidget(sourceComboBox);
    controlLayout->addWidget(streamComboBox);
    controlLayout->addWidget(rtspUrlEdit);
    controlLayout->addWidget(startButton);
    controlLayout->addWidget(stopButton);

    // Video Group
    videoGroup = new QGroupBox("Video", this);
    QVBoxLayout *videoLayout = new QVBoxLayout(videoGroup);
    videoLabel = new QLabel(this);
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setMinimumSize(640, 480);
    videoLayout->addWidget(videoLabel);

    // Add groups to video tab layout
    videoTabLayout->addWidget(modelGroup);
    videoTabLayout->addWidget(controlGroup);
    videoTabLayout->addWidget(videoGroup);

    // Create stream management tab
    QWidget *streamTab = new QWidget(this);
    QVBoxLayout *streamTabLayout = new QVBoxLayout(streamTab);

    // Stream management group
    streamGroup = new QGroupBox("Stream Management", this);
    QVBoxLayout *streamLayout = new QVBoxLayout(streamGroup);

    // Stream table
    streamTable = new QTableWidget(this);
    streamTable->setColumnCount(2);
    streamTable->setHorizontalHeaderLabels({"Name", "URL"});
    streamTable->horizontalHeader()->setStretchLastSection(true);

    // Stream input fields
    QHBoxLayout *streamInputLayout = new QHBoxLayout();
    streamNameEdit = new QLineEdit(this);
    streamNameEdit->setPlaceholderText("Stream Name");
    QLineEdit *streamUrlEdit = new QLineEdit(this);
    streamUrlEdit->setPlaceholderText("RTSP URL");
    addStreamButton = new QPushButton("Add Stream", this);
    removeStreamButton = new QPushButton("Remove Selected", this);
    connect(addStreamButton, &QPushButton::clicked, this, &MainWindow::onAddStreamClicked);
    connect(removeStreamButton, &QPushButton::clicked, this, &MainWindow::onRemoveStreamClicked);

    streamInputLayout->addWidget(streamNameEdit);
    streamInputLayout->addWidget(streamUrlEdit);
    streamInputLayout->addWidget(addStreamButton);
    streamInputLayout->addWidget(removeStreamButton);

    // Add widgets to stream layout
    streamLayout->addWidget(streamTable);
    streamLayout->addLayout(streamInputLayout);

    // Add groups to stream tab layout
    streamTabLayout->addWidget(streamGroup);

    // Add tabs to tab widget
    tabWidget->addTab(videoTab, "Video");
    tabWidget->addTab(streamTab, "Stream Management");
}

void MainWindow::loadStreams()
{
    QFile file("streams.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Tidak dapat membuka file streams.json";
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        streams = obj["streams"].toArray();
        updateStreamComboBox();
        updateStreamTable();
    }
}

void MainWindow::saveStreams()
{
    QFile file("streams.json");
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Tidak dapat membuka file streams.json untuk ditulis";
        return;
    }

    QJsonObject obj;
    obj["streams"] = streams;
    QJsonDocument doc(obj);
    file.write(doc.toJson());
    file.close();
}

void MainWindow::updateStreamComboBox()
{
    streamComboBox->clear();
    for (const QJsonValue &value : streams) {
        QJsonObject obj = value.toObject();
        streamComboBox->addItem(obj["name"].toString());
    }
}

void MainWindow::updateStreamTable()
{
    streamTable->setRowCount(streams.size());
    for (int i = 0; i < streams.size(); ++i) {
        QJsonObject obj = streams[i].toObject();
        streamTable->setItem(i, 0, new QTableWidgetItem(obj["name"].toString()));
        streamTable->setItem(i, 1, new QTableWidgetItem(obj["url"].toString()));
    }
}

void MainWindow::onAddStreamClicked()
{
    QString name = streamNameEdit->text();
    QString url = rtspUrlEdit->text();

    if (name.isEmpty() || url.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Nama dan URL stream harus diisi");
        return;
    }

    QJsonObject newStream;
    newStream["name"] = name;
    newStream["url"] = url;
    streams.append(newStream);

    saveStreams();
    updateStreamComboBox();
    updateStreamTable();

    streamNameEdit->clear();
    rtspUrlEdit->clear();
}

void MainWindow::onRemoveStreamClicked()
{
    int row = streamTable->currentRow();
    if (row >= 0 && row < streams.size()) {
        streams.removeAt(row);
        saveStreams();
        updateStreamComboBox();
        updateStreamTable();
    }
}

void MainWindow::onStreamSelected(int index)
{
    if (index >= 0 && index < streams.size()) {
        QJsonObject obj = streams[index].toObject();
        rtspUrlEdit->setText(obj["url"].toString());
    }
}

void MainWindow::onSourceChanged(int index)
{
    rtspUrlEdit->setEnabled(index == 1); // Enable RTSP URL input only when RTSP is selected
}

void MainWindow::onStartButtonClicked()
{
    if (isRunning) return;

    // Initialize video capture based on selected source
    if (sourceComboBox->currentIndex() == 0) {
        // Webcam
        videoCapture = new cv::VideoCapture(0);
        videoCapture->set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        videoCapture->set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        videoCapture->set(cv::CAP_PROP_FPS, 30);
    } else {
        // RTSP
        QString url = rtspUrlEdit->text();
        if (url.isEmpty()) {
            QMessageBox::warning(this, "Warning", "Masukkan URL RTSP terlebih dahulu");
            return;
        }
        videoCapture = new cv::VideoCapture(url.toStdString());
    }

    if (!videoCapture->isOpened()) {
        QMessageBox::critical(this, "Error", "Tidak dapat membuka sumber video");
        delete videoCapture;
        videoCapture = nullptr;
        return;
    }

    isRunning = true;
    startButton->setEnabled(false);
    stopButton->setEnabled(true);
    sourceComboBox->setEnabled(false);
    rtspUrlEdit->setEnabled(false);
    timer->start(33); // ~30 FPS
}

void MainWindow::onStopButtonClicked()
{
    stopFaceDetection();
}

void MainWindow::stopFaceDetection()
{
    if (!isRunning) return;

    timer->stop();
    if (videoCapture) {
        videoCapture->release();
        delete videoCapture;
        videoCapture = nullptr;
    }

    isRunning = false;
    startButton->setEnabled(true);
    stopButton->setEnabled(false);
    sourceComboBox->setEnabled(true);
    rtspUrlEdit->setEnabled(sourceComboBox->currentIndex() == 1);
    videoLabel->clear();
}

void MainWindow::updateFrame()
{
    if (!videoCapture || !videoCapture->isOpened()) return;

    cv::Mat frame;
    *videoCapture >> frame;
    if (frame.empty()) return;

    // Convert frame to InspireFace format
    HFImageData imageData;
    imageData.data = frame.data;
    imageData.width = frame.cols;
    imageData.height = frame.rows;
    imageData.format = HF_STREAM_BGR;
    imageData.rotation = HF_CAMERA_ROTATION_0;

    HFImageStream streamHandle;
    HResult ret = HFCreateImageStream(&imageData, &streamHandle);
    if (ret != HSUCCEED) {
        qDebug() << "Error: Gagal membuat image stream";
        return;
    }

    // Detect faces
    HFMultipleFaceData results;
    ret = HFExecuteFaceTrack(session, streamHandle, &results);
    if (ret == HSUCCEED) {
        for (int i = 0; i < results.detectedNum; i++) {
            // Draw rectangle around face
            cv::Rect faceRect(
                results.rects[i].x,
                results.rects[i].y,
                results.rects[i].width,
                results.rects[i].height
            );
            cv::rectangle(frame, faceRect, cv::Scalar(0, 255, 0), 2);

            // Display confidence
            std::string confidence = "Conf: " + std::to_string(results.detConfidence[i]);
            cv::putText(frame, confidence, cv::Point(faceRect.x, faceRect.y - 30),
                      cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 2);

            // Display tracking ID
            std::string text = "ID: " + std::to_string(results.trackIds[i]);
            cv::putText(frame, text, cv::Point(faceRect.x, faceRect.y - 10),
                      cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);

            // Display face angles if available
            if (results.angles.yaw && results.angles.pitch && results.angles.roll) {
                std::string angles = "Yaw: " + std::to_string(int(*results.angles.yaw)) +
                                  " Pitch: " + std::to_string(int(*results.angles.pitch)) +
                                  " Roll: " + std::to_string(int(*results.angles.roll));
                cv::putText(frame, angles, cv::Point(faceRect.x, faceRect.y + faceRect.height + 20),
                          cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 2);
            }
        }
    }

    // Release image stream
    HFReleaseImageStream(streamHandle);

    // Convert frame to QImage and display
    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
    QImage qImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    videoLabel->setPixmap(QPixmap::fromImage(qImage).scaled(
        videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::loadSettings()
{
    QString modelPath = settings->value("modelPath", "/Users/rachmataditiya/.inspireface/models/Megatron").toString();
    modelPathEdit->setText(modelPath);
    initializeInspireFace();
}

void MainWindow::saveSettings()
{
    settings->setValue("modelPath", modelPathEdit->text());
}

void MainWindow::initializeInspireFace()
{
    QString modelPath = modelPathEdit->text();
    HResult ret = HFLaunchInspireFace(modelPath.toStdString().c_str());
    if (ret != HSUCCEED) {
        QMessageBox::critical(this, "Error", "Gagal menginisialisasi InspireFace");
        return;
    }

    // Configure pipeline
    param.enable_recognition = 1;
    param.enable_liveness = 1;
    param.enable_mask_detect = 1;
    param.enable_face_attribute = 1;
    param.enable_face_quality = 1;
    param.enable_ir_liveness = 0;
    param.enable_interaction_liveness = 0;
    param.enable_detect_mode_landmark = 1;

    // Create session
    ret = HFCreateInspireFaceSession(param, HF_DETECT_MODE_LIGHT_TRACK, 1, 320, 0, &session);
    if (ret != HSUCCEED) {
        QMessageBox::critical(this, "Error", "Gagal membuat session");
        return;
    }

    // Set detection parameters
    HFSessionSetFaceDetectThreshold(session, 0.7f);
    HFSessionSetTrackModeSmoothRatio(session, 0.7f);
    HFSessionSetFilterMinimumFacePixelSize(session, 60);
}

void MainWindow::onModelPathButtonClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Model Directory",
                                                  modelPathEdit->text(),
                                                  QFileDialog::ShowDirsOnly);
    if (!dir.isEmpty()) {
        modelPathEdit->setText(dir);
    }
}

void MainWindow::onModelPathChanged(const QString &path)
{
    if (!path.isEmpty()) {
        // Release current session
        if (session) {
            HFReleaseInspireFaceSession(session);
        }
        HFTerminateInspireFace();
        
        // Initialize with new path
        initializeInspireFace();
    }
} 