#include "faissmanager.h"
#include "settingsmanager.h"  // Asumsikan header ini menyediakan getter untuk konfigurasi
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cstring>

// Sertakan header untuk IDSelector agar IDSelectorRange dikenali
#include <faiss/impl/IDSelector.h>
// Sertakan header FAISS utilitas untuk mendapatkan faiss::fvec_renorm_L2
#include <faiss/utils/distances.h>

// === Custom stream operators untuk PersonInfo ===
QDataStream &operator<<(QDataStream &out, const PersonInfo &info)
{
    out << info.name << info.memberId;
    return out;
}

QDataStream &operator>>(QDataStream &in, PersonInfo &info)
{
    in >> info.name >> info.memberId;
    return in;
}

// === Konstruktor ===
// Urutan inisialisasi harus sesuai dengan urutan deklarasi di header.
FaissManager::FaissManager(SettingsManager* settingsManager, QObject *parent)
    : QObject(parent)
    , m_index(nullptr)
    , m_idMap()
    , m_rowIds()
    , m_personInfo()
    , m_lastSyncTime()
    , m_dataDir()
    , m_vectorPath()
    , m_idMapPath()
    , m_rowIdPath()
    , m_personInfoPath()
    , m_pgConn(nullptr)
    , m_settingsManager(settingsManager)
{
    // Setup path cache dari konfigurasi
    m_dataDir = m_settingsManager->getFaissCachePath();
    m_vectorPath = QDir(m_dataDir).filePath("vectors.dat");
    m_idMapPath  = QDir(m_dataDir).filePath("id_map.dat");
    m_rowIdPath  = QDir(m_dataDir).filePath("row_ids.dat");
    m_personInfoPath = QDir(m_dataDir).filePath("person_info.dat");

    QDir().mkpath(m_dataDir);
}

// Destructor: Tutup index dan koneksi database
FaissManager::~FaissManager()
{
    shutdown();
    disconnectFromDatabase();
}

// Inisialisasi index: coba load cache; jika gagal, refresh dari database
bool FaissManager::initialize()
{
    if (m_index) {
        return true;
    }
    if (!loadCachedIndex()) {
        qDebug() << "Cache tidak ditemukan atau tidak lengkap, membuat index baru...";
        if (!createIndex()) {
            return false;
        }
        if (!refreshIndex(false)) { // full refresh
            return false;
        }
    }
    return true;
}

// Shutdown index
bool FaissManager::shutdown()
{
    if (m_index) {
        delete m_index;
        m_index = nullptr;
    }
    return true;
}

// Memuat cache index dari file
bool FaissManager::loadCachedIndex()
{
    try {
        // Cek file cache yang diperlukan
        QStringList requiredFiles = { m_vectorPath, m_idMapPath, m_rowIdPath };
        for (const QString &file : requiredFiles) {
            if (!QFile::exists(file)) {
                qDebug() << "Cache file tidak ditemukan:" << file;
                qDebug() << "Membangun index baru dari database...";
                return createIndex() && refreshIndex(false);
            }
            QFileInfo fileInfo(file);
            if (fileInfo.size() == 0) {
                qDebug() << "Cache file kosong:" << file;
                qDebug() << "Membangun index baru dari database...";
                return createIndex() && refreshIndex(false);
            }
        }
        
        // Load vektor dari file
        QFile vectorFile(m_vectorPath);
        if (!vectorFile.open(QIODevice::ReadOnly)) {
            qDebug() << "Gagal membuka file vektor:" << m_vectorPath;
            return false;
        }
        QDataStream vectorStream(&vectorFile);
        int numVectors, dimension;
        vectorStream >> numVectors >> dimension;
        if (dimension != EMBEDDING_DIM) {
            qDebug() << "Dimensi vektor tidak valid:" << dimension << "(diinginkan:" << EMBEDDING_DIM << ")";
            return false;
        }
        std::vector<float> vectors(numVectors * dimension);
        if (numVectors > 0) {
            vectorStream.readRawData(reinterpret_cast<char*>(vectors.data()), numVectors * dimension * sizeof(float));
        }
        vectorFile.close();
        
        // Normalisasi seluruh batch dengan fungsi FAISS
        if (numVectors > 0)
            faiss::fvec_renorm_L2(EMBEDDING_DIM, numVectors, vectors.data());
        
        // Load mapping ID
        QFile idMapFile(m_idMapPath);
        if (!idMapFile.open(QIODevice::ReadOnly)) {
            qDebug() << "Gagal membuka file ID map:" << m_idMapPath;
            return false;
        }
        QDataStream idMapStream(&idMapFile);
        idMapStream >> m_idMap;
        idMapFile.close();
        
        // Load row IDs
        QFile rowIdFile(m_rowIdPath);
        if (!rowIdFile.open(QIODevice::ReadOnly)) {
            qDebug() << "Gagal membuka file row IDs:" << m_rowIdPath;
            return false;
        }
        QDataStream rowIdStream(&rowIdFile);
        rowIdStream >> m_rowIds;
        rowIdFile.close();

        // Buat index FAISS dan tambahkan vektor (yang sudah dinormalisasi)
        m_index = new faiss::IndexFlatIP(EMBEDDING_DIM);
        if (numVectors > 0) {
            m_index->add(numVectors, vectors.data());
        } else {
            qDebug() << "Cache kosong, memuat dari database...";
            return refreshIndex(false);
        }

        // Muat person info, jika file belum ada maka load dari database
        if (QFile::exists(m_personInfoPath)) {
            QFile personInfoFile(m_personInfoPath);
            if (personInfoFile.open(QIODevice::ReadOnly)) {
                QDataStream personInfoStream(&personInfoFile);
                personInfoStream >> m_personInfo;
                personInfoFile.close();
            } else {
                loadPersonInfo();
            }
        } else {
            loadPersonInfo();
        }

        qDebug() << "Cache dimuat:" << m_index->ntotal << "vektor";
        return true;
    } catch (const std::exception &e) {
        qDebug() << "Gagal memuat cache:" << e.what();
        QStringList requiredFiles = { m_vectorPath, m_idMapPath, m_rowIdPath };
        for (const QString &file : requiredFiles) {
            if (QFile::exists(file))
                QFile::remove(file);
        }
        return false;
    }
}

