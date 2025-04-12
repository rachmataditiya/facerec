#include "faissmanager.h"
#include "settingsmanager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDataStream>

FaissManager::FaissManager(SettingsManager* settingsManager, QObject *parent)
    : QObject(parent)
    , m_settingsManager(settingsManager)
    , m_index(nullptr)
    , m_isInitialized(false)
    , m_pgConn(nullptr)
{
    // Setup paths using settings
    m_dataDir = m_settingsManager->getFaissCachePath();
    m_vectorPath = QDir(m_dataDir).filePath("vectors.dat");
    m_idMapPath = QDir(m_dataDir).filePath("id_map.dat");
    m_rowIdPath = QDir(m_dataDir).filePath("row_ids.dat");
    m_personInfoPath = QDir(m_dataDir).filePath("person_info.dat");

    // Create data directory if it doesn't exist
    QDir().mkpath(m_dataDir);
}

FaissManager::~FaissManager()
{
    shutdown();
}

bool FaissManager::initialize()
{
    if (m_isInitialized) {
        return true;
    }

    // Try to load cached index first
    if (!loadCachedIndex()) {
        qDebug() << "Cache not found, creating new index...";
        if (!createIndex()) {
            return false;
        }
        // Save empty index
        if (!saveCache()) {
            qDebug() << "Failed to save empty index";
            return false;
        }
    }

    m_isInitialized = true;
    return true;
}

bool FaissManager::shutdown()
{
    if (m_index) {
        delete m_index;
        m_index = nullptr;
    }
    m_isInitialized = false;
    return true;
}

bool FaissManager::loadCachedIndex()
{
    try {
        // Check if all cache files exist
        QStringList requiredFiles = {m_vectorPath, m_idMapPath, m_rowIdPath};
        for (const QString &file : requiredFiles) {
            if (!QFile::exists(file)) {
                qDebug() << "Cache file missing:" << file;
                qDebug() << "Building new index from database...";
                return createIndex() && refreshIndex(false);  // false for full refresh
            }
            
            QFileInfo fileInfo(file);
            if (fileInfo.size() == 0) {
                qDebug() << "Cache file empty:" << file;
                qDebug() << "Building new index from database...";
                return createIndex() && refreshIndex(false);  // false for full refresh
            }
        }

        // Load vectors
        QFile vectorFile(m_vectorPath);
        if (!vectorFile.open(QIODevice::ReadOnly)) {
            return false;
        }
        
        QDataStream vectorStream(&vectorFile);
        int numVectors, dimension;
        vectorStream >> numVectors >> dimension;
        
        if (dimension != EMBEDDING_DIM) {
            qDebug() << "Invalid vector dimension:" << dimension;
            return false;
        }
        
        std::vector<float> vectors(numVectors * dimension);
        vectorStream.readRawData(reinterpret_cast<char*>(vectors.data()), vectors.size() * sizeof(float));
        vectorFile.close();

        // Load ID map
        QFile idMapFile(m_idMapPath);
        if (!idMapFile.open(QIODevice::ReadOnly)) {
            return false;
        }
        QDataStream idMapStream(&idMapFile);
        idMapStream >> m_idMap;
        idMapFile.close();

        // Load row IDs
        QFile rowIdFile(m_rowIdPath);
        if (!rowIdFile.open(QIODevice::ReadOnly)) {
            return false;
        }
        QDataStream rowIdStream(&rowIdFile);
        rowIdStream >> m_rowIds;
        rowIdFile.close();

        // Create and populate index
        m_index = new faiss::IndexFlatIP(EMBEDDING_DIM);
        if (numVectors > 0) {
            // Normalize vectors
            for (int i = 0; i < numVectors; i++) {
                float norm = 0.0f;
                for (int j = 0; j < dimension; j++) {
                    norm += vectors[i * dimension + j] * vectors[i * dimension + j];
                }
                norm = std::sqrt(norm);
                if (norm > 0) {
                    for (int j = 0; j < dimension; j++) {
                        vectors[i * dimension + j] /= norm;
                    }
                }
            }
            m_index->add(numVectors, vectors.data());
        } else {
            qDebug() << "Cache is empty, loading from database...";
            return refreshIndex(false);  // false for full refresh
        }

        // Load person info if exists
        if (QFile::exists(m_personInfoPath)) {
            QFile personInfoFile(m_personInfoPath);
            if (personInfoFile.open(QIODevice::ReadOnly)) {
                QDataStream personInfoStream(&personInfoFile);
                QMap<QString, QJsonObject> tempPersonInfo;
                personInfoStream >> tempPersonInfo;
                for (auto it = tempPersonInfo.begin(); it != tempPersonInfo.end(); ++it) {
                    PersonInfo info;
                    info.name = it.value()["name"].toString();
                    info.memberId = it.value()["member_id"].toString();
                    m_personInfo[it.key()] = info;
                }
                personInfoFile.close();
            }
        } else {
            loadPersonInfo();
        }

        qDebug() << "Cache loaded:" << m_index->ntotal << "vectors";
        
        // If cache is empty or very small compared to database, refresh from database
        if (m_index->ntotal == 0) {
            qDebug() << "Cache is empty, loading from database...";
            return refreshIndex(false);  // false for full refresh
        }

        return true;

    } catch (const std::exception& e) {
        qDebug() << "Failed to load cache:" << e.what();
        // Delete corrupt cache files
        QStringList requiredFiles = {m_vectorPath, m_idMapPath, m_rowIdPath};
        for (const QString &file : requiredFiles) {
            if (QFile::exists(file)) {
                QFile::remove(file);
            }
        }
        // Try to build new index from database
        qDebug() << "Building new index from database...";
        return createIndex() && refreshIndex(false);  // false for full refresh
    }
}

