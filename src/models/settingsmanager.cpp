#include "settingsmanager.h"
#include <QJsonDocument>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    // Get home directory
    QString homePath = QDir::homePath();
    m_settingsPath = QDir(homePath).filePath(".facerec/settings.json");
    
    // Create directory if it doesn't exist
    QDir().mkpath(QFileInfo(m_settingsPath).path());
    
    // Initialize default settings
    initializeDefaultSettings();
    
    // Load existing settings or create new one
    if (!loadSettings()) {
        saveSettings(); // Save default settings if no file exists
    }
}

SettingsManager::~SettingsManager()
{
    // Save settings before destroying
    saveSettings();
}

void SettingsManager::initializeDefaultSettings()
{
    QString homePath = QDir::homePath();
    
    m_settings["modelPath"] = QDir(homePath).filePath(".inspireface/models");
    
    // PostgreSQL settings with default values
    QJsonObject defaultPostgresSettings;
    defaultPostgresSettings["host"] = "localhost";
    defaultPostgresSettings["port"] = 5432;
    defaultPostgresSettings["database"] = "facerec";
    defaultPostgresSettings["username"] = "postgres";
    defaultPostgresSettings["password"] = "";
    m_settings["postgresSettings"] = defaultPostgresSettings;
    
    // Supabase settings with default values
    QJsonObject defaultSupabaseSettings;
    defaultSupabaseSettings["url"] = "";
    defaultSupabaseSettings["key"] = "";
    m_settings["supabaseSettings"] = defaultSupabaseSettings;
    
    // Model parameters with default values
    QJsonObject defaultParams;
    defaultParams["enable_recognition"] = 1;
    defaultParams["enable_liveness"] = 1;
    defaultParams["enable_mask_detect"] = 1;
    defaultParams["enable_face_attribute"] = 1;
    defaultParams["enable_face_quality"] = 1;
    defaultParams["enable_ir_liveness"] = 0;
    defaultParams["enable_interaction_liveness"] = 0;
    defaultParams["enable_detect_mode_landmark"] = 1;
    m_settings["modelParameters"] = defaultParams;
    
    // Detection parameters with default values
    QJsonObject defaultDetectionParams;
    defaultDetectionParams["face_detect_threshold"] = 0.7;
    defaultDetectionParams["track_mode_smooth_ratio"] = 0.7;
    defaultDetectionParams["filter_minimum_face_pixel_size"] = 60;
    m_settings["detectionParameters"] = defaultDetectionParams;
    
    // Initialize empty streams array
    m_settings["streams"] = QJsonArray();
}

bool SettingsManager::loadSettings()
{
    QFile file(m_settingsPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open settings file:" << m_settingsPath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty()) {
        qDebug() << "Settings file is empty";
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        qDebug() << "Invalid settings file format";
        return false;
    }

    QJsonObject loadedSettings = doc.object();
    
    // Merge loaded settings with default settings to ensure all fields exist
    if (!loadedSettings.contains("modelPath")) {
        loadedSettings["modelPath"] = m_settings["modelPath"];
    }
    
    if (!loadedSettings.contains("streams")) {
        loadedSettings["streams"] = m_settings["streams"];
    }
    
    if (!loadedSettings.contains("modelParameters")) {
        loadedSettings["modelParameters"] = m_settings["modelParameters"];
    } else {
        // Ensure all parameters exist in loaded settings
        QJsonObject defaultParams = m_settings["modelParameters"].toObject();
        QJsonObject loadedParams = loadedSettings["modelParameters"].toObject();
        for (auto it = defaultParams.begin(); it != defaultParams.end(); ++it) {
            if (!loadedParams.contains(it.key())) {
                loadedParams[it.key()] = it.value();
            }
        }
        loadedSettings["modelParameters"] = loadedParams;
    }
    
    if (!loadedSettings.contains("detectionParameters")) {
        loadedSettings["detectionParameters"] = m_settings["detectionParameters"];
    } else {
        // Ensure all detection parameters exist in loaded settings
        QJsonObject defaultParams = m_settings["detectionParameters"].toObject();
        QJsonObject loadedParams = loadedSettings["detectionParameters"].toObject();
        for (auto it = defaultParams.begin(); it != defaultParams.end(); ++it) {
            if (!loadedParams.contains(it.key())) {
                loadedParams[it.key()] = it.value();
            }
        }
        loadedSettings["detectionParameters"] = loadedParams;
    }

    // Load PostgreSQL settings
    if (!loadedSettings.contains("postgresSettings")) {
        loadedSettings["postgresSettings"] = m_settings["postgresSettings"];
    } else {
        // Ensure all PostgreSQL settings exist
        QJsonObject defaultParams = m_settings["postgresSettings"].toObject();
        QJsonObject loadedParams = loadedSettings["postgresSettings"].toObject();
        for (auto it = defaultParams.begin(); it != defaultParams.end(); ++it) {
            if (!loadedParams.contains(it.key())) {
                loadedParams[it.key()] = it.value();
            }
        }
        loadedSettings["postgresSettings"] = loadedParams;
    }

    // Load Supabase settings
    if (!loadedSettings.contains("supabaseSettings")) {
        loadedSettings["supabaseSettings"] = m_settings["supabaseSettings"];
    } else {
        // Ensure all Supabase settings exist
        QJsonObject defaultParams = m_settings["supabaseSettings"].toObject();
        QJsonObject loadedParams = loadedSettings["supabaseSettings"].toObject();
        for (auto it = defaultParams.begin(); it != defaultParams.end(); ++it) {
            if (!loadedParams.contains(it.key())) {
                loadedParams[it.key()] = it.value();
            }
        }
        loadedSettings["supabaseSettings"] = loadedParams;
    }
    
    m_settings = loadedSettings;
    return true;
}

