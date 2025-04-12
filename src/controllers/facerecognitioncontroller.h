#ifndef FACERECOGNITIONCONTROLLER_H
#define FACERECOGNITIONCONTROLLER_H

#include <QObject>
#include <QImage>
#include <QVector>
#include <QString>
#include <QDir>
#include <faiss/IndexFlat.h>
#include <faiss/IndexIVFFlat.h>
#include <faiss/index_io.h>
#include "../models/modelmanager.h"
#include "../models/settingsmanager.h"

class FaceRecognitionController : public QObject
{
    Q_OBJECT

public:
    explicit FaceRecognitionController(ModelManager* modelManager, SettingsManager* settingsManager, QObject *parent = nullptr);
    ~FaceRecognitionController();

    bool initialize();
    void shutdown();

    // Face recognition functions
    bool addFace(const QString &name, const QImage &image);
    bool removeFace(const QString &name);
    QString recognizeFace(const QImage &image);
    QVector<QString> getAllFaces() const;

private:
    ModelManager* m_modelManager;
    SettingsManager* m_settingsManager;
    faiss::Index* m_index;
    QVector<QString> m_faceNames;
    bool m_isInitialized;

    bool loadIndex();
    bool saveIndex();
    bool createIndex();
    QVector<float> extractFeatures(const QImage &image);
};

#endif // FACERECOGNITIONCONTROLLER_H 