// Menyimpan cache index ke file
bool FaissManager::saveCache()
{
    try {
        if (!m_index) {
            qDebug() << "Index belum diinisialisasi, tidak ada data untuk disimpan";
            return false;
        }
        QDir().mkpath(m_dataDir);

        int numVectors = m_index->ntotal;
        std::vector<float> vectors(numVectors * EMBEDDING_DIM);
        for (int i = 0; i < numVectors; i++) {
            m_index->reconstruct(i, vectors.data() + i * EMBEDDING_DIM);
        }
        // Normalisasi ulang dengan fungsi FAISS
        if (numVectors > 0)
            faiss::fvec_renorm_L2(EMBEDDING_DIM, numVectors, vectors.data());

        QFile vectorFile(m_vectorPath);
        if (!vectorFile.open(QIODevice::WriteOnly)) {
            qDebug() << "Gagal membuka file vektor untuk ditulis:" << m_vectorPath;
            return false;
        }
        QDataStream vectorStream(&vectorFile);
        vectorStream << numVectors << EMBEDDING_DIM;
        if (numVectors > 0)
            vectorStream.writeRawData(reinterpret_cast<const char*>(vectors.data()),
                                        numVectors * EMBEDDING_DIM * sizeof(float));
        vectorFile.close();

        QFile idMapFile(m_idMapPath);
        if (!idMapFile.open(QIODevice::WriteOnly)) {
            qDebug() << "Gagal membuka file ID map untuk ditulis:" << m_idMapPath;
            return false;
        }
        QDataStream idMapStream(&idMapFile);
        idMapStream << m_idMap;
        idMapFile.close();

        QFile rowIdFile(m_rowIdPath);
        if (!rowIdFile.open(QIODevice::WriteOnly)) {
            qDebug() << "Gagal membuka file row IDs untuk ditulis:" << m_rowIdPath;
            return false;
        }
        QDataStream rowIdStream(&rowIdFile);
        rowIdStream << m_rowIds;
        rowIdFile.close();

        QFile personInfoFile(m_personInfoPath);
        if (personInfoFile.open(QIODevice::WriteOnly)) {
            QDataStream personInfoStream(&personInfoFile);
            personInfoStream << m_personInfo;
            personInfoFile.close();
        }

        qDebug() << "Cache disimpan:" << m_index->ntotal << "vektor";
        return true;
    } catch (const std::exception &e) {
        qDebug() << "Gagal menyimpan cache:" << e.what();
        return false;
    }
}

