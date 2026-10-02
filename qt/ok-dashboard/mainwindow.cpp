#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDateTime>
#include <QFile>
#include <QNetworkInterface>
#include <QTextStream>
#include <QTimer>

/* Read the first line of a sysfs/procfs file, empty string if missing */
static QString readFirstLine(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromLatin1(f.readLine()).trimmed();
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    /* layout, colors, fonts and the hostLabel text all live in mainwindow.ui */
    ui->setupUi(this);

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::refresh);
    timer->start(1000);

    cpuUsagePercent(); /* prime the first /proc/stat sample */
    refresh();
}

MainWindow::~MainWindow()
{
    delete ui;
}

/* CPU busy % since the previous call, from the aggregate "cpu" line */
int MainWindow::cpuUsagePercent()
{
    const QStringList f = readFirstLine("/proc/stat").split(' ', Qt::SkipEmptyParts);
    if (f.size() < 5)
        return 0;

    quint64 total = 0;
    for (int i = 1; i < f.size(); i++)
        total += f[i].toULongLong();
    /* idle + iowait */
    const quint64 idle = f[4].toULongLong() + (f.size() > 5 ? f[5].toULongLong() : 0);

    const quint64 dTotal = total - prevTotal;
    const quint64 dIdle = idle - prevIdle;
    prevTotal = total;
    prevIdle = idle;

    if (dTotal == 0)
        return 0;
    return int(100 * (dTotal - dIdle) / dTotal);
}

void MainWindow::refresh()
{
    const QDateTime now = QDateTime::currentDateTime();
    ui->clockLabel->setText(now.toString("HH:mm:ss"));
    ui->dateLabel->setText(now.toString("ddd dd/MM/yyyy"));

    /* first IPv4 address of an interface that is up, skipping loopback */
    QString ip;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || (flags & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ip = e.ip().toString();
                break;
            }
        }
        if (!ip.isEmpty())
            break;
    }
    ui->ipValue->setText(ip.isEmpty() ? "no link" : ip);

    const int cpu = cpuUsagePercent();
    const int mhz = readFirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq").toInt() / 1000;
    ui->cpuValue->setText(mhz > 0 ? QString("%1% %2MHz").arg(cpu).arg(mhz)
                                  : QString("%1%").arg(cpu));
    ui->cpuBar->setValue(cpu);

    /* MemTotal / MemAvailable in kB */
    quint64 memTotal = 0, memAvail = 0;
    QFile meminfo("/proc/meminfo");
    if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&meminfo);
        for (QString l = in.readLine(); !l.isNull(); l = in.readLine()) {
            const QStringList p = l.split(' ', Qt::SkipEmptyParts);
            if (p.size() < 2)
                continue;
            if (p[0] == "MemTotal:")
                memTotal = p[1].toULongLong();
            else if (p[0] == "MemAvailable:")
                memAvail = p[1].toULongLong();
        }
    }
    const quint64 memUsed = memTotal - memAvail;
    ui->ramValue->setText(QString("%1/%2 MB").arg(memUsed / 1024).arg(memTotal / 1024));
    ui->ramBar->setValue(memTotal ? int(100 * memUsed / memTotal) : 0);

    /* AM335x often has no usable thermal zone; show N/A in that case */
    const QString t = readFirstLine("/sys/class/thermal/thermal_zone0/temp");
    ui->tempValue->setText(t.isEmpty() ? "N/A"
                                       : QString("%1 °C").arg(t.toInt() / 1000.0, 0, 'f', 1));

    const qint64 up = qint64(readFirstLine("/proc/uptime").section(' ', 0, 0).toDouble());
    ui->upValue->setText(QString("%1d %2:%3:%4")
                             .arg(up / 86400)
                             .arg((up / 3600) % 24, 2, 10, QChar('0'))
                             .arg((up / 60) % 60, 2, 10, QChar('0'))
                             .arg(up % 60, 2, 10, QChar('0')));
}
