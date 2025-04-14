#include "facerecognitioncontroller.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <inspireface.h>
#include <cstring>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <libpq-fe.h>

FaceRecognitionController::FaceRecognitionController(ModelManager* modelManager, 
                                                     SettingsManager* settingsManager,
                                                     VideoWidget* videoWidget,
                                                     QObject *parent)
    : QObject(parent)
    , m_modelManager(modelManager)
    , m_settingsManager(settingsManager)
    , m_videoWidget(videoWidget)
    , m_isInitialized(false)
    , m_isRunning(false)
    , m_timer(new QTimer(this))
    , m_videoCapture(nullptr)
    , m_pgConn(nullptr)
{
    // Konfigurasi Feature Hub
    HFFeatureHubConfiguration config;
    config.primaryKeyMode = HF_PK_MANUAL_INPUT;
    config.enablePersistence = 0; // Disable persistence karena menggunakan PostgreSQL
    config.persistenceDbPath = nullptr;
    config.searchThreshold = 0.6f;
    config.searchMode = HF_SEARCH_MODE_EXHAUSTIVE;
    
    HResult ret = HFFeatureHubDataEnable(config);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to configure Feature Hub";
    }

    // Optimasi konversi similarity
    HFSimilarityConverterConfig simConfig;
    simConfig.threshold = 0.42f;
    simConfig.middleScore = 0.6f;
    simConfig.steepness = 8.0f;
    simConfig.outputMin = 0.01f;
    simConfig.outputMax = 1.0f;
    
    ret = HFUpdateCosineSimilarityConverter(simConfig);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to update similarity converter config";
    }

    // Set threshold pencarian wajah
    ret = HFFeatureHubFaceSearchThresholdSetting(0.6f);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to set face search threshold";
    }

    connect(m_timer, SIGNAL(timeout()), this, SLOT(processFrame()));
}

// Destructor
FaceRecognitionController::~FaceRecognitionController()
{
    shutdown();
}

// Inisialisasi face recognition controller
bool FaceRecognitionController::initialize()
{
    if (m_isInitialized) {
        qDebug() << "Face recognition already initialized";
        return true;
    }

    if (!connectToDatabase()) {
        qDebug() << "Failed to connect to PostgreSQL database";
        return false;
    }

    qDebug() << "Initializing face recognition controller...";
    m_isInitialized = true;
    qDebug() << "Face recognition controller initialized successfully";
    return true;
}

// Shutdown controller
void FaceRecognitionController::shutdown()
{
    qDebug() << "Shutting down face recognition controller...";
    stopRecognition();
    disconnectFromDatabase();
    if (m_modelManager) {
        m_modelManager->unloadModel();
    }
    m_isInitialized = false;
    qDebug() << "Face recognition controller shutdown complete";
}

bool FaceRecognitionController::connectToDatabase()
{
    QString connStr = QString(
        "host='%1' port='%2' dbname='%3' user='%4' password='%5'")
        .arg(m_settingsManager->getPostgresHost())
        .arg(m_settingsManager->getPostgresPort())
        .arg(m_settingsManager->getPostgresDatabase())
        .arg(m_settingsManager->getPostgresUsername())
        .arg(m_settingsManager->getPostgresPassword());

    m_pgConn = PQconnectdb(connStr.toStdString().c_str());
    if (PQstatus(m_pgConn) != CONNECTION_OK) {
        qDebug() << "Connection to database failed:" << PQerrorMessage(m_pgConn);
        PQfinish(m_pgConn);
        m_pgConn = nullptr;
        return false;
    }
    return true;
}

void FaceRecognitionController::disconnectFromDatabase()
{
    if (m_pgConn) {
        PQfinish(m_pgConn);
        m_pgConn = nullptr;
    }
}