// Membuat index FAISS baru
bool FaissManager::createIndex()
{
    try {
        if (m_index) {
            delete m_index;
            m_index = nullptr;
        }
        m_index = new faiss::IndexFlatIP(EMBEDDING_DIM);
        m_idMap.clear();
        m_rowIds.clear();
        m_personInfo.clear();
        m_lastSyncTime = QDateTime();
        qDebug() << "Index FAISS baru dibuat:";
        qDebug() << "  • Tipe: FlatIP";
        qDebug() << "  • Dimensi:" << EMBEDDING_DIM;
        qDebug() << "  • Ukuran awal:" << m_index->ntotal;
        return true;
    } catch (const std::exception &e) {
        qDebug() << "Error membuat index:" << e.what();
        return false;
    }
}

// Refresh index dengan mengambil embedding baru dari database
bool FaissManager::refreshIndex(bool incremental)
{
    if (!connectToDatabase()) {
        return false;
    }

    qDebug() << "Menyegarkan index FAISS..." << (incremental ? "(incremental)" : "(full)");

    // Load informasi person terlebih dahulu
    if (!loadPersonInfo()) {
        qDebug() << "Gagal memuat informasi person";
        return false;
    }

    // Dapatkan jumlah total embedding di database
    PGresult* countRes = PQexec(m_pgConn, "SELECT COUNT(*) FROM person_embeddings");
    if (PQresultStatus(countRes) != PGRES_TUPLES_OK) {
        qDebug() << "Gagal mendapatkan count:" << PQerrorMessage(m_pgConn);
        PQclear(countRes);
        return false;
    }
    int dbCount = QString(PQgetvalue(countRes, 0, 0)).toInt();
    PQclear(countRes);
    qDebug() << "Statistik database: total embedding:" << dbCount
             << ", index FAISS saat ini:" << (m_index ? m_index->ntotal : 0);

    // Susun query untuk mengambil embedding
    QString queryStr = "SELECT id, face_embedding, person_id, created_at FROM person_embeddings";
    if (incremental && m_lastSyncTime.isValid()) {
        queryStr += " WHERE created_at > $1";
        qDebug() << "Memuat embedding sejak:" << m_lastSyncTime.toString(Qt::ISODate);
    }
    queryStr += " ORDER BY created_at ASC";

    std::string queryStdStr = queryStr.toStdString();
    const char* query = queryStdStr.c_str();
    PGresult* res;
    if (incremental && m_lastSyncTime.isValid()) {
        std::string timestampStr = m_lastSyncTime.toString(Qt::ISODate).toStdString();
        const char* params[1] = { timestampStr.c_str() };
        res = PQexecParams(m_pgConn, query, 1, nullptr, params, nullptr, nullptr, 0);
    } else {
        res = PQexec(m_pgConn, query);
    }
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        qDebug() << "Gagal mengambil embedding:" << PQerrorMessage(m_pgConn);
        PQclear(res);
        return false;
    }

    int rows = PQntuples(res);
    qDebug() << "Memproses" << rows << "embedding...";
    int newCount = 0, skippedCount = 0, errorCount = 0;
    QMap<QString, int> personEmbeddingCounts;
    std::vector<float> vectors;
    vectors.reserve(rows * EMBEDDING_DIM);
    int currentIndexSize = m_index->ntotal;

    for (int row = 0; row < rows; row++) {
        QString rowId = QString(PQgetvalue(res, row, 0));
        if (m_rowIds.contains(rowId)) {
            skippedCount++;
            continue;
        }
        QString embeddingStr = QString(PQgetvalue(res, row, 1));
        QString personId = QString(PQgetvalue(res, row, 2));
        QString createdAt = QString(PQgetvalue(res, row, 3));
        
        // Lewati jika informasi person tidak ditemukan
        if (!m_personInfo.contains(personId)) {
            qDebug() << "Lewati embedding untuk person tidak dikenal:" << personId;
            errorCount++;
            continue;
        }
        personEmbeddingCounts[personId]++;
        
        // Parsing string embedding menjadi QVector<float>
        QVector<float> vec = parseEmbedding(embeddingStr);
        if (vec.size() != EMBEDDING_DIM) {
            qDebug() << "Dimensi embedding tidak valid:" << vec.size() << "pada baris" << row;
            errorCount++;
            continue;
        }
        
        // Gunakan fungsi normalisasi FAISS untuk tiap vektor
        faiss::fvec_renorm_L2(EMBEDDING_DIM, 1, vec.data());
        
        // Tambahkan elemen vektor ke daftar
        for (int i = 0; i < EMBEDDING_DIM; i++) {
            vectors.push_back(vec[i]);
        }
        m_idMap[currentIndexSize + newCount] = personId;
        m_rowIds.insert(rowId);
        newCount++;

        QDateTime dt = QDateTime::fromString(createdAt, Qt::ISODate);
        if (!m_lastSyncTime.isValid() || dt > m_lastSyncTime) {
            m_lastSyncTime = dt;
        }
    }
    qDebug() << "Hasil pemrosesan: Ditambahkan:" << newCount 
             << "vektor, Dilewati:" << skippedCount << "vektor, Error:" << errorCount;
    qDebug() << "Unik persons dalam batch:" << personEmbeddingCounts.size();
    if (newCount > 0) {
        qDebug() << "Menambahkan vektor ke index...";
        m_index->add(newCount, vectors.data());
        qDebug() << "Ukuran index setelah penambahan:" << m_index->ntotal;
        qDebug() << "Menyimpan cache...";
        saveCache();
    } else {
        qDebug() << "Tidak ada embedding baru ditemukan";
    }
    PQclear(res);
    return true;
}

