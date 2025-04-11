#include "facedetectioncontroller.h"
#include <QDebug>

FaceDetectionController::FaceDetectionController(ModelManager *modelManager,
                                               VideoWidget *videoWidget,
                                               QObject *parent)
    : QObject(parent)
    , m_modelManager(modelManager)
    , m_videoWidget(videoWidget)
    , m_timer(new QTimer(this))
    , m_videoCapture(nullptr)
    , m_isRunning(false)
{
    connect(m_timer, &QTimer::timeout, this, &FaceDetectionController::processFrame);
}

FaceDetectionController::~FaceDetectionController()
{
    stopDetection();
}

bool FaceDetectionController::startDetection(int sourceIndex, const QString &streamUrl)
{
    if (m_isRunning) {
        stopDetection();
    }

    if (!m_modelManager->isModelLoaded()) {
        qDebug() << "Model not loaded";
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

void FaceDetectionController::stopDetection()
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

bool FaceDetectionController::isRunning() const
{
    return m_isRunning;
}

void FaceDetectionController::processFrame()
{
    if (!m_videoCapture || !m_videoCapture->isOpened()) {
        stopDetection();
        return;
    }

    cv::Mat frame;
    if (!m_videoCapture->read(frame)) {
        if (m_videoCapture->get(cv::CAP_PROP_BACKEND) == cv::CAP_FFMPEG) {
            // Try to reconnect for RTSP
            QString url = QString::fromStdString(m_videoCapture->getBackendName());
            m_videoCapture->release();
            if (!m_videoCapture->open(url.toStdString())) {
                stopDetection();
                return;
            }
        } else {
            stopDetection();
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

    HFImageStream streamHandle;
    HResult ret = HFCreateImageStream(&imageData, &streamHandle);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream";
        return;
    }

    // Detect faces
    HFMultipleFaceData results;
    ret = HFExecuteFaceTrack(m_modelManager->getSession(), streamHandle, &results);
    if (ret == HSUCCEED) {
        drawFaceDetection(frame, results);
    }

    // Release image stream
    HFReleaseImageStream(streamHandle);

    // Display frame
    m_videoWidget->setFrame(frame);
}

void FaceDetectionController::drawFaceDetection(cv::Mat &frame, const HFMultipleFaceData &results)
{
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