bool FaissManager::saveCache()
{
    try {
        if (!m_index) {
            qDebug() << "Index not initialized";
            return false;
        }

        // Ensure cache directory exists
        QDir().mkpath(m_dataDir);

        // Get all vectors from index
        int numVectors = m_index->ntotal;
        std::vector<float> vectors(numVectors * EMBEDDING_DIM);
        for (int i = 0; i < numVectors; i++) {
            m_index->reconstruct(i, vectors.data() + i * EMBEDDING_DIM);
        }

        // Save vectors
        QFile vectorFile(m_vectorPath);
        if (!vectorFile.open(QIODevice::WriteOnly)) {
            return false;
        }
        QDataStream vectorStream(&vectorFile);
        vectorStream << numVectors << EMBEDDING_DIM;
        if (numVectors > 0) {
            vectorStream.writeRawData(reinterpret_cast<const char*>(vectors.data()), vectors.size() * sizeof(float));
        }
        vectorFile.close();

        // Save ID map
        QFile idMapFile(m_idMapPath);
        if (!idMapFile.open(QIODevice::WriteOnly)) {
            return false;
        }
        QDataStream idMapStream(&idMapFile);
        idMapStream << m_idMap;
        idMapFile.close();

        // Save row IDs
        QFile rowIdFile(m_rowIdPath);
        if (!rowIdFile.open(QIODevice::WriteOnly)) {
            return false;
        }
        QDataStream rowIdStream(&rowIdFile);
        rowIdStream << m_rowIds;
        rowIdFile.close();

        // Save person info
        QFile personInfoFile(m_personInfoPath);
        if (personInfoFile.open(QIODevice::WriteOnly)) {
            QDataStream personInfoStream(&personInfoFile);
            QMap<QString, QJsonObject> tempPersonInfo;
            for (auto it = m_personInfo.begin(); it != m_personInfo.end(); ++it) {
                QJsonObject obj;
                obj["name"] = it.value().name;
                obj["member_id"] = it.value().memberId;
                tempPersonInfo[it.key()] = obj;
            }
            personInfoStream << tempPersonInfo;
            personInfoFile.close();
        }

        qDebug() << "Cache saved:" << m_index->ntotal << "vectors";
        return true;

    } catch (const std::exception& e) {
        qDebug() << "Failed to save cache:" << e.what();
        return false;
    }
}

bool FaissManager::createIndex()
{
    try {
        m_index = new faiss::IndexFlatIP(EMBEDDING_DIM);
        m_idMap.clear();
        m_rowIds.clear();
        return true;
    } catch (const std::exception& e) {
        qDebug() << "Error creating index:" << e.what();
        return false;
    }
}

