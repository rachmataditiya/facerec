#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

class SettingsManager : public QObject
{
    Q_OBJECT

public:
    explicit SettingsManager(QObject *parent = nullptr);
    ~SettingsManager();

    bool loadSettings();
    bool saveSettings();

    // Model settings
    QString getModelPath() const;
    void setModelPath(const QString &path);

    // Model parameters
    void setModelParameters(const QJsonObject &params);
    QJsonObject getModelParameters() const;

    // Detection parameters
    void setDetectionParameters(const QJsonObject &params);
    QJsonObject getDetectionParameters() const;

    // Faiss settings
    QString getFaissCachePath() const;
    void setFaissCachePath(const QString &path);

    // PostgreSQL settings
    void setPostgresSettings(const QJsonObject &settings);
    QJsonObject getPostgresSettings() const;
    QString getPostgresHost() const;
    int getPostgresPort() const;
    QString getPostgresDatabase() const;
    QString getPostgresUsername() const;
    QString getPostgresPassword() const;

    // Supabase settings
    void setSupabaseSettings(const QJsonObject &settings);
    QJsonObject getSupabaseSettings() const;

    // Stream settings
    void addStream(const QString &name, const QString &url);
    void removeStream(int index);
    void updateStream(int index, const QString &name, const QString &url);
    QJsonObject getStream(int index) const;
    QJsonArray getAllStreams() const;
    int getStreamCount() const;

private:
    QJsonObject m_settings;
    QString m_settingsPath;
    
    void initializeDefaultSettings();
};

#endif // SETTINGSMANAGER_H 