PersonInfo FaceRecognitionController::searchFaceInDatabase(const QVector<float> &feature)
{
    PersonInfo result;
    if (!m_pgConn || PQstatus(m_pgConn) != CONNECTION_OK) {
        qDebug() << "Database connection is not available";
        return result;
    }

    qDebug() << "Starting face search in database...";
    qDebug() << "Feature vector size:" << feature.size();

    // Convert vector to string with higher precision
    QString vectorStr = "'[";
    for (int i = 0; i < feature.size(); ++i) {
        if (i > 0) vectorStr += ",";
        vectorStr += QString::number(feature[i], 'f', 12); // Increased precision
    }
    vectorStr += "]'";

    qDebug() << "Executing search query with vector:" << vectorStr;

    // Execute the search_person_embedding function
    QString query = QString("SELECT * FROM search_person_embedding(%1::vector)").arg(vectorStr);
    qDebug() << "Query:" << query;
    
    PGresult *res = PQexec(m_pgConn, query.toStdString().c_str());

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        qDebug() << "Query failed:" << PQerrorMessage(m_pgConn);
        PQclear(res);
        return result;
    }

    int numRows = PQntuples(res);
    qDebug() << "Query returned" << numRows << "rows";

    if (numRows > 0) {
        result.id = QString::fromUtf8(PQgetvalue(res, 0, 1));        // person_id
        result.name = QString::fromUtf8(PQgetvalue(res, 0, 2));      // name
        result.memberId = QString::fromUtf8(PQgetvalue(res, 0, 3));  // member_id
        result.distance = QString::fromUtf8(PQgetvalue(res, 0, 5)).toFloat(); // distance

        qDebug() << "Found match:";
        qDebug() << "  Person ID:" << result.id;
        qDebug() << "  Name:" << result.name;
        qDebug() << "  Member ID:" << result.memberId;
        qDebug() << "  Distance:" << result.distance;
    } else {
        qDebug() << "No matches found in database";
    }

    PQclear(res);
    return result;
}

// Fungsi untuk melakukan face recognition dari QImage
QString FaceRecognitionController::recognizeFace(const QImage &image)
{
    if (!m_modelManager->isModelLoaded()) {
        qDebug() << "Model not loaded, cannot recognize face";
        return QString();
    }

    qDebug() << "Starting face recognition process...";
    // Konversi QImage ke cv::Mat
    cv::Mat mat(image.height(), image.width(), CV_8UC3, (void*)image.bits(), image.bytesPerLine());
    cv::cvtColor(mat, mat, cv::COLOR_BGR2RGB);

    // Siapkan struktur data gambar
    HFImageData imageData;
    imageData.data = mat.data;
    imageData.width = mat.cols;
    imageData.height = mat.rows;
    imageData.format = HF_STREAM_BGR;
    imageData.rotation = HF_CAMERA_ROTATION_0;

    // --- 1. Buat stream untuk deteksi ---
    HFImageStream detectionStream;
    int32_t ret = HFCreateImageStream(&imageData, &detectionStream);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream for detection. Error code:" << ret;
        return QString();
    }
    qDebug() << "Detection stream created successfully";

    // Dapatkan session dari model manager
    HFSession session = m_modelManager->getSession();

    // Deteksi wajah
    HFMultipleFaceData multipleFaceData;
    memset(&multipleFaceData, 0, sizeof(HFMultipleFaceData));
    ret = HFExecuteFaceTrack(session, detectionStream, &multipleFaceData);
    if (ret != HSUCCEED || multipleFaceData.detectedNum == 0) {
        qDebug() << "No face detected. Error code:" << ret;
        HFReleaseImageStream(detectionStream);
        return QString();
    }
    qDebug() << "Faces detected:" << multipleFaceData.detectedNum;

    // Buat token dari wajah pertama yang terdeteksi
    HFFaceBasicToken faceToken;
    faceToken.size = multipleFaceData.tokens[0].size;
    faceToken.data = new unsigned char[faceToken.size];
    memcpy(faceToken.data, multipleFaceData.tokens[0].data, faceToken.size);
    qDebug() << "Face token created, size:" << faceToken.size;

    // Lepaskan stream deteksi
    HFReleaseImageStream(detectionStream);

    // --- 2. Buat stream untuk ekstraksi fitur ---
    HFImageStream extractionStream;
    ret = HFCreateImageStream(&imageData, &extractionStream);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream for extraction. Error code:" << ret;
        delete[] static_cast<unsigned char*>(faceToken.data);
        return QString();
    }
    qDebug() << "Extraction stream created successfully";

    // Inisialisasi struktur fitur wajah
    HFFaceFeature feature;
    feature.size = 0;
    feature.data = nullptr;

    // Ekstraksi fitur wajah
    ret = HFFaceFeatureExtract(session, extractionStream, faceToken, &feature);
    delete[] static_cast<unsigned char*>(faceToken.data);
    HFReleaseImageStream(extractionStream);
    
    if (ret != HSUCCEED) {
        qDebug() << "Failed to extract features. Error code:" << ret;
        return QString();
    }
    qDebug() << "Features extracted successfully, size:" << feature.size;

    // --- Konversi HFFaceFeature ke QVector<float> ---
    QVector<float> featureVec;
    if (feature.size > 0 && feature.data) {
        featureVec.resize(feature.size);
        memcpy(featureVec.data(), feature.data, feature.size * sizeof(float));
    } else {
        qDebug() << "Feature extraction menghasilkan data kosong.";
        return QString();
    }

    // Search in PostgreSQL database
    PersonInfo personInfo = searchFaceInDatabase(featureVec);
    if (personInfo.id.isEmpty() || personInfo.distance > 0.75) {
        qDebug() << "No match found in database or distance too high:" << personInfo.distance;
        return QString();
    }

    qDebug() << "Best match found:" << personInfo.name << "with distance:" << personInfo.distance;
    return personInfo.id;
}

