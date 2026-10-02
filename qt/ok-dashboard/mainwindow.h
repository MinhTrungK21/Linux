#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void refresh();

private:
    int cpuUsagePercent();

    Ui::MainWindow *ui;

    /* previous /proc/stat sample for CPU usage */
    quint64 prevTotal = 0;
    quint64 prevIdle = 0;
};
#endif // MAINWINDOW_H
