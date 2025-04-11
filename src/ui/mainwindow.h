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
    void onLoadModelClicked();
    void onModelSelectionChanged();
    void onSourceChanged(int index);
    void onStreamSelected(int index);
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onAddStreamClicked();
    void onRemoveStreamClicked();
    void onStreamTableChanged(int row, int column);

private:
    Ui::MainWindow *ui;
    ModelManager *m_modelManager;
    SettingsManager *m_settingsManager;
    FaceDetectionController *m_faceDetectionController;

    void updateStreamComboBox();
    void updateStreamTable();
    void updateModelControls();
};

#endif // MAINWINDOW_H 