// Mengubah string JSON (array) menjadi QVector<float>
QVector<float> FaissManager::parseEmbedding(const QString &embeddingStr)
{
    QVector<float> vec;
    QJsonDocument doc = QJsonDocument::fromJson(embeddingStr.toUtf8());
    if (!doc.isArray()) {
        qDebug() << "Embedding bukan array JSON";
        return vec;
    }
    QJsonArray arr = doc.array();
    if (arr.size() != EMBEDDING_DIM) {
        qDebug() << "Dimensi embedding tidak valid:" << arr.size();
        return vec;
    }
    vec.resize(EMBEDDING_DIM);
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        vec[i] = arr[i].toDouble();
    }
    return vec;
}

// Menambahkan face embedding baru ke index
bool FaissManager::addFace(const QString &personId, const QVector<float> &feature, const QString &rowId)
{
    if (!m_index) {
        qDebug() << "Index belum diinisialisasi.";
        return false;
    }
    if (feature.size() != EMBEDDING_DIM) {
        qDebug() << "Dimensi feature tidak valid:" << feature.size();
        return false;
    }
    std::vector<float> vec(feature.begin(), feature.end());
    // Normalisasi vektor menggunakan fungsi FAISS untuk 1 vektor
    faiss::fvec_renorm_L2(EMBEDDING_DIM, 1, vec.data());
    
    int idx = m_index->ntotal;
    m_index->add(1, vec.data());
    m_idMap[idx] = personId;
    m_rowIds.insert(rowId);
    return saveCache();
}

// Menghapus face embedding berdasarkan personId
bool FaissManager::removeFace(const QString &personId)
{
    if (!m_index) {
        return false;
    }
    QVector<int> indicesToRemove;
    for (auto it = m_idMap.begin(); it != m_idMap.end(); ++it) {
        if (it.value() == personId) {
            indicesToRemove.append(it.key());
        }
    }
    if (indicesToRemove.isEmpty()) {
        return false;
    }

    // Hapus dari index menggunakan IDSelectorRange
    for (int idx : indicesToRemove) {
        faiss::IDSelectorRange selector(idx, idx + 1);
        m_index->remove_ids(selector);
        m_idMap.remove(idx);
    }
    return saveCache();
}

