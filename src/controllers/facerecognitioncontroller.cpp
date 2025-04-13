#include "facerecognitioncontroller.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <inspireface.h>
#include <cstring>
#include <QFile>
#include <QTextStream>

FaceRecognitionController::FaceRecognitionController(ModelManager* modelManager, 
                                                     SettingsManager* settingsManager,
                                                     VideoWidget* videoWidget,
                                                     QObject *parent)
    : QObject(parent)
    , m_modelManager(modelManager)
    , m_settingsManager(settingsManager)
    , m_videoWidget(videoWidget)
    , m_timer(new QTimer(this))
    , m_videoCapture(nullptr)
    , m_isInitialized(false)
    , m_isRunning(false)
{
    connect(m_timer, &QTimer::timeout, this, &FaceRecognitionController::processFrame);
}

// Destructor
FaceRecognitionController::~FaceRecognitionController()
{
    shutdown();
}

// Inisialisasi face recognition controller
bool FaceRecognitionController::initialize()
{
    if (m_isInitialized) {
        qDebug() << "Face recognition already initialized";
        return true;
    }

    qDebug() << "Initializing face recognition controller...";
    m_isInitialized = true;
    qDebug() << "Face recognition controller initialized successfully";
    return true;
}

// Shutdown controller
void FaceRecognitionController::shutdown()
{
    qDebug() << "Shutting down face recognition controller...";
    m_isInitialized = false;
    qDebug() << "Face recognition controller shutdown complete";
}

// Fungsi untuk melakukan face recognition dari QImage
QString FaceRecognitionController::recognizeFace(const QImage &image)
{
    if (!m_modelManager->isModelLoaded()) {
        qDebug() << "Model not loaded, cannot recognize face";
        return QString();
    }

    qDebug() << "Starting face recognition process...";
    // Konversi QImage ke cv::Mat
    cv::Mat mat(image.height(), image.width(), CV_8UC3, (void*)image.bits(), image.bytesPerLine());
    cv::cvtColor(mat, mat, cv::COLOR_BGR2RGB);

    // Siapkan struktur data gambar
    HFImageData imageData;
    imageData.data = mat.data;
    imageData.width = mat.cols;
    imageData.height = mat.rows;
    imageData.format = HF_STREAM_BGR;
    imageData.rotation = HF_CAMERA_ROTATION_0;

    // --- 1. Buat stream untuk deteksi ---
    HFImageStream detectionStream;
    int32_t ret = HFCreateImageStream(&imageData, &detectionStream);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream for detection. Error code:" << ret;
        return QString();
    }
    qDebug() << "Detection stream created successfully";

    // Dapatkan session dari model manager
    HFSession session = m_modelManager->getSession();

    // Deteksi wajah
    HFMultipleFaceData multipleFaceData;
    memset(&multipleFaceData, 0, sizeof(HFMultipleFaceData));
    ret = HFExecuteFaceTrack(session, detectionStream, &multipleFaceData);
    if (ret != HSUCCEED || multipleFaceData.detectedNum == 0) {
        qDebug() << "No face detected. Error code:" << ret;
        HFReleaseImageStream(detectionStream);
        return QString();
    }
    qDebug() << "Faces detected:" << multipleFaceData.detectedNum;

    // Buat token dari wajah pertama yang terdeteksi
    HFFaceBasicToken faceToken;
    faceToken.size = multipleFaceData.tokens[0].size;
    faceToken.data = new unsigned char[faceToken.size];
    memcpy(faceToken.data, multipleFaceData.tokens[0].data, faceToken.size);
    qDebug() << "Face token created, size:" << faceToken.size;

    // Lepaskan stream deteksi
    HFReleaseImageStream(detectionStream);

    // --- 2. Buat stream untuk ekstraksi fitur ---
    HFImageStream extractionStream;
    ret = HFCreateImageStream(&imageData, &extractionStream);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream for extraction. Error code:" << ret;
        delete[] static_cast<unsigned char*>(faceToken.data);
        return QString();
    }
    qDebug() << "Extraction stream created successfully";

    // Inisialisasi struktur fitur wajah
    HFFaceFeature feature;
    feature.size = 0;
    feature.data = nullptr;

    // Ekstraksi fitur wajah
    ret = HFFaceFeatureExtract(session, extractionStream, faceToken, &feature);
    delete[] static_cast<unsigned char*>(faceToken.data);
    HFReleaseImageStream(extractionStream);
    
    if (ret != HSUCCEED) {
        qDebug() << "Failed to extract features. Error code:" << ret;
        return QString();
    }
    qDebug() << "Features extracted successfully, size:" << feature.size;

    // --- Konversi HFFaceFeature ke QVector<float> ---
    QVector<float> featureVec;
    if (feature.size > 0 && feature.data) {
        featureVec.resize(feature.size);
        memcpy(featureVec.data(), feature.data, feature.size * sizeof(float));
    } else {
        qDebug() << "Feature extraction menghasilkan data kosong.";
        return QString();
    }

    // Pencarian di index FAISS menggunakan fitur yang telah dikonversi.
    // Fungsi recognizeFace() pada FaissManager mengembalikan QPair<QString, float>.
    QPair<QString, float> recognitionResult = m_faissManager->recognizeFace(featureVec);
    if (recognitionResult.first.isEmpty()) {
        qDebug() << "No match found in database";
        return QString();
    }
    if (recognitionResult.second < 0.75) {
        qDebug() << "Best match similarity too low:" << recognitionResult.second;
        return QString();
    }
    qDebug() << "Best match found:" << recognitionResult.first << "with similarity:" << recognitionResult.second;
    return recognitionResult.first;
}