bool FaissManager::connectToDatabase()
{
    if (m_pgConn && PQstatus(m_pgConn) == CONNECTION_OK) {
        qDebug() << "🔄 Using existing PostgreSQL connection";
        return true;
    }

    qDebug() << "🔌 Connecting to PostgreSQL...";
    QString connInfo = QString(
        "host=%1 port=%2 dbname=%3 user=%4 password=%5"
    ).arg(
        m_settingsManager->getPostgresHost(),
        QString::number(m_settingsManager->getPostgresPort()),
        m_settingsManager->getPostgresDatabase(),
        m_settingsManager->getPostgresUsername(),
        m_settingsManager->getPostgresPassword()
    );

    m_pgConn = PQconnectdb(connInfo.toUtf8().constData());
    if (PQstatus(m_pgConn) != CONNECTION_OK) {
        qDebug() << "❌ Failed to connect to PostgreSQL:" << PQerrorMessage(m_pgConn);
        return false;
    }
    qDebug() << "✅ Connected to PostgreSQL successfully";
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
        return false;
    }

    qDebug() << "📚 Loading person info from database...";
    const char* query = "SELECT id, name, member_id FROM persons";
    PGresult* res = PQexec(m_pgConn, query);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        qDebug() << "❌ Failed to query persons:" << PQerrorMessage(m_pgConn);
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
        m_personInfo[id] = info;
    }

    qDebug() << "✅ Loaded" << rows << "persons from database";
    qDebug() << "👥 Person details:";
    qDebug() << "  • Total persons:" << m_personInfo.size();
    qDebug() << "  • With member ID:" << std::count_if(m_personInfo.begin(), m_personInfo.end(), 
        [](const PersonInfo& info) { return !info.memberId.isEmpty(); });
    qDebug() << "  • With name:" << std::count_if(m_personInfo.begin(), m_personInfo.end(), 
        [](const PersonInfo& info) { return !info.name.isEmpty(); });

    PQclear(res);
    return true;
}

QVector<float> FaissManager::parseEmbedding(const QString &embeddingStr)
{
    QJsonDocument doc = QJsonDocument::fromJson(embeddingStr.toUtf8());
    if (!doc.isArray()) {
        return {};
    }

    QJsonArray arr = doc.array();
    if (arr.size() != EMBEDDING_DIM) {
        qDebug() << "Invalid embedding dimension:" << arr.size();
        return {};
    }

    QVector<float> vec(EMBEDDING_DIM);
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        vec[i] = arr[i].toDouble();
    }
    return vec;
}

bool FaissManager::refreshIndex(bool incremental)
{
    if (!connectToDatabase()) {
        return false;
    }

    qDebug() << "🔄 Refreshing FAISS index..." << (incremental ? "(incremental)" : "(full)");

    // Get total count from database
    PGresult* countRes = PQexec(m_pgConn, "SELECT COUNT(*) FROM person_embeddings");
    if (PQresultStatus(countRes) != PGRES_TUPLES_OK) {
        qDebug() << "❌ Failed to get count:" << PQerrorMessage(m_pgConn);
        PQclear(countRes);
        return false;
    }
    int dbCount = QString(PQgetvalue(countRes, 0, 0)).toInt();
    PQclear(countRes);

    qDebug() << "📊 Database stats:";
    qDebug() << "  • Total embeddings in DB:" << dbCount;
    qDebug() << "  • Total vectors in FAISS:" << m_index->ntotal;
    qDebug() << "  • Total unique persons:" << m_personInfo.size();
    qDebug() << "  • Average embeddings per person:" 
             << (m_personInfo.size() > 0 ? QString::number(static_cast<double>(dbCount) / m_personInfo.size(), 'f', 1) : "0");

    // Prepare query
    QString queryStr = "SELECT id, face_embedding, person_id, created_at FROM person_embeddings";
    if (incremental && m_lastSyncTime.isValid()) {
        queryStr += " WHERE created_at > $1";
        qDebug() << "🕒 Loading embeddings since:" << m_lastSyncTime.toString(Qt::ISODate);
    }
    queryStr += " ORDER BY created_at DESC";

    const char* query = queryStr.toUtf8().constData();
    PGresult* res;

    if (incremental && m_lastSyncTime.isValid()) {
        const char* params[1] = { m_lastSyncTime.toString(Qt::ISODate).toUtf8().constData() };
        res = PQexecParams(m_pgConn, query, 1, nullptr, params, nullptr, nullptr, 0);
    } else {
        res = PQexec(m_pgConn, query);
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        qDebug() << "❌ Failed to query embeddings:" << PQerrorMessage(m_pgConn);
        PQclear(res);
        return false;
    }

    int rows = PQntuples(res);
    qDebug() << "📥 Processing" << rows << "embeddings...";

    int newCount = 0;
    int skippedCount = 0;
    int errorCount = 0;
    QSet<QString> uniquePersons;

    std::vector<float> vectors;
    vectors.reserve(rows * EMBEDDING_DIM);

    for (int row = 0; row < rows; row++) {
        QString rowId = QString(PQgetvalue(res, row, 0));
        if (m_rowIds.contains(rowId)) {
            skippedCount++;
            continue;
        }

        QString embeddingStr = QString(PQgetvalue(res, row, 1));
        QString personId = QString(PQgetvalue(res, row, 2));
        QString createdAt = QString(PQgetvalue(res, row, 3));

        QVector<float> vec = parseEmbedding(embeddingStr);
        if (vec.isEmpty()) {
            errorCount++;
            continue;
        }

        // Normalize vector
        float norm = 0.0f;
        for (float v : vec) {
            norm += v * v;
        }
        norm = std::sqrt(norm);
        if (norm > 0.0f) {
            for (float &v : vec) {
                v /= norm;
            }
        }

        vectors.insert(vectors.end(), vec.begin(), vec.end());
        m_idMap[m_index->ntotal] = personId;
        m_rowIds.insert(rowId);
        newCount++;

        QDateTime dt = QDateTime::fromString(createdAt, Qt::ISODate);
        if (!m_lastSyncTime.isValid() || dt > m_lastSyncTime) {
            m_lastSyncTime = dt;
        }

        uniquePersons.insert(personId);
    }

    qDebug() << "📥 Processing results:";
    qDebug() << "  ✅ Added:" << newCount << "vectors";
    qDebug() << "  ⏭️ Skipped:" << skippedCount << "vectors (already exists)";
    qDebug() << "  ❌ Errors:" << errorCount << "vectors";
    qDebug() << "  👥 Unique persons in batch:" << uniquePersons.size();
    qDebug() << "  📚 Total vectors in FAISS:" << m_index->ntotal;
    qDebug() << "  📈 Coverage:" << QString::number(static_cast<double>(m_index->ntotal) / dbCount * 100, 'f', 1) << "%";

    PQclear(res);

    if (newCount > 0) {
        qDebug() << "💾 Saving cache...";
        m_index->add(newCount, vectors.data());
        saveCache();
    } else {
        qDebug() << "No new embeddings found";
    }

    return true;
}