// Melakukan pencarian untuk face recognition
QPair<QString, float> FaissManager::recognizeFace(const QVector<float> &feature)
{
    // Inisialisasi bestMatch dengan jarak maksimum
    QPair<QString, float> bestMatch("", std::numeric_limits<float>::max());
    
    // Periksa apakah index sudah diinisialisasi dan tidak kosong
    if (!m_index || m_index->ntotal == 0) {
        qDebug() << "Index not initialized or empty.";
        return bestMatch;
    }
    
    // Validasi dimensi input
    if (feature.size() != EMBEDDING_DIM) {
        qDebug() << "Invalid feature dimension for query:" << feature.size()
                 << "Expected:" << EMBEDDING_DIM;
        return bestMatch;
    }
    
    // Konversi fitur ke std::vector dan lakukan normalisasi L2
    std::vector<float> queryFeature(feature.begin(), feature.end());
    faiss::fvec_renorm_L2(EMBEDDING_DIM, 1, queryFeature.data());
    
    // Atur parameter pencarian: gunakan 5 kandidat terdekat
    const int k = 5;
    std::vector<faiss::idx_t> labels(k, -1);  // inisialisasi dengan -1
    std::vector<float> distances(k, std::numeric_limits<float>::max());
    
    // Lakukan pencarian menggunakan FAISS
    m_index->search(1, queryFeature.data(), k, distances.data(), labels.data());
    
    // Iterasi kandidat untuk menemukan hasil terbaik dengan jarak terkecil
    for (int i = 0; i < k; ++i) {
        if (labels[i] >= 0 && m_idMap.contains(labels[i])) {
            QString personId = m_idMap[labels[i]];
            float currDistance = distances[i];
            
            // Jika hasil untuk personId yang sama muncul lebih dari satu kali,
            // pilih hasil dengan nilai jarak yang lebih kecil
            if (currDistance < bestMatch.second) {
                bestMatch.first = personId;
                bestMatch.second = currDistance;
            }
        }
    }
    
    // Jika ditemukan kandidat, tampilkan informasi dan hitung similarity score
    if (!bestMatch.first.isEmpty()) {
        qDebug() << "Best match found - Person ID:" << bestMatch.first 
                 << "Distance:" << bestMatch.second;
        
        if (m_personInfo.contains(bestMatch.first)) {
            PersonInfo info = m_personInfo[bestMatch.first];
            qDebug() << "Details - Name:" << info.name 
                     << "Member ID:" << info.memberId;
        }
        
        // Konversi jarak ke similarity score.
        // Catatan: Sesuaikan rumus berikut jika FAISS mengembalikan nilai L2 (bukan squared L2).
        float similarityScore = 1.0f - (bestMatch.second / 2.0f);
        similarityScore = std::max(0.0f, std::min(similarityScore, 1.0f));
        qDebug() << "Similarity score:" << similarityScore;
    } else {
        qDebug() << "No matching person found in the database";
    }
    
    return bestMatch;
}

QStringList FaissManager::getAllFaces() const
{
    QSet<QString> uniqueFaces;
    for (const auto &pid : m_idMap.values()) {
        uniqueFaces.insert(pid);
    }
    return uniqueFaces.values();
}

PersonInfo FaissManager::getPersonInfo(const QString &personId) const
{
    return m_personInfo.value(personId);
}

bool FaissManager::connectToDatabase()
{
    if (m_pgConn && PQstatus(m_pgConn) == CONNECTION_OK) {
        qDebug() << "Menggunakan koneksi PostgreSQL yang sudah ada";
        return true;
    }
    qDebug() << "Menghubungkan ke PostgreSQL...";
    QString connInfo = QString("host=%1 port=%2 dbname=%3 user=%4 password=%5")
        .arg(m_settingsManager->getPostgresHost(),
             QString::number(m_settingsManager->getPostgresPort()),
             m_settingsManager->getPostgresDatabase(),
             m_settingsManager->getPostgresUsername(),
             m_settingsManager->getPostgresPassword());
    m_pgConn = PQconnectdb(connInfo.toUtf8().constData());
    if (PQstatus(m_pgConn) != CONNECTION_OK) {
        qDebug() << "Gagal terhubung ke PostgreSQL:" << PQerrorMessage(m_pgConn);
        return false;
    }
    qDebug() << "Terhubung ke PostgreSQL dengan sukses";
    return true;
}

void FaissManager::disconnectFromDatabase()
{
    if (m_pgConn) {
        PQfinish(m_pgConn);
        m_pgConn = nullptr;
    }
}

bool FaissManager::loadPersonInfo()
{
    if (!connectToDatabase()) {
        qDebug() << "Tidak dapat terhubung ke database untuk memuat person info";
        return false;
    }

    qDebug() << "Memuat informasi person dari database...";
    const char* query = "SELECT id, name, member_id FROM persons ORDER BY id";
    PGresult* res = PQexec(m_pgConn, query);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        qDebug() << "Gagal mengambil data persons:" << PQerrorMessage(m_pgConn);
        PQclear(res);
        return false;
    }

    m_personInfo.clear();
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        QString id = QString(PQgetvalue(res, i, 0));
        PersonInfo info;
        info.name = QString(PQgetvalue(res, i, 1));
        info.memberId = QString(PQgetvalue(res, i, 2));
        m_personInfo.insert(id, info);
    }

    qDebug() << "Loaded" << rows << "persons from database";
    PQclear(res);
    return true;
}