// Memulai proses face recognition (webcam atau RTSP)
bool FaceRecognitionController::startRecognition(int sourceIndex, const QString &streamUrl)
{
    if (m_isRunning) {
        qDebug() << "Recognition already running, stopping first...";
        stopRecognition();
    }

    if (!m_isInitialized || !m_modelManager->isModelLoaded()) {
        qDebug() << "Face recognition not initialized or model not loaded";
        return false;
    }

    qDebug() << "Starting face recognition...";
    m_videoCapture = new cv::VideoCapture();
    
    if (sourceIndex == 0) { // Webcam
        qDebug() << "Opening webcam...";
        m_videoCapture->open(0);
        m_videoCapture->set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        m_videoCapture->set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        m_videoCapture->set(cv::CAP_PROP_FPS, 30);
    } else { // RTSP
        qDebug() << "Opening RTSP stream:" << streamUrl;
        QString rtspUrl = streamUrl;
        if (!rtspUrl.contains("transport=")) {
            rtspUrl += (rtspUrl.contains("?") ? "&" : "?") + QString("transport=tcp");
        }
        if (!m_videoCapture->open(rtspUrl.toStdString())) {
            qDebug() << "Failed to open RTSP stream:" << rtspUrl;
            delete m_videoCapture;
            m_videoCapture = nullptr;
            return false;
        }
        m_videoCapture->set(cv::CAP_PROP_BUFFERSIZE, 1);
        m_videoCapture->set(cv::CAP_PROP_FPS, 30);
    }

    if (!m_videoCapture->isOpened()) {
        qDebug() << "Failed to open video source";
        delete m_videoCapture;
        m_videoCapture = nullptr;
        return false;
    }

    qDebug() << "Video source opened successfully";
    m_isRunning = true;
    m_timer->start(33); // ~30 FPS
    qDebug() << "Face recognition started successfully";
    return true;
}

// Menghentikan proses recognition
void FaceRecognitionController::stopRecognition()
{
    if (m_isRunning) {
        qDebug() << "Stopping face recognition...";
        m_timer->stop();
        if (m_videoCapture) {
            QString url = QString::fromStdString(m_videoCapture->getBackendName());
            m_videoCapture->release();
            delete m_videoCapture;
            m_videoCapture = nullptr;
            emit streamStopped(url);
            qDebug() << "Video capture stopped:" << url;
        }
        m_isRunning = false;
        m_videoWidget->clear();
        qDebug() << "Face recognition stopped successfully";
    }
}