// Memulai proses face recognition (webcam atau RTSP)
bool FaceRecognitionController::startRecognition(int sourceIndex, const QString &streamUrl)
{
    if (m_isRunning) {
        qDebug() << "Recognition already running, stopping first...";
        stopRecognition();
    }

    if (!m_isInitialized || !m_modelManager->isModelLoaded()) {
        qDebug() << "Face recognition not initialized or model not loaded";
        return false;
    }

    qDebug() << "Starting face recognition...";
    m_videoCapture = new cv::VideoCapture();
    
    if (sourceIndex == 0) { // Webcam
        qDebug() << "Opening webcam...";
        m_videoCapture->open(0);
        m_videoCapture->set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        m_videoCapture->set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        m_videoCapture->set(cv::CAP_PROP_FPS, 30);
    } else { // RTSP
        qDebug() << "Opening RTSP stream:" << streamUrl;
        QString rtspUrl = streamUrl;
        if (!rtspUrl.contains("transport=")) {
            rtspUrl += (rtspUrl.contains("?") ? "&" : "?") + QString("transport=tcp");
        }
        if (!m_videoCapture->open(rtspUrl.toStdString())) {
            qDebug() << "Failed to open RTSP stream:" << rtspUrl;
            delete m_videoCapture;
            m_videoCapture = nullptr;
            return false;
        }
        m_videoCapture->set(cv::CAP_PROP_BUFFERSIZE, 1);
        m_videoCapture->set(cv::CAP_PROP_FPS, 30);
    }

    if (!m_videoCapture->isOpened()) {
        qDebug() << "Failed to open video source";
        delete m_videoCapture;
        m_videoCapture = nullptr;
        return false;
    }

    qDebug() << "Video source opened successfully";
    m_isRunning = true;
    m_timer->start(33); // ~30 FPS
    qDebug() << "Face recognition started successfully";
    return true;
}

// Menghentikan proses recognition
void FaceRecognitionController::stopRecognition()
{
    if (m_isRunning) {
        qDebug() << "Stopping face recognition...";
        m_timer->stop();
        if (m_videoCapture) {
            QString url = QString::fromStdString(m_videoCapture->getBackendName());
            m_videoCapture->release();
            delete m_videoCapture;
            m_videoCapture = nullptr;
            emit streamStopped(url);
            qDebug() << "Video capture stopped:" << url;
        }
        m_isRunning = false;
        m_videoWidget->clear();
        qDebug() << "Face recognition stopped successfully";
    }
}

