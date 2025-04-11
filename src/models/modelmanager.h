#ifndef MODELMANAGER_H
#define MODELMANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <inspireface.h>

class ModelManager : public QObject
{
    Q_OBJECT

public:
    explicit ModelManager(QObject *parent = nullptr);
    ~ModelManager();

    bool loadModel(const QString &modelPath);
    void unloadModel();
    bool isModelLoaded() const;
    HFSession getSession() const;
    HFSessionCustomParameter getParameters() const;

    QList<QString> scanModelDirectory(const QString &dirPath);
    
    // Fungsi untuk menyimpan dan memuat path model
    bool loadModelPath(const QString &filename = "model.json");
    bool saveModelPath(const QString &filename = "model.json");
    QString getModelPath() const;
    void setModelPath(const QString &path);

private:
    HFSession m_session;
    HFSessionCustomParameter m_param;
    bool m_isModelLoaded;
    QString m_modelPath;
};

#endif // MODELMANAGER_H 