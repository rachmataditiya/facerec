#include "modelmanager.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

ModelManager::ModelManager(SettingsManager* settingsManager, QObject *parent)
    : QObject(parent)
    , m_session(nullptr)
    , m_isModelLoaded(false)
    , m_settingsManager(settingsManager)
{
    // Initialize parameters from settings
    QJsonObject params = m_settingsManager->getModelParameters();
    m_param.enable_recognition = params.value("enable_recognition").toInt(1);
    m_param.enable_liveness = params.value("enable_liveness").toInt(1);
    m_param.enable_mask_detect = params.value("enable_mask_detect").toInt(1);
    m_param.enable_face_attribute = params.value("enable_face_attribute").toInt(1);
    m_param.enable_face_quality = params.value("enable_face_quality").toInt(1);
    m_param.enable_ir_liveness = params.value("enable_ir_liveness").toInt(0);
    m_param.enable_interaction_liveness = params.value("enable_interaction_liveness").toInt(0);
    m_param.enable_detect_mode_landmark = params.value("enable_detect_mode_landmark").toInt(1);
}

ModelManager::~ModelManager()
{
    unloadModel();
}

bool ModelManager::loadModel()
{
    if (m_isModelLoaded) {
        unloadModel();
    }

    QString modelPath = m_settingsManager->getModelPath();
    if (modelPath.isEmpty()) {
        qDebug() << "Model path is not set in settings";
        emit modelLoaded(false);
        return false;
    }

    // Get the first model in the directory
    QList<QString> models = scanModelDirectory(modelPath);
    if (models.isEmpty()) {
        qDebug() << "No models found in directory:" << modelPath;
        emit modelLoaded(false);
        return false;
    }

    QString modelFullPath = modelPath + "/" + models.first();

    HResult ret = HFLaunchInspireFace(modelFullPath.toStdString().c_str());
    if (ret != HSUCCEED) {
        qDebug() << "Failed to initialize InspireFace. Error code:" << ret;
        emit modelLoaded(false);
        return false;
    }

    ret = HFCreateInspireFaceSession(m_param, HF_DETECT_MODE_LIGHT_TRACK, 1, 320, 0, &m_session);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create session. Error code:" << ret;
        HFTerminateInspireFace();
        emit modelLoaded(false);
        return false;
    }

    // Set detection parameters from settings
    QJsonObject detectionParams = m_settingsManager->getDetectionParameters();
    float faceDetectThreshold = detectionParams.value("face_detect_threshold").toDouble(0.7);
    float trackModeSmoothRatio = detectionParams.value("track_mode_smooth_ratio").toDouble(0.7);
    int filterMinimumFacePixelSize = detectionParams.value("filter_minimum_face_pixel_size").toInt(60);

    HFSessionSetFaceDetectThreshold(m_session, faceDetectThreshold);
    HFSessionSetTrackModeSmoothRatio(m_session, trackModeSmoothRatio);
    HFSessionSetFilterMinimumFacePixelSize(m_session, filterMinimumFacePixelSize);

    m_isModelLoaded = true;
    emit modelLoaded(true);
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
        emit modelUnloaded();
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