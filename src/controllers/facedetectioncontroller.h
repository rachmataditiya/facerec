#ifndef FACEDETECTIONCONTROLLER_H
#define FACEDETECTIONCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <opencv2/opencv.hpp>
#include <inspireface.h>
#include "models/modelmanager.h"
#include "ui/videowidget.h"

class FaceDetectionController : public QObject
{
    Q_OBJECT

public:
    explicit FaceDetectionController(ModelManager *modelManager, 
                                   VideoWidget *videoWidget,
                                   QObject *parent = nullptr);
    ~FaceDetectionController();

    bool startDetection(int sourceIndex, const QString &streamUrl = QString());
    void stopDetection();
    bool isRunning() const;

signals:
    void streamStopped(const QString &url);

private slots:
    void processFrame();

private:
    ModelManager *m_modelManager;
    VideoWidget *m_videoWidget;
    QTimer *m_timer;
    cv::VideoCapture *m_videoCapture;
    bool m_isRunning;

    void drawFaceDetection(cv::Mat &frame, const HFMultipleFaceData &results);
};

#endif // FACEDETECTIONCONTROLLER_H 