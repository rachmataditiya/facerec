#include "facerecognitioncontroller.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <inspireface.h>
#include <cstring> // untuk memset
#include <QFile>
#include <QTextStream>

FaceRecognitionController::FaceRecognitionController(ModelManager* modelManager, 
                                                     SettingsManager* settingsManager,
                                                     FaissManager* faissManager,
                                                     VideoWidget* videoWidget,
                                                     QObject *parent)
    : QObject(parent)
    , m_modelManager(modelManager)
    , m_settingsManager(settingsManager)
    , m_faissManager(faissManager)
    , m_videoWidget(videoWidget)
    , m_timer(new QTimer(this))
    , m_videoCapture(nullptr)
    , m_isInitialized(false)
    , m_isRunning(false)
{
    connect(m_timer, &QTimer::timeout, this, &FaceRecognitionController::processFrame);
}

FaceRecognitionController::~FaceRecognitionController()
{
    shutdown();
}

bool FaceRecognitionController::initialize()
{
    if (m_isInitialized)
        return true;

    m_isInitialized = true;
    return true;
}

void FaceRecognitionController::shutdown()
{
    m_isInitialized = false;
}

QString FaceRecognitionController::recognizeFace(const QImage &image)
{
    if (!m_modelManager->isModelLoaded()) {
        qDebug() << "Model not loaded";
        return QString();
    }

    // Konversi QImage ke cv::Mat
    cv::Mat mat(image.height(), image.width(), CV_8UC3, (void*)image.bits(), image.bytesPerLine());
    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);

    // Siapkan struktur data gambar
    HFImageData imageData;
    imageData.data = mat.data;
    imageData.width = mat.cols;
    imageData.height = mat.rows;
    imageData.format = HF_STREAM_BGR;
    imageData.rotation = HF_CAMERA_ROTATION_0;

    // --- 1. Buat stream khusus untuk deteksi ---
    HFImageStream detectionStream;
    int32_t ret = HFCreateImageStream(&imageData, &detectionStream);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream for detection. Error code:" << ret;
        return QString();
    }

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

    // Buat token untuk wajah pertama yang terdeteksi
    HFFaceBasicToken faceToken;
    faceToken.size = multipleFaceData.tokens[0].size;
    faceToken.data = new unsigned char[faceToken.size];
    memcpy(faceToken.data, multipleFaceData.tokens[0].data, faceToken.size);

    // Lepaskan stream deteksi agar tidak terpakai lagi
    HFReleaseImageStream(detectionStream);

    // Buat stream baru khusus untuk ekstraksi fitur
    HFImageStream extractionStream;
    ret = HFCreateImageStream(&imageData, &extractionStream);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream for extraction. Error code:" << ret;
        delete[] faceToken.data;
        return QString();
    }

    // Inisialisasi objek fitur
    HFFaceFeature feature;
    feature.size = 0;
    feature.data = nullptr;

    // Ekstraksi fitur
    ret = HFFaceFeatureExtract(session, extractionStream, faceToken, &feature);
    HFReleaseImageStream(extractionStream);
    delete[] faceToken.data;
    
    if (ret != HSUCCEED) {
        qDebug() << "Failed to extract features. Error code:" << ret;
        return QString();
    }

    // Save embedding data to file
    QFile file("camera.txt");
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "Feature size: " << feature.size << "\n";
        out << "Embedding data:\n";
        
        int embeddingSize = feature.size / static_cast<int>(sizeof(float));
        float* embeddingData = reinterpret_cast<float*>(feature.data);
        for (int i = 0; i < embeddingSize; i++) {
            out << embeddingData[i] << " ";
        }
        out << "\n";
        file.close();
        qDebug() << "Embedding data saved to camera.txt";
    } else {
        qDebug() << "Failed to open camera.txt for writing";
    }

    // Pencarian di index FAISS
    QVector<QPair<QString, float>> recognitionResults = m_faissManager->recognizeFace(feature);
    if (recognitionResults.isEmpty()) {
        qDebug() << "No match found in database";
        return QString();
    }

    QPair<QString, float> bestMatch = recognitionResults.first();
    if (bestMatch.second < 0.75) {
        qDebug() << "Best match similarity too low:" << bestMatch.second;
        return QString();
    }
    return bestMatch.first;
}