bool SettingsManager::saveSettings()
{
    QFile file(m_settingsPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Cannot open settings file for writing:" << m_settingsPath;
        return false;
    }

    QJsonDocument doc(m_settings);
    if (file.write(doc.toJson(QJsonDocument::Indented)) == -1) {
        qDebug() << "Error writing settings file:" << file.errorString();
        file.close();
        return false;
    }

    file.close();
    return true;
}

QString SettingsManager::getModelPath() const
{
    return m_settings["modelPath"].toString();
}

void SettingsManager::setModelPath(const QString &path)
{
    m_settings["modelPath"] = path;
    saveSettings();
}

void SettingsManager::addStream(const QString &name, const QString &url)
{
    QJsonObject stream;
    stream["name"] = name;
    stream["url"] = url;
    
    QJsonArray streams = m_settings["streams"].toArray();
    streams.append(stream);
    m_settings["streams"] = streams;
    saveSettings();
}

void SettingsManager::removeStream(int index)
{
    QJsonArray streams = m_settings["streams"].toArray();
    if (index >= 0 && index < streams.size()) {
        streams.removeAt(index);
        m_settings["streams"] = streams;
        saveSettings();
    }
}

void SettingsManager::updateStream(int index, const QString &name, const QString &url)
{
    QJsonArray streams = m_settings["streams"].toArray();
    if (index >= 0 && index < streams.size()) {
        QJsonObject stream = streams[index].toObject();
        stream["name"] = name;
        stream["url"] = url;
        streams[index] = stream;
        m_settings["streams"] = streams;
        saveSettings();
    }
}

QJsonObject SettingsManager::getStream(int index) const
{
    QJsonArray streams = m_settings["streams"].toArray();
    if (index >= 0 && index < streams.size()) {
        return streams[index].toObject();
    }
    return QJsonObject();
}

QJsonArray SettingsManager::getAllStreams() const
{
    return m_settings["streams"].toArray();
}

int SettingsManager::getStreamCount() const
{
    return m_settings["streams"].toArray().size();
}

void SettingsManager::setModelParameters(const QJsonObject &params)
{
    m_settings["modelParameters"] = params;
    saveSettings();
}

QJsonObject SettingsManager::getModelParameters() const
{
    return m_settings["modelParameters"].toObject();
}

void SettingsManager::setDetectionParameters(const QJsonObject &params)
{
    m_settings["detectionParameters"] = params;
    saveSettings();
}

QJsonObject SettingsManager::getDetectionParameters() const
{
    return m_settings["detectionParameters"].toObject();
}

void SettingsManager::setPostgresSettings(const QJsonObject &settings)
{
    m_settings["postgresSettings"] = settings;
    saveSettings();
}

QJsonObject SettingsManager::getPostgresSettings() const
{
    return m_settings["postgresSettings"].toObject();
}

void SettingsManager::setSupabaseSettings(const QJsonObject &settings)
{
    m_settings["supabaseSettings"] = settings;
    saveSettings();
}

QJsonObject SettingsManager::getSupabaseSettings() const
{
    return m_settings["supabaseSettings"].toObject();
}

QString SettingsManager::getPostgresHost() const
{
    return m_settings["postgresSettings"].toObject()["host"].toString("localhost");
}

int SettingsManager::getPostgresPort() const
{
    return m_settings["postgresSettings"].toObject()["port"].toInt(5432);
}

QString SettingsManager::getPostgresDatabase() const
{
    return m_settings["postgresSettings"].toObject()["database"].toString("facerec");
}

QString SettingsManager::getPostgresUsername() const
{
    return m_settings["postgresSettings"].toObject()["username"].toString("postgres");
}

QString SettingsManager::getPostgresPassword() const
{
    return m_settings["postgresSettings"].toObject()["password"].toString("");
} 