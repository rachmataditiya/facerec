#ifndef FACERECOGNITIONCONTROLLER_H
#define FACERECOGNITIONCONTROLLER_H

#include <QObject>
#include <QImage>
#include <QVector>
#include <QString>
#include <QTimer>
#include <QDebug>
#include <opencv2/opencv.hpp>
#include <inspireface.h>
#include "../models/modelmanager.h"
#include "../models/settingsmanager.h"
#include "../models/faissmanager.h"
#include "../ui/videowidget.h"

class FaceRecognitionController : public QObject
{
    Q_OBJECT

public:
    explicit FaceRecognitionController(ModelManager* modelManager, 
                                     SettingsManager* settingsManager,
                                     FaissManager* faissManager,
                                     VideoWidget* videoWidget,
                                     QObject *parent = nullptr);
    ~FaceRecognitionController();

    bool initialize();
    void shutdown();

    // Video source handling
    bool startRecognition(int sourceIndex, const QString &streamUrl);
    void stopRecognition();
    bool isRunning() const;

    // Face recognition functions
    QString recognizeFace(const QImage &image);

signals:
    void streamStopped(const QString &url);

private slots:
    void processFrame();

private:
    void drawRecognitionResults(cv::Mat &frame, const QString &name, float distance, 
                              const cv::Rect &faceRect, const QString &gender, 
                              const QString &age, bool isWearingMask, bool isLive,
                              const QString &memberId);
    void drawLowQualityFace(cv::Mat &frame, const cv::Rect &faceRect);

    ModelManager* m_modelManager;
    SettingsManager* m_settingsManager;
    FaissManager* m_faissManager;
    VideoWidget* m_videoWidget;
    QTimer* m_timer;
    cv::VideoCapture* m_videoCapture;
    bool m_isInitialized;
    bool m_isRunning;
};

#endif // FACERECOGNITIONCONTROLLER_H 