#include "settingsmanager.h"
#include <QJsonDocument>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    // Initialize default settings
    m_settings["modelPath"] = "";
    m_settings["streams"] = QJsonArray();
    
    // Get home directory
    QString homePath = QDir::homePath();
    m_settingsPath = QDir(homePath).filePath(".facerec/settings.json");
    
    // Create directory if it doesn't exist
    QDir().mkpath(QFileInfo(m_settingsPath).path());
    
    // Load existing settings
    loadSettings();
}

SettingsManager::~SettingsManager()
{
}

bool SettingsManager::loadSettings()
{
    QFile file(m_settingsPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << m_settingsPath;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isObject()) {
        m_settings = doc.object();
        return true;
    }

    return false;
}

bool SettingsManager::saveSettings()
{
    QFile file(m_settingsPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Cannot open file for writing:" << m_settingsPath;
        return false;
    }

    QJsonDocument doc(m_settings);
    file.write(doc.toJson());
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