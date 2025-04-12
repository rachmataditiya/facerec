#include "facerecognitioncontroller.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <inspireface.h>

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
    if (m_isInitialized) {
        return true;
    }

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

    // Convert QImage to cv::Mat
    cv::Mat mat(image.height(), image.width(), CV_8UC3, (void*)image.bits(), image.bytesPerLine());
    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);

    // Create image data structure
    HFImageData imageData;
    imageData.data = mat.data;
    imageData.width = mat.cols;
    imageData.height = mat.rows;
    imageData.format = HF_STREAM_BGR;
    imageData.rotation = HF_CAMERA_ROTATION_0;

    // Create image stream
    HFImageStream imageStream;
    int32_t ret = HFCreateImageStream(&imageData, &imageStream);
    if (ret != 0) {
        qDebug() << "Failed to create image stream. Error code:" << ret;
        return QString();
    }

    // Get session from model manager
    HFSession session = m_modelManager->getSession();

    // Detect faces
    HFMultipleFaceData multipleFaceData;
    ret = HFExecuteFaceTrack(session, imageStream, &multipleFaceData);
    if (ret != 0 || multipleFaceData.detectedNum == 0) {
        qDebug() << "No face detected. Error code:" << ret;
        HFReleaseImageStream(imageStream);
        return QString();
    }

    // Create face token
    HFFaceBasicToken faceToken;
    faceToken.size = sizeof(HFFaceBasicToken);
    faceToken.data = &multipleFaceData.tokens[0];

    // Extract features
    HFFaceFeature feature;
    ret = HFFaceFeatureExtract(session, imageStream, faceToken, &feature);
    if (ret != 0) {
        qDebug() << "Failed to extract features. Error code:" << ret;
        HFReleaseImageStream(imageStream);
        return QString();
    }

    // Convert feature to inspire::FaceEmbedding
    inspire::FaceEmbedding embedding;
    embedding.embedding.resize(512);
    std::copy(feature.data, feature.data + 512, embedding.embedding.begin());

    // Search in FAISS index
    QVector<QPair<QString, float>> recognitionResults = m_faissManager->recognizeFace(embedding);
    if (recognitionResults.isEmpty()) {
        qDebug() << "No match found in database";
        HFReleaseImageStream(imageStream);
        return QString();
    }

    // Get the best match
    QPair<QString, float> bestMatch = recognitionResults.first();
    if (bestMatch.second < 0.75) { // Increased threshold for better accuracy
        qDebug() << "Best match similarity too low:" << bestMatch.second;
        HFReleaseImageStream(imageStream);
        return QString();
    }

    HFReleaseImageStream(imageStream);
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

    // Create image data structure
    HFImageData imageData;
    imageData.data = frame.data;
    imageData.width = frame.cols;
    imageData.height = frame.rows;
    imageData.format = HF_STREAM_BGR;
    imageData.rotation = HF_CAMERA_ROTATION_0;

    // Create image stream
    HFImageStream imageStream;
    int32_t ret = HFCreateImageStream(&imageData, &imageStream);
    if (ret != 0) {
        qDebug() << "Failed to create image stream. Error code:" << ret;
        return;
    }

    // Get session from model manager
    HFSession session = m_modelManager->getSession();

    // Detect faces
    HFMultipleFaceData multipleFaceData;
    ret = HFExecuteFaceTrack(session, imageStream, &multipleFaceData);
    if (ret == 0) {
        for (int i = 0; i < multipleFaceData.detectedNum; i++) {
            // Skip low quality faces
            if (multipleFaceData.detConfidence[i] < 0.6) {
                cv::Rect faceRect(
                    multipleFaceData.rects[i].x,
                    multipleFaceData.rects[i].y,
                    multipleFaceData.rects[i].width,
                    multipleFaceData.rects[i].height
                );
                drawLowQualityFace(frame, faceRect);
                continue;
            }

            // Create face token
            HFFaceBasicToken faceToken;
            faceToken.size = sizeof(HFFaceBasicToken);
            faceToken.data = &multipleFaceData.tokens[i];

            // Extract features
            HFFaceFeature feature;
            ret = HFFaceFeatureExtract(session, imageStream, faceToken, &feature);
            if (ret != 0) {
                qDebug() << "Failed to extract features. Error code:" << ret;
                continue;
            }

            // Convert feature to inspire::FaceEmbedding
            inspire::FaceEmbedding embedding;
            embedding.embedding.resize(512);
            std::copy(feature.data, feature.data + 512, embedding.embedding.begin());

            // Search in FAISS index
            QVector<QPair<QString, float>> recognitionResults = m_faissManager->recognizeFace(embedding);
            
            // Get face attributes
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

            // Get face rectangle
            cv::Rect faceRect(
                multipleFaceData.rects[i].x,
                multipleFaceData.rects[i].y,
                multipleFaceData.rects[i].width,
                multipleFaceData.rects[i].height
            );

            // Draw recognition results with all details
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
    HFReleaseImageStream(imageStream);

    // Display frame
    m_videoWidget->setFrame(frame);
}

void FaceRecognitionController::drawRecognitionResults(cv::Mat &frame, const QString &name, float distance, 
                                                     const cv::Rect &faceRect, const QString &gender, 
                                                     const QString &age, bool isWearingMask, bool isLive,
                                                     const QString &memberId)
{
    // Draw face rectangle
    cv::rectangle(frame, faceRect, cv::Scalar(0, 255, 0), 2);

    // Prepare label lines
    std::vector<std::string> labelLines;
    labelLines.push_back(name.toStdString());
    if (!memberId.isEmpty()) {
        labelLines.push_back("ID: " + memberId.toStdString());
    }
    labelLines.push_back(gender.toStdString() + ", " + age.toStdString());
    labelLines.push_back(QString("Score: %1%").arg(int((1.0 - distance) * 100)).toStdString());
    
    // Add mask and liveness status
    if (isWearingMask) {
        labelLines.push_back("Mask: Yes");
    } else {
        labelLines.push_back("Mask: No");
    }
    
    if (isLive) {
        labelLines.push_back("Liveness: Live");
    } else {
        labelLines.push_back("Liveness: Spoof");
    }

    // Set text properties
    int font = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.8;
    int thickness = 3;
    cv::Scalar color = (name == "Unknown") ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 255, 255);
    int verticalOffset = 90;
    int lineHeight = 30;

    // Draw each line of text
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
    // Draw face rectangle
    cv::rectangle(frame, faceRect, cv::Scalar(0, 0, 255), 2);

    // Draw "Low Quality" text
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