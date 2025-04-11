#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <opencv2/opencv.hpp>
#include <inspireface.h>

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
    void onAddStreamClicked();
    void onRemoveStreamClicked();
    void onStreamSelected(int index);

private:
    void setupUI();
    void startFaceDetection();
    void stopFaceDetection();
    void updateFrame();
    void loadStreams();
    void saveStreams();
    void updateStreamComboBox();
    void updateStreamTable();

    // UI Components
    QTabWidget *tabWidget;
    QComboBox *sourceComboBox;
    QComboBox *streamComboBox;
    QLineEdit *rtspUrlEdit;
    QLineEdit *streamNameEdit;
    QPushButton *startButton;
    QPushButton *stopButton;
    QPushButton *addStreamButton;
    QPushButton *removeStreamButton;
    QLabel *videoLabel;
    QGroupBox *controlGroup;
    QGroupBox *videoGroup;
    QGroupBox *streamGroup;
    QTableWidget *streamTable;

    // Video processing
    cv::VideoCapture *videoCapture;
    QTimer *timer;
    bool isRunning;
    HFSession session;
    HFSessionCustomParameter param;

    // Stream management
    QJsonArray streams;
};

#endif // MAINWINDOW_H 