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
#include <libpq-fe.h>
#include "../models/modelmanager.h"
#include "../models/settingsmanager.h"
#include "../ui/videowidget.h"

// Structure to hold person information from PostgreSQL
struct PersonInfo {
    QString id;
    QString name;
    QString memberId;
    float distance;
};

class FaceRecognitionController : public QObject
{
    Q_OBJECT

public:
    explicit FaceRecognitionController(ModelManager* modelManager, 
                                     SettingsManager* settingsManager,
                                     VideoWidget* videoWidget,
                                     QObject *parent = nullptr);
    ~FaceRecognitionController();

    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_isInitialized; }

    QString recognizeFace(const QImage &image);
    bool startRecognition(int sourceIndex, const QString &streamUrl);
    void stopRecognition();
    bool isRunning() const { return m_isRunning; }

signals:
    void streamStopped(const QString &url);

private slots:
    void processFrame();

private:
    void drawRecognitionResults(cv::Mat &frame, const QString &personId, float distance, 
                               const cv::Rect &faceRect, const QString &memberId);
    void drawLowQualityFace(cv::Mat &frame, const cv::Rect &faceRect);
    PersonInfo searchFaceInDatabase(const QVector<float> &feature);
    bool connectToDatabase();
    void disconnectFromDatabase();

    ModelManager* m_modelManager;
    SettingsManager* m_settingsManager;
    VideoWidget* m_videoWidget;
    QTimer* m_timer;
    cv::VideoCapture* m_videoCapture;
    PGconn* m_pgConn;
    bool m_isInitialized;
    bool m_isRunning;
};

#endif // FACERECOGNITIONCONTROLLER_H 