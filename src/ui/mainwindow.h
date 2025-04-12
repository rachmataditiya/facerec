#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QGroupBox>
#include "../models/modelmanager.h"
#include "../models/settingsmanager.h"
#include "../models/faissmanager.h"
#include "../controllers/facedetectioncontroller.h"
#include "videowidget.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onModelPathButtonClicked();
    void onLoadModelButtonClicked();
    void onModelParameterChanged();
    void onDetectionParameterChanged();
    void onFaissCachePathButtonClicked();
    void onSaveFaissSettingsButtonClicked();
    void onModelSelectionChanged();
    void onSourceChanged(int index);
    void onStreamSelected(int index);
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onAddStreamClicked();
    void onRemoveStreamClicked();
    void onStreamTableChanged(int row, int column);
    void loadModelParameters();
    void loadDetectionParameters();
    void loadFaissSettings();

private:
    Ui::MainWindow *ui;
    SettingsManager *m_settingsManager;
    ModelManager *m_modelManager;
    FaceDetectionController *m_faceDetectionController;
    FaissManager *m_faissManager;

    void updateStreamComboBox();
    void updateStreamTable();
    void updateModelControls();
};

#endif // MAINWINDOW_H 