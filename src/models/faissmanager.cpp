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
                return false;
            }
            
            QFileInfo fileInfo(file);
            if (fileInfo.size() == 0) {
                qDebug() << "Cache file empty:" << file;
                return false;
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
        return false;
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

bool FaissManager::loadPersonInfo()
{
    // TODO: Implement loading person info from database
    return true;
}

bool FaissManager::addFace(const QString &personId, const inspire::FaceEmbedding &embedding, const QString &rowId)
{
    if (!m_isInitialized) {
        return false;
    }

    try {
        // Convert embedding to vector<float>
        std::vector<float> vec(EMBEDDING_DIM);
        for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
            vec[i] = embedding.embedding[i];
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

QVector<QPair<QString, float>> FaissManager::recognizeFace(const inspire::FaceEmbedding &embedding)
{
    if (!m_isInitialized || m_index->ntotal == 0) {
        return {};
    }

    try {
        // Convert embedding to vector<float>
        std::vector<float> vec(EMBEDDING_DIM);
        for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
            vec[i] = embedding.embedding[i];
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

bool FaissManager::refreshIndex(bool incremental)
{
    // TODO: Implement index refresh from database
    Q_UNUSED(incremental)
    return true;
}

PersonInfo FaissManager::getPersonInfo(const QString &personId) const
{
    return m_personInfo.value(personId);
} 