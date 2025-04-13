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
#include "../controllers/facerecognitioncontroller.h"
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
    void onSaveAllSettingsButtonClicked();
    void onPostgresTestButtonClicked();
    void onSupabaseTestButtonClicked();
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
    void loadDatabaseSettings();
    void onModelLoaded(bool success);
    void onModelUnloaded();

private:
    Ui::MainWindow *ui;
    SettingsManager *m_settingsManager;
    ModelManager *m_modelManager;
    QObject *m_activeController;

    void updateStreamComboBox();
    void updateStreamTable();
    void updateModelControls();
};

#endif // MAINWINDOW_H 