// Proses frame video
void FaceRecognitionController::processFrame()
{
    if (!m_videoCapture || !m_videoCapture->isOpened()) {
        qDebug() << "Video capture not available, stopping recognition";
        stopRecognition();
        return;
    }

    cv::Mat frame;
    if (!m_videoCapture->read(frame)) {
        if (m_videoCapture->get(cv::CAP_PROP_BACKEND) == cv::CAP_FFMPEG) {
            // Try reconnect untuk RTSP
            QString url = QString::fromStdString(m_videoCapture->getBackendName());
            qDebug() << "RTSP stream disconnected, attempting to reconnect:" << url;
            m_videoCapture->release();
            if (!m_videoCapture->open(url.toStdString())) {
                qDebug() << "Failed to reconnect to RTSP stream";
                stopRecognition();
                return;
            }
            qDebug() << "Successfully reconnected to RTSP stream";
        } else {
            qDebug() << "Failed to read frame from video source";
            stopRecognition();
            return;
        }
    }

    if (frame.empty()) {
        qDebug() << "Received empty frame";
        return;
    }

    // Konversi frame ke format InspireFace
    HFImageData imageData;
    imageData.data = frame.data;
    imageData.width = frame.cols;
    imageData.height = frame.rows;
    imageData.format = HF_STREAM_BGR;
    imageData.rotation = HF_CAMERA_ROTATION_0;

    // Buat image stream
    HFImageStream streamHandle;
    HResult ret = HFCreateImageStream(&imageData, &streamHandle);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream";
        return;
    }

    // Deteksi wajah
    HFMultipleFaceData results;
    memset(&results, 0, sizeof(HFMultipleFaceData));
    ret = HFExecuteFaceTrack(m_modelManager->getSession(), streamHandle, &results);
    
    if (ret == HSUCCEED) {
        qDebug() << "Faces detected:" << results.detectedNum;
        for (int i = 0; i < results.detectedNum; i++) {
            // Lewati wajah dengan confidence rendah
            if (results.detConfidence[i] < 0.7) {
                qDebug() << "Skipping face with low confidence:" << results.detConfidence[i];
                cv::Rect faceRect(
                    results.rects[i].x,
                    results.rects[i].y,
                    results.rects[i].width,
                    results.rects[i].height
                );
                drawLowQualityFace(frame, faceRect);
                continue;
            }

            // Lewati wajah yang terlalu kecil
            if (results.rects[i].width < 60 || results.rects[i].height < 60) {
                qDebug() << "Skipping face that is too small:" 
                         << results.rects[i].width << "x" << results.rects[i].height;
                continue;
            }

            // Buat token wajah
            HFFaceBasicToken faceToken;
            faceToken.size = results.tokens[i].size;
            faceToken.data = new unsigned char[faceToken.size];
            memcpy(faceToken.data, results.tokens[i].data, faceToken.size);

            // Ekstraksi fitur untuk wajah tersebut
            HFFaceFeature feature;
            feature.size = 0;
            feature.data = nullptr;

            ret = HFFaceFeatureExtract(m_modelManager->getSession(), streamHandle, faceToken, &feature);
            delete[] static_cast<unsigned char*>(faceToken.data);

            if (ret != HSUCCEED) {
                qDebug() << "Failed to extract features. Error code:" << ret;
                continue;
            }

            // Konversi HFFaceFeature ke QVector<float>
            QVector<float> featureVec;
            if (feature.size > 0 && feature.data) {
                featureVec.resize(feature.size);
                memcpy(featureVec.data(), feature.data, feature.size * sizeof(float));
            } else {
                qDebug() << "Feature extraction returned empty data";
                continue;
            }

            // Pencarian wajah menggunakan FAISS manager
            QPair<QString, float> recognitionResult = m_faissManager->recognizeFace(featureVec);
            
            // Siapkan atribut wajah
            QString memberId = "";
            QString personId = "";
            float distance = 1.0f;

            if (!recognitionResult.first.isEmpty()) {
                personId = recognitionResult.first;
                distance = recognitionResult.second;
                qDebug() << "Face recognized:" << personId << "with distance:" << distance;

                // Dapatkan info tambahan dari FAISS manager
                PersonInfo personInfo = m_faissManager->getPersonInfo(personId);
                memberId = personInfo.memberId;
            } else {
                qDebug() << "No match found for face";
            }

            // Dapatkan koordinat rectangle wajah
            cv::Rect faceRect(
                results.rects[i].x,
                results.rects[i].y,
                results.rects[i].width,
                results.rects[i].height
            );

            // Gambar hasil face recognition
            drawRecognitionResults(frame, 
                                   recognitionResult.first.isEmpty() ? "Unknown" : personId,
                                   distance,
                                   faceRect,
                                   memberId);
        }
    } else {
        qDebug() << "Face detection failed. Error code:" << ret;
    }

    // Lepaskan image stream
    HFReleaseImageStream(streamHandle);

    // Tampilkan frame ke video widget
    m_videoWidget->setFrame(frame);
}

