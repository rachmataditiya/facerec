#include "facerecognitioncontroller.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>

FaceRecognitionController::FaceRecognitionController(ModelManager* modelManager, SettingsManager* settingsManager, QObject *parent)
    : QObject(parent)
    , m_modelManager(modelManager)
    , m_settingsManager(settingsManager)
    , m_index(nullptr)
    , m_isInitialized(false)
{
}

FaceRecognitionController::~FaceRecognitionController()
{
    shutdown();
}

bool FaceRecognitionController::initialize()
{
    if (m_isInitialized) {
        return true;
    }

    // Create cache directory if it doesn't exist
    QString cachePath = m_settingsManager->getFaissCachePath();
    QDir().mkpath(cachePath);

    // Try to load existing index
    if (!loadIndex()) {
        // If no index exists, create a new one
        if (!createIndex()) {
            qDebug() << "Failed to create index";
            return false;
        }
    }

    m_isInitialized = true;
    return true;
}

void FaceRecognitionController::shutdown()
{
    if (m_index) {
        delete m_index;
        m_index = nullptr;
    }
    m_isInitialized = false;
}

bool FaceRecognitionController::loadIndex()
{
    QString cachePath = m_settingsManager->getFaissCachePath();
    QString indexFile = QDir(cachePath).filePath("faces.index");
    QString namesFile = QDir(cachePath).filePath("faces.names");

    if (!QFileInfo::exists(indexFile) || !QFileInfo::exists(namesFile)) {
        return false;
    }

    try {
        // Load index
        faiss::Index* index = faiss::read_index(indexFile.toStdString().c_str());
        if (!index) {
            return false;
        }

        // Load names
        QFile file(namesFile);
        if (!file.open(QIODevice::ReadOnly)) {
            delete index;
            return false;
        }

        QByteArray data = file.readAll();
        file.close();

        QStringList names = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
        m_faceNames = QVector<QString>(names.begin(), names.end());

        m_index = index;
        return true;
    } catch (const std::exception& e) {
        qDebug() << "Error loading index:" << e.what();
        return false;
    }
}

bool FaceRecognitionController::saveIndex()
{
    if (!m_index || !m_isInitialized) {
        return false;
    }

    QString cachePath = m_settingsManager->getFaissCachePath();
    QString indexFile = QDir(cachePath).filePath("faces.index");
    QString namesFile = QDir(cachePath).filePath("faces.names");

    try {
        // Save index
        faiss::write_index(m_index, indexFile.toStdString().c_str());

        // Save names
        QFile file(namesFile);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }

        QString names = m_faceNames.join('\n');
        file.write(names.toUtf8());
        file.close();

        return true;
    } catch (const std::exception& e) {
        qDebug() << "Error saving index:" << e.what();
        return false;
    }
}

bool FaceRecognitionController::createIndex()
{
    try {
        // Create a new index with 512 dimensions (typical face embedding size)
        m_index = new faiss::IndexFlatL2(512);
        m_faceNames.clear();
        return true;
    } catch (const std::exception& e) {
        qDebug() << "Error creating index:" << e.what();
        return false;
    }
}

bool FaceRecognitionController::addFace(const QString &name, const QImage &image)
{
    if (!m_isInitialized || !m_modelManager->isModelLoaded()) {
        return false;
    }

    // Extract features from image
    QVector<float> features = extractFeatures(image);
    if (features.isEmpty()) {
        return false;
    }

    try {
        // Add features to index
        m_index->add(1, features.data());
        m_faceNames.append(name);
        return saveIndex();
    } catch (const std::exception& e) {
        qDebug() << "Error adding face:" << e.what();
        return false;
    }
}

bool FaceRecognitionController::removeFace(const QString &name)
{
    if (!m_isInitialized) {
        return false;
    }

    int index = m_faceNames.indexOf(name);
    if (index == -1) {
        return false;
    }

    try {
        // Remove from index
        m_index->remove_ids(&index, 1);
        m_faceNames.remove(index);
        return saveIndex();
    } catch (const std::exception& e) {
        qDebug() << "Error removing face:" << e.what();
        return false;
    }
}

QString FaceRecognitionController::recognizeFace(const QImage &image)
{
    if (!m_isInitialized || !m_modelManager->isModelLoaded() || m_faceNames.isEmpty()) {
        return QString();
    }

    // Extract features from image
    QVector<float> features = extractFeatures(image);
    if (features.isEmpty()) {
        return QString();
    }

    try {
        // Search for nearest neighbor
        faiss::idx_t* I = new faiss::idx_t[1];
        float* D = new float[1];
        m_index->search(1, features.data(), 1, D, I);

        QString result = m_faceNames[I[0]];
        delete[] I;
        delete[] D;
        return result;
    } catch (const std::exception& e) {
        qDebug() << "Error recognizing face:" << e.what();
        return QString();
    }
}

QVector<QString> FaceRecognitionController::getAllFaces() const
{
    return m_faceNames;
}

QVector<float> FaceRecognitionController::extractFeatures(const QImage &image)
{
    // TODO: Implement feature extraction using InspireFace
    // This is a placeholder that returns dummy features
    return QVector<float>(512, 0.0f);
} 