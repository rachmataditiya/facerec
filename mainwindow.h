#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QJsonArray>
#include <QCheckBox>
#include <QListWidget>
#include <QTabWidget>
#include <opencv2/opencv.hpp>
#include <inspireface.h>

class QTimer;
namespace cv {
    class VideoCapture;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onSourceChanged(int index);
    void onStreamSelected(int index);
    void onAddStreamClicked();
    void onRemoveStreamClicked();
    void onModelPathButtonClicked();
    void onLoadModelClicked();
    void onModelSelectionChanged();
    void onStreamTableChanged(int row, int column);

private:
    void setupUI();
    void loadStreams();
    void saveStreams();
    void updateStreamComboBox();
    void updateStreamTable();
    void scanModelDirectory();
    bool initializeInspireFace();
    void unloadModel();
    void updateModelControls();
    void stopFaceDetection();
    void updateFrame();

    QTabWidget *tabWidget;
    QGroupBox *modelGroup;
    QGroupBox *controlGroup;
    QGroupBox *videoGroup;
    QGroupBox *streamGroup;

    QLineEdit *modelPathEdit;
    QPushButton *modelPathButton;
    QPushButton *loadModelButton;
    QListWidget *modelListWidget;

    QComboBox *sourceComboBox;
    QComboBox *streamComboBox;
    QLineEdit *rtspUrlEdit;
    QLineEdit *streamNameEdit;
    QLineEdit *streamUrlEdit;
    QPushButton *startButton;
    QPushButton *stopButton;
    QPushButton *addStreamButton;
    QPushButton *removeStreamButton;

    QLabel *videoLabel;
    QTableWidget *streamTable;

    cv::VideoCapture *videoCapture;
    QTimer *timer;
    bool isRunning;
    bool isModelLoaded;
    QJsonArray streams;

    HFSession session;
    HFSessionCustomParameter param;
};

#endif // MAINWINDOW_H 