// Menggambar hasil recognition di atas frame
void FaceRecognitionController::drawRecognitionResults(cv::Mat &frame, const QString &personId, float distance, 
                                                         const cv::Rect &faceRect, const QString &memberId)
{
    // Gambar rectangle wajah
    cv::rectangle(frame, faceRect, cv::Scalar(0, 255, 0), 2);

    // Ambil info person dari FAISS manager
    PersonInfo personInfo = m_faissManager->getPersonInfo(personId);
    QString name = personInfo.name;
    QString displayMemberId = personInfo.memberId.isEmpty() ? memberId : personInfo.memberId;

    // Buat label yang akan ditampilkan
    std::vector<std::string> labelLines;
    labelLines.push_back(name.isEmpty() ? "Unknown" : name.toStdString());
    if (!displayMemberId.isEmpty())
        labelLines.push_back("ID: " + displayMemberId.toStdString());
    labelLines.push_back(QString("Score: %1%").arg(int((1.0 - distance) * 100)).toStdString());

    int font = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.8;
    int thickness = 3;
    cv::Scalar color = (name.isEmpty() || name == "Unknown") ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 255, 255);
    int verticalOffset = 90;
    int lineHeight = 30;

    // Gambar setiap baris teks di atas rectangle wajah
    for (size_t i = 0; i < labelLines.size(); i++) {
        cv::Size textSize = cv::getTextSize(labelLines[i], font, fontScale, thickness, nullptr);
        int textX = faceRect.x + ((faceRect.width - textSize.width) / 2);
        int textY = faceRect.y - verticalOffset - (lineHeight * (labelLines.size() - i - 1));
        cv::putText(frame, labelLines[i], cv::Point(textX, textY),
                    font, fontScale, color, thickness);
    }
}

// Menggambar tanda untuk wajah berkualitas rendah
void FaceRecognitionController::drawLowQualityFace(cv::Mat &frame, const cv::Rect &faceRect)
{
    // Gambar rectangle dengan warna merah untuk wajah berkualitas rendah
    cv::rectangle(frame, faceRect, cv::Scalar(0, 0, 255), 2);

    std::string label = "Low Quality";
    int font = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.8;
    int thickness = 3;
    cv::Scalar color = cv::Scalar(0, 0, 255);
    int verticalOffset = 90;

    cv::Size textSize = cv::getTextSize(label, font, fontScale, thickness, nullptr);
    int textX = faceRect.x + ((faceRect.width - textSize.width) / 2);
    int textY = faceRect.y - verticalOffset;
    cv::putText(frame, label, cv::Point(textX, textY),
                font, fontScale, color, thickness);
}