// Proses frame video
void FaceRecognitionController::processFrame()
{
    if (!m_videoCapture || !m_videoCapture->isOpened()) {
        qDebug() << "Video capture not available, stopping recognition";
        stopRecognition();
        return;
    }

    cv::Mat frame;
    if (!m_videoCapture->read(frame)) {
        if (m_videoCapture->get(cv::CAP_PROP_BACKEND) == cv::CAP_FFMPEG) {
            // Try reconnect untuk RTSP
            QString url = QString::fromStdString(m_videoCapture->getBackendName());
            qDebug() << "RTSP stream disconnected, attempting to reconnect:" << url;
            m_videoCapture->release();
            if (!m_videoCapture->open(url.toStdString())) {
                qDebug() << "Failed to reconnect to RTSP stream";
                stopRecognition();
                return;
            }
            qDebug() << "Successfully reconnected to RTSP stream";
        } else {
            qDebug() << "Failed to read frame from video source";
            stopRecognition();
            return;
        }
    }

    if (frame.empty()) {
        qDebug() << "Received empty frame";
        return;
    }

    processFrame(frame);
}

void FaceRecognitionController::processFrame(const cv::Mat &frame)
{
    if (!m_isInitialized || !m_modelManager || !m_pgConn) {
        qDebug() << "Controller not initialized or model not loaded";
        return;
    }

    // Convert frame to HFImageStream
    HFImageData imageData;
    imageData.data = frame.data;
    imageData.width = frame.cols;
    imageData.height = frame.rows;
    imageData.format = HF_STREAM_BGR;
    imageData.rotation = HF_CAMERA_ROTATION_0;

    HFImageStream streamHandle;
    HResult ret = HFCreateImageStream(&imageData, &streamHandle);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to create image stream";
        return;
    }

    // Detect faces with additional flags
    HFMultipleFaceData results;
    memset(&results, 0, sizeof(HFMultipleFaceData));
    
    ret = HFExecuteFaceTrack(m_modelManager->getSession(), streamHandle, &results);
    if (ret != HSUCCEED) {
        qDebug() << "Failed to execute face track";
        HFReleaseImageStream(streamHandle);
        return;
    }

    qDebug() << "Detected" << results.detectedNum << "faces";

    // Process each detected face
    for (int i = 0; i < results.detectedNum; i++) {
        // Skip if confidence is too low
        if (results.detConfidence[i] < 0.5f) {
            qDebug() << "Skipping face" << i << "due to low confidence:" << results.detConfidence[i];
            continue;
        }

        // Skip if face is too small
        if (results.rects[i].width < 60 || results.rects[i].height < 60) {
            qDebug() << "Skipping face" << i << "due to small size:" 
                    << results.rects[i].width << "x" << results.rects[i].height;
            continue;
        }

        // Validate coordinates
        int x1 = std::max(0, std::min(results.rects[i].x, frame.cols - 1));
        int y1 = std::max(0, std::min(results.rects[i].y, frame.rows - 1));
        int x2 = std::max(0, std::min(results.rects[i].x + results.rects[i].width, frame.cols - 1));
        int y2 = std::max(0, std::min(results.rects[i].y + results.rects[i].height, frame.rows - 1));

        if (x2 <= x1 || y2 <= y1) {
            qDebug() << "Skipping face" << i << "due to invalid coordinates";
            continue;
        }

        // Create face token
        HFFaceBasicToken faceToken;
        faceToken.size = results.tokens[i].size;
        faceToken.data = new unsigned char[faceToken.size];
        memcpy(faceToken.data, results.tokens[i].data, faceToken.size);

        // Check face quality first
        float qualityScore;
        ret = HFFaceQualityDetect(m_modelManager->getSession(), faceToken, &qualityScore);
        if (ret == HSUCCEED && qualityScore < 0.7f) {
            qDebug() << "Skipping face" << i << "due to low quality score:" << qualityScore;
            delete[] static_cast<unsigned char*>(faceToken.data);
            continue;
        }

        // Extract face feature
        HFFaceFeature feature;
        feature.size = 0;
        feature.data = nullptr;

        ret = HFFaceFeatureExtract(m_modelManager->getSession(), streamHandle, faceToken, &feature);
        delete[] static_cast<unsigned char*>(faceToken.data);

        if (ret != HSUCCEED) {
            qDebug() << "Failed to extract face feature for face" << i;
            continue;
        }

        // Convert feature to QVector
        QVector<float> featureVec;
        if (feature.size > 0 && feature.data) {
            featureVec.resize(feature.size);
            memcpy(featureVec.data(), feature.data, feature.size * sizeof(float));
        } else {
            qDebug() << "Feature extraction menghasilkan data kosong.";
            continue;
        }

        // Search in database with higher precision
        PersonInfo personInfo = searchFaceInDatabase(featureVec);
        if (personInfo.id.isEmpty() || personInfo.distance > 0.75) {
            qDebug() << "No match found in database or distance too high:" << personInfo.distance;
            continue;
        }

        qDebug() << "Best match found:" << personInfo.name << "with distance:" << personInfo.distance;
        
        // Convert HFaceRect to cv::Rect
        cv::Rect faceRect(
            results.rects[i].x,
            results.rects[i].y,
            results.rects[i].width,
            results.rects[i].height
        );
        
        drawRecognitionResults(frame, personInfo.id, personInfo.distance, faceRect, personInfo.memberId);
    }

    HFReleaseImageStream(streamHandle);
    m_videoWidget->setFrame(frame);
}

