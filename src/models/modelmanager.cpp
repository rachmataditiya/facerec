#include "modelmanager.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

ModelManager::ModelManager(QObject *parent)
    : QObject(parent)
    , m_session(nullptr)
    , m_isModelLoaded(false)
{
    // Initialize default parameters
    m_param.enable_recognition = 1;
    m_param.enable_liveness = 1;
    m_param.enable_mask_detect = 1;
    m_param.enable_face_attribute = 1;
    m_param.enable_face_quality = 1;
    m_param.enable_ir_liveness = 0;
    m_param.enable_interaction_liveness = 0;
    m_param.enable_detect_mode_landmark = 1;
}

ModelManager::~ModelManager()
{
    unloadModel();
}

bool ModelManager::loadModel(const QString &modelPath)
{
    if (m_isModelLoaded) {
        unloadModel();
    }

    HResult ret = HFLaunchInspireFace(modelPath.toStdString().c_str());
    if (ret != HSUCCEED) {
        qDebug() << "Failed to initialize InspireFace. Error code:" << ret;
        return false;
    }

    ret = HFCreateInspireFaceSession(m_param, HF_DETECT_MODE_LIGHT_TRACK, 1, 320, 0, &m_session);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create session. Error code:" << ret;
        HFTerminateInspireFace();
        return false;
    }

    // Set detection parameters
    HFSessionSetFaceDetectThreshold(m_session, 0.7f);
    HFSessionSetTrackModeSmoothRatio(m_session, 0.7f);
    HFSessionSetFilterMinimumFacePixelSize(m_session, 60);

    m_isModelLoaded = true;
    return true;
}

void ModelManager::unloadModel()
{
    if (m_isModelLoaded) {
        if (m_session) {
            HFReleaseInspireFaceSession(m_session);
            m_session = nullptr;
        }
        HFTerminateInspireFace();
        m_isModelLoaded = false;
    }
}

bool ModelManager::isModelLoaded() const
{
    return m_isModelLoaded;
}

HFSession ModelManager::getSession() const
{
    return m_session;
}

HFSessionCustomParameter ModelManager::getParameters() const
{
    return m_param;
}

QList<QString> ModelManager::scanModelDirectory(const QString &dirPath)
{
    QList<QString> models;
    QDir dir(dirPath);
    
    if (!dir.exists()) {
        qDebug() << "Directory does not exist:" << dirPath;
        return models;
    }

    QStringList files = dir.entryList(QDir::Files);
    for (const QString &file : files) {
        if (QFileInfo(file).suffix().isEmpty()) {
            models.append(file);
        }
    }

    return models;
}

bool ModelManager::loadModelPath(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << filename;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        m_modelPath = obj["modelPath"].toString();
        return true;
    }

    return false;
}

bool ModelManager::saveModelPath(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Cannot open file for writing:" << filename;
        return false;
    }

    QJsonObject obj;
    obj["modelPath"] = m_modelPath;
    
    QJsonDocument doc(obj);
    file.write(doc.toJson());
    file.close();
    
    return true;
}

QString ModelManager::getModelPath() const
{
    return m_modelPath;
}

void ModelManager::setModelPath(const QString &path)
{
    m_modelPath = path;
    saveModelPath();
} 