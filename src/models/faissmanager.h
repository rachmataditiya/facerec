#ifndef FAISSMANAGER_H
#define FAISSMANAGER_H

#include <QObject>
#include <QMap>
#include <QSet>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QPair>
#include <QJsonObject>
#include <faiss/IndexFlat.h>
#include <libpq-fe.h>

#define EMBEDDING_DIM 512

// Struktur untuk menyimpan informasi orang
struct PersonInfo {
    QString name;
    QString memberId;
};

// Forward declaration dari SettingsManager (dianggap sudah ada)
class SettingsManager;

class FaissManager : public QObject
{
    Q_OBJECT
public:
    explicit FaissManager(SettingsManager* settingsManager, QObject *parent = nullptr);
    ~FaissManager();

    // Metode untuk inisialisasi dan shutdown index
    bool initialize();
    bool shutdown();

    // Metode untuk memuat dan menyimpan cache index
    bool loadCachedIndex();
    bool saveCache();

    // Membuat index baru dan refresh dari database
    bool createIndex();
    bool refreshIndex(bool incremental = true);

    // Load informasi person dari database
    bool loadPersonInfo();

    // Parsing embedding (JSON array) menjadi QVector<float>
    QVector<float> parseEmbedding(const QString &embeddingStr);

    // Tambah face embedding ke index
    bool addFace(const QString &personId, const QVector<float> &feature, const QString &rowId);

    // Hapus face embedding untuk personId tertentu
    bool removeFace(const QString &personId);

    // Lakukan pencarian (recognize face) berdasarkan feature query
    QPair<QString, float> recognizeFace(const QVector<float> &feature);

    // Ambil daftar unik ID face/person
    QStringList getAllFaces() const;

    // Ambil informasi person berdasarkan personId
    PersonInfo getPersonInfo(const QString &personId) const;

private:
    // Metode untuk koneksi ke database PostgreSQL
    bool connectToDatabase();
    void disconnectFromDatabase();

    // Data anggota (gunakan urutan yang sama seperti deklarasi)
    faiss::IndexFlatIP *m_index;          // Pointer ke index FAISS
    QMap<int, QString> m_idMap;             // Mapping indeks FAISS ke personId
    QSet<QString> m_rowIds;                 // Set untuk row ID agar tidak duplikat
    QMap<QString, PersonInfo> m_personInfo; // Data informasi person (id, nama, memberId)
    QDateTime m_lastSyncTime;               // Waktu terakhir sinkronisasi dengan DB

    // Path file untuk cache
    QString m_dataDir;
    QString m_vectorPath;
    QString m_idMapPath;
    QString m_rowIdPath;
    QString m_personInfoPath;

    // Koneksi database PostgreSQL
    PGconn* m_pgConn;

    // SettingsManager untuk mengambil konfigurasi (misalnya path cache, kredensial DB)
    SettingsManager* m_settingsManager;
};

#endif // FAISSMANAGER_H