void FaceRecognitionController::updateLastSeen(const QString &personId)
{
    if (!m_pgConn || PQstatus(m_pgConn) != CONNECTION_OK) {
        qDebug() << "Database connection is not available";
        return;
    }

    QString query = QString("UPDATE persons SET last_seen = NOW() WHERE id = '%1'").arg(personId);
    PGresult *res = PQexec(m_pgConn, query.toStdString().c_str());

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        qDebug() << "Failed to update last seen:" << PQerrorMessage(m_pgConn);
    }

    PQclear(res);
}

// Menggambar hasil recognition di atas frame
void FaceRecognitionController::drawRecognitionResults(cv::Mat frame, const QString &personId, float distance, 
                                                     const cv::Rect &faceRect, const QString &memberId)
{
    // Gambar rectangle wajah
    cv::rectangle(frame, faceRect, cv::Scalar(0, 255, 0), 2);

    // Buat label yang akan ditampilkan
    std::vector<std::string> labelLines;
    if (personId == "Unknown") {
        labelLines.push_back("Unknown");
    } else {
        // Query nama dari database
        QString query = QString("SELECT name FROM persons WHERE id = '%1'").arg(personId);
        PGresult *res = PQexec(m_pgConn, query.toStdString().c_str());
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            labelLines.push_back(PQgetvalue(res, 0, 0));
        } else {
            labelLines.push_back("Unknown");
        }
        PQclear(res);
    }
    
    if (!memberId.isEmpty())
        labelLines.push_back("ID: " + memberId.toStdString());
    labelLines.push_back(QString("Score: %1%").arg(int((1.0 - distance) * 100)).toStdString());

    int font = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.8;
    int thickness = 3;
    cv::Scalar color = (personId == "Unknown") ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 255, 255);
    int verticalOffset = 90;
    int lineHeight = 30;

    // Gambar setiap baris teks di atas rectangle wajah
    for (size_t i = 0; i < labelLines.size(); i++) {
        cv::Size textSize = cv::getTextSize(labelLines[i], font, fontScale, thickness, nullptr);
        int textX = faceRect.x + ((faceRect.width - textSize.width) / 2);
        int textY = faceRect.y - verticalOffset - (lineHeight * (labelLines.size() - i - 1));
        cv::putText(frame, labelLines[i], cv::Point(textX, textY),
                    font, fontScale, color, thickness);
    }
}

// Menggambar tanda untuk wajah berkualitas rendah
void FaceRecognitionController::drawLowQualityFace(cv::Mat frame, const cv::Rect &faceRect)
{
    // Gambar rectangle dengan warna merah untuk wajah berkualitas rendah
    cv::rectangle(frame, faceRect, cv::Scalar(0, 0, 255), 2);

    std::string label = "Low Quality";
    int font = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.8;
    int thickness = 3;
    cv::Scalar color = cv::Scalar(0, 0, 255);
    int verticalOffset = 90;

    cv::Size textSize = cv::getTextSize(label, font, fontScale, thickness, nullptr);
    int textX = faceRect.x + ((faceRect.width - textSize.width) / 2);
    int textY = faceRect.y - verticalOffset;
    cv::putText(frame, label, cv::Point(textX, textY),
                font, fontScale, color, thickness);
}