bool FaissManager::addFace(const QString &personId, const HFFaceFeature &feature, const QString &rowId)
{
    if (!m_isInitialized) {
        return false;
    }

    try {
        // Convert HFFaceFeature to vector<float>
        std::vector<float> vec(EMBEDDING_DIM);
        for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
            vec[i] = feature.data[i];
        }

        // Normalize vector
        float norm = 0.0f;
        for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
            norm += vec[i] * vec[i];
        }
        norm = std::sqrt(norm);
        if (norm > 0.0f) {
            for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
                vec[i] /= norm;
            }
        }

        // Add to index
        int idx = m_index->ntotal;
        m_index->add(1, vec.data());
        m_idMap[idx] = personId;
        m_rowIds.insert(rowId);
        
        return saveCache();
    } catch (const std::exception& e) {
        qDebug() << "Error adding face:" << e.what();
        return false;
    }
}

bool FaissManager::removeFace(const QString &personId)
{
    if (!m_isInitialized) {
        return false;
    }

    try {
        // Find all indices for this person ID
        QVector<int> indicesToRemove;
        for (auto it = m_idMap.begin(); it != m_idMap.end(); ++it) {
            if (it.value() == personId) {
                indicesToRemove.append(it.key());
            }
        }

        if (indicesToRemove.isEmpty()) {
            return false;
        }

        // Remove from index
        for (int idx : indicesToRemove) {
            faiss::IDSelectorRange selector(idx, idx + 1);
            m_index->remove_ids(selector);
            m_idMap.remove(idx);
        }

        return saveCache();
    } catch (const std::exception& e) {
        qDebug() << "Error removing face:" << e.what();
        return false;
    }
}

QVector<QPair<QString, float>> FaissManager::recognizeFace(const HFFaceFeature &feature)
{
    if (!m_isInitialized || m_index->ntotal == 0) {
        return {};
    }

    try {
        // Convert HFFaceFeature to vector<float>
        std::vector<float> vec(EMBEDDING_DIM);
        for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
            vec[i] = feature.data[i];
        }

        // Normalize vector
        float norm = 0.0f;
        for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
            norm += vec[i] * vec[i];
        }
        norm = std::sqrt(norm);
        if (norm > 0.0f) {
            for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
                vec[i] /= norm;
            }
        }

        // Search
        std::vector<faiss::idx_t> labels(1);
        std::vector<float> distances(1);
        m_index->search(1, vec.data(), 1, distances.data(), labels.data());

        QVector<QPair<QString, float>> results;
        if (labels[0] >= 0 && labels[0] < m_idMap.size()) {
            results.append({m_idMap[labels[0]], distances[0]});
        }
        return results;
    } catch (const std::exception& e) {
        qDebug() << "Error recognizing face:" << e.what();
        return {};
    }
}

QStringList FaissManager::getAllFaces() const
{
    QSet<QString> uniqueFaces;
    for (const QString &personId : m_idMap.values()) {
        uniqueFaces.insert(personId);
    }
    return uniqueFaces.values();
}

PersonInfo FaissManager::getPersonInfo(const QString &personId) const
{
    return m_personInfo.value(personId);
} 