bool FaceRecognitionController::startRecognition(int sourceIndex, const QString &streamUrl)
{
    if (m_isRunning) {
        stopRecognition();
    }

    if (!m_isInitialized || !m_modelManager->isModelLoaded()) {
        qDebug() << "Face recognition not initialized or model not loaded";
        return false;
    }

    m_videoCapture = new cv::VideoCapture();
    
    if (sourceIndex == 0) { // Webcam
        m_videoCapture->open(0);
        m_videoCapture->set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        m_videoCapture->set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        m_videoCapture->set(cv::CAP_PROP_FPS, 30);
    } else { // RTSP
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

    m_isRunning = true;
    m_timer->start(33); // ~30 FPS
    return true;
}

void FaceRecognitionController::stopRecognition()
{
    if (m_isRunning) {
        m_timer->stop();
        if (m_videoCapture) {
            QString url = QString::fromStdString(m_videoCapture->getBackendName());
            m_videoCapture->release();
            delete m_videoCapture;
            m_videoCapture = nullptr;
            emit streamStopped(url);
        }
        m_isRunning = false;
        m_videoWidget->clear();
    }
}

bool FaceRecognitionController::isRunning() const
{
    return m_isRunning;
}

void FaceRecognitionController::processFrame()
{
    if (!m_videoCapture || !m_videoCapture->isOpened()) {
        stopRecognition();
        return;
    }

    cv::Mat frame;
    if (!m_videoCapture->read(frame)) {
        if (m_videoCapture->get(cv::CAP_PROP_BACKEND) == cv::CAP_FFMPEG) {
            // Try to reconnect for RTSP
            QString url = QString::fromStdString(m_videoCapture->getBackendName());
            m_videoCapture->release();
            if (!m_videoCapture->open(url.toStdString())) {
                stopRecognition();
                return;
            }
        } else {
            stopRecognition();
            return;
        }
    }

    if (frame.empty()) return;

    // Convert frame to InspireFace format
    HFImageData imageData;
    imageData.data = frame.data;
    imageData.width = frame.cols;
    imageData.height = frame.rows;
    imageData.format = HF_STREAM_BGR;
    imageData.rotation = HF_CAMERA_ROTATION_0;

    // Create image stream
    HFImageStream streamHandle;
    HResult ret = HFCreateImageStream(&imageData, &streamHandle);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream";
        return;
    }

    // Detect faces
    HFMultipleFaceData results;
    memset(&results, 0, sizeof(HFMultipleFaceData));
    ret = HFExecuteFaceTrack(m_modelManager->getSession(), streamHandle, &results);
    
    if (ret == HSUCCEED) {
        for (int i = 0; i < results.detectedNum; i++) {
            // Skip faces with low confidence
            if (results.detConfidence[i] < 0.7) {
                cv::Rect faceRect(
                    results.rects[i].x,
                    results.rects[i].y,
                    results.rects[i].width,
                    results.rects[i].height
                );
                drawLowQualityFace(frame, faceRect);
                continue;
            }

            // Skip faces that are too small
            if (results.rects[i].width < 60 || results.rects[i].height < 60) {
                continue;
            }

            // Create face token
            HFFaceBasicToken faceToken;
            faceToken.size = results.tokens[i].size;
            faceToken.data = new unsigned char[faceToken.size];
            memcpy(faceToken.data, results.tokens[i].data, faceToken.size);

            // Extract features
            HFFaceFeature feature;
            feature.size = 0;
            feature.data = nullptr;

            ret = HFFaceFeatureExtract(m_modelManager->getSession(), streamHandle, faceToken, &feature);
            delete[] faceToken.data;

            if (ret != HSUCCEED) {
                qDebug() << "Failed to extract features. Error code:" << ret;
                continue;
            }

            // Recognize face
            QVector<QPair<QString, float>> recognitionResults = m_faissManager->recognizeFace(feature);
            
            // Prepare face attributes
            QString gender = "Unknown";
            QString age = "Unknown";
            bool isWearingMask = false;
            bool isLive = true;
            QString memberId = "";
            QString personId = "";
            float distance = 1.0f;

            if (!recognitionResults.isEmpty()) {
                QPair<QString, float> result = recognitionResults.first();
                personId = result.first;
                distance = result.second;

                // Get additional info from FAISS manager
                PersonInfo personInfo = m_faissManager->getPersonInfo(personId);
                memberId = personInfo.memberId;
            }

            // Get face rectangle coordinates
            cv::Rect faceRect(
                results.rects[i].x,
                results.rects[i].y,
                results.rects[i].width,
                results.rects[i].height
            );

            // Draw recognition results
            drawRecognitionResults(frame, 
                                recognitionResults.isEmpty() ? "Unknown" : personId,
                                distance,
                                faceRect,
                                gender,
                                age,
                                isWearingMask,
                                isLive,
                                memberId);
        }
    }

    // Release image stream
    HFReleaseImageStream(streamHandle);

    // Display frame
    m_videoWidget->setFrame(frame);
}

void FaceRecognitionController::drawRecognitionResults(cv::Mat &frame, const QString &name, float distance, 
                                                         const cv::Rect &faceRect, const QString &gender, 
                                                         const QString &age, bool isWearingMask, bool isLive,
                                                         const QString &memberId)
{
    // Gambar rectangle wajah
    cv::rectangle(frame, faceRect, cv::Scalar(0, 255, 0), 2);

    // Siapkan label yang akan ditampilkan
    std::vector<std::string> labelLines;
    labelLines.push_back(name.toStdString());
    if (!memberId.isEmpty())
        labelLines.push_back("ID: " + memberId.toStdString());
    labelLines.push_back(gender.toStdString() + ", " + age.toStdString());
    labelLines.push_back(QString("Score: %1%").arg(int((1.0 - distance) * 100)).toStdString());
    labelLines.push_back(isWearingMask ? "Mask: Yes" : "Mask: No");
    labelLines.push_back(isLive ? "Liveness: Live" : "Liveness: Spoof");

    int font = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.8;
    int thickness = 3;
    cv::Scalar color = (name == "Unknown") ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 255, 255);
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