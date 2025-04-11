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
};

#endif // SETTINGSMANAGER_H 