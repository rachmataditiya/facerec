#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>

// PostgreSQL
#include <libpq-fe.h>

// FAISS
#include <faiss/IndexFlat.h>
#include <faiss/Index.h>
#include <faiss/IndexIDMap.h>
#include <faiss/impl/IDSelector.h>

// InspireFace C API
#include <inspireface.h>

class SettingsManager;

struct PersonInfo {
    QString name;
    QString memberId;
};

class FaissManager : public QObject
{
    Q_OBJECT

public:
    explicit FaissManager(SettingsManager* settingsManager, QObject *parent = nullptr);
    ~FaissManager();

    bool initialize();
    bool shutdown();

    bool loadCachedIndex();
    bool refreshIndex(bool incremental = true);
    bool saveCache();
    bool createIndex();

    bool addFace(const QString &personId, const HFFaceFeature &feature, const QString &rowId);
    bool removeFace(const QString &personId);
    QVector<QPair<QString, float>> recognizeFace(const HFFaceFeature &feature);
    QStringList getAllFaces() const;

    bool loadPersonInfo();
    PersonInfo getPersonInfo(const QString &personId) const;

private:
    static const int EMBEDDING_DIM = 512;
    static const int BATCH_SIZE = 1000;

    SettingsManager* m_settingsManager;
    faiss::Index* m_index;
    QMap<int, QString> m_idMap;
    QSet<QString> m_rowIds;
    QMap<QString, PersonInfo> m_personInfo;
    QDateTime m_lastSyncTime;
    bool m_isInitialized;

    QString m_dataDir;
    QString m_vectorPath;
    QString m_idMapPath;
    QString m_rowIdPath;
    QString m_personInfoPath;
    PGconn* m_pgConn;

    bool connectToDatabase();
    void disconnectFromDatabase();
    QVector<float> parseEmbedding(const QString &embeddingStr);
    bool saveToFile(const QString &path, const QByteArray &data) const;
    QByteArray loadFromFile(const QString &path) const;
};