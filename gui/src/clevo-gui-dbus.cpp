#include "clevo-gui.h"
#include "clevo-settings.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusInterface>
#include <QDBusError>
#include <QDBusConnectionInterface>

#define DBUS_SERVICE_NAME "org.freedesktop.ClevoDaemon"
#define DBUS_OBJECT_PATH "/org/freedesktop/ClevoDaemon"
#define DBUS_INTERFACE "org.freedesktop.ClevoDaemon"

ClevoMonitor::ClevoMonitor(QWidget *parent)
    : QWidget(parent)
    , updateTimer(nullptr)
    , contextMenu(nullptr)
    , settingsDialog(nullptr)
    , dbusConnected(false)
    , dbusSubscribed(false)
    , cpuTemp(0)
    , fanDuty(0)
    , fanRpm(0)
    , autoMode(false)
    , dataValid(false)
    , isTransparent(false)
    , isDragging(false)
    , updateInterval(1000)  // 1 second default
    , windowOpacity(0.85)
    , lastDisplayCpuTemp(-1)
    , lastDisplayFanDuty(-1)
    , lastDisplayFanRpm(-1)
    , lastDisplayAutoMode(false)
{
    setupWindow();
    setupTimer();
    setupDBus();
    connectToDaemonDBus();

    // Connect to DBus StatusChanged signal for push updates
    auto bus = QDBusConnection::systemBus();
    bus.connect(DBUS_SERVICE_NAME,
                DBUS_OBJECT_PATH,
                DBUS_INTERFACE,
                "StatusChanged",
                this,
                SLOT(onStatusChanged(int,int,int,bool)));
}

ClevoMonitor::~ClevoMonitor()
{
    // DBus connection is automatically cleaned up
}

void ClevoMonitor::setupWindow()
{
    // Set window flags for transparent, frameless, always-on-top window
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    
    // Set window size and opacity
    resize(200, 100);
    setWindowOpacity(windowOpacity);
    
    // Set window title
    setWindowTitle("Clevo Fan Monitor (DBus)");
    
    // Enable mouse tracking for dragging
    setMouseTracking(true);
}

void ClevoMonitor::setupTimer()
{
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &ClevoMonitor::updateStatus);
    updateTimer->start(updateInterval);
}

void ClevoMonitor::setupDBus()
{
    dbusConnected = false;
    dbusSubscribed = false;
}

void ClevoMonitor::setupContextMenu()
{
    contextMenu = new QMenu(this);
    
    QAction *transparencyAction = new QAction("Toggle Transparency", this);
    connect(transparencyAction, &QAction::triggered, this, &ClevoMonitor::toggleTransparency);
    contextMenu->addAction(transparencyAction);
    
    contextMenu->addSeparator();
    
    QAction *settingsAction = new QAction("Settings and Commands", this);
    connect(settingsAction, &QAction::triggered, this, &ClevoMonitor::openSettings);
    contextMenu->addAction(settingsAction);
    
    contextMenu->addSeparator();
    
    QAction *reconnectAction = new QAction("Reconnect to Daemon", this);
    connect(reconnectAction, &QAction::triggered, this, &ClevoMonitor::reconnectToDaemon);
    contextMenu->addAction(reconnectAction);
    
    contextMenu->addSeparator();
    
    QAction *quitAction = new QAction("Quit", this);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    contextMenu->addAction(quitAction);
}

void ClevoMonitor::connectToDaemonDBus()
{
    // Connect to system DBus (daemon runs on system bus)
    QDBusConnection connection = QDBusConnection::systemBus();
    
    if (!connection.isConnected()) {
        qDebug() << "Failed to connect to system bus";
        return;
    }
    
    // Quick service availability check
    QDBusReply<bool> isRegistered = connection.interface()->isServiceRegistered(DBUS_SERVICE_NAME);
    if (isRegistered.isValid() && isRegistered.value()) {
        dbusConnected = true;
        qDebug() << "Connected to DBus service:" << DBUS_SERVICE_NAME;
        if (!dbusSubscribed) {
            subscribeToStatus();
        }
    } else {
        qDebug() << "DBus service not found or not registered:" << DBUS_SERVICE_NAME;
    }
}

void ClevoMonitor::subscribeToStatus()
{
    QDBusConnection connection = QDBusConnection::systemBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "SubscribeStatus"
    );
    
    QDBusReply<QString> reply = connection.call(msg, QDBus::BlockWithGui, 1000);
    if (reply.isValid()) {
        qDebug() << "Subscribed to status updates:" << reply.value();
        dbusSubscribed = true;
    } else {
        qDebug() << "Failed to subscribe to status updates:" << reply.error().message();
    }
}

void ClevoMonitor::unsubscribeFromStatus()
{
    QDBusConnection connection = QDBusConnection::systemBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "UnsubscribeStatus"
    );
    
    QDBusReply<QString> reply = connection.call(msg, QDBus::BlockWithGui, 1000);
    if (reply.isValid()) {
        qDebug() << "Unsubscribed from status updates:" << reply.value();
        dbusSubscribed = false;
    } else {
        qDebug() << "Failed to unsubscribe from status updates:" << reply.error().message();
    }
}

void ClevoMonitor::updateStatus()
{
    if (!dbusConnected) {
        connectToDaemonDBus();
        // Don't mark data invalid immediately; allow a few retries to avoid flapping
    }
    
    QDBusConnection connection = QDBusConnection::systemBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "GetStatus"
    );
    
    QDBusReply<QString> reply = connection.call(msg, QDBus::BlockWithGui, 1000);
    if (reply.isValid()) {
        QString response = reply.value();
        parseStatusResponse(response);
        dataValid = true;
        update();
    } else {
        qDebug() << "Failed to get status:" << reply.error().message();
        // Soft failure: don't immediately drop connection/subscription
        // Allow signal-driven updates to continue if subscription is active
    }
}

void ClevoMonitor::parseStatusResponse(const QString &response)
{
    // Parse response format: "CPU:XX FAN_DUTY:XX FAN_RPM:XX AUTO:XX"
    QStringList parts = response.split(" ");
    for (const QString &part : parts) {
        if (part.startsWith("CPU:")) {
            cpuTemp = part.mid(4).toInt();
        } else if (part.startsWith("FAN_DUTY:")) {
            fanDuty = part.mid(10).toInt();
        } else if (part.startsWith("FAN_RPM:")) {
            fanRpm = part.mid(8).toInt();
        } else if (part.startsWith("AUTO:")) {
            autoMode = (part.mid(5).toInt() != 0);
        }
    }
}

void ClevoMonitor::sendCommandDBus(const QString &command)
{
    if (!dbusConnected) {
        qDebug() << "Not connected to daemon";
        return;
    }
    
    QDBusConnection connection = QDBusConnection::systemBus();
    QDBusMessage msg;
    
    if (command.startsWith("SET_FAN")) {
        int duty = command.mid(8).toInt();
        msg = QDBusMessage::createMethodCall(
            DBUS_SERVICE_NAME,
            DBUS_OBJECT_PATH,
            DBUS_INTERFACE,
            "SetFanDuty"
        );
        msg << duty;
    } else if (command == "SET_AUTO") {
        msg = QDBusMessage::createMethodCall(
            DBUS_SERVICE_NAME,
            DBUS_OBJECT_PATH,
            DBUS_INTERFACE,
            "SetAutoMode"
        );
        msg << true;
    } else {
        qDebug() << "Unknown command:" << command;
        return;
    }
    
    QDBusReply<QString> reply = connection.call(msg, QDBus::BlockWithGui, 1000);
    if (reply.isValid()) {
        qDebug() << "Command response:" << reply.value();
    } else {
        qDebug() << "Failed to send command:" << reply.error().message();
    }
}

void ClevoMonitor::reconnectToDaemon()
{
    dbusConnected = false;
    dbusSubscribed = false;
    // Don't clear data to avoid UI flicker
    connectToDaemonDBus();
}

void ClevoMonitor::toggleTransparency()
{
    isTransparent = !isTransparent;
    setWindowOpacity(isTransparent ? 0.3 : windowOpacity);
}

void ClevoMonitor::openSettings()
{
    if (!settingsDialog) {
        settingsDialog = new ClevoSettingsDialog(this);
    }
    settingsDialog->show();
}

void ClevoMonitor::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Set background
    QColor bgColor = isTransparent ? QColor(0, 0, 0, 100) : QColor(0, 0, 0, 200);
    painter.fillRect(rect(), bgColor);
    
    if (!dataValid) {
        // Show connection error
        painter.setPen(Qt::red);
        painter.setFont(QFont("Arial", 10));
        painter.drawText(rect(), Qt::AlignCenter, "No Connection");
        return;
    }
    
    // Draw temperature
    QColor tempColor = getTemperatureColor(cpuTemp);
    painter.setPen(tempColor);
    painter.setFont(QFont("Arial", 12, QFont::Bold));
    QString tempText = QString("CPU: %1°C").arg(cpuTemp);
    painter.drawText(rect().adjusted(10, 10, -10, -50), Qt::AlignLeft, tempText);
    
    // Draw fan info
    QColor fanColor = getFanRpmColor(fanRpm, fanDuty);
    painter.setPen(fanColor);
    painter.setFont(QFont("Arial", 10));
    QString fanText = QString("Fan: %1").arg(fanDuty) + QLatin1Char('%') +
                      QString(" (%1 RPM)").arg(fanRpm);
    painter.drawText(rect().adjusted(10, 40, -10, -20), Qt::AlignLeft, fanText);
    
    // Draw mode
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 8));
    QString modeText = autoMode ? "AUTO" : "MANUAL";
    painter.drawText(rect().adjusted(10, 70, -10, -10), Qt::AlignLeft, modeText);
    
    // Draw status indicator
    QString statusText = getTemperatureStatus(cpuTemp);
    painter.setPen(tempColor);
    painter.setFont(QFont("Arial", 8));
    painter.drawText(rect().adjusted(10, 85, -10, -5), Qt::AlignLeft, statusText);
}

void ClevoMonitor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        isDragging = true;
        dragStartPos = event->pos();
    } else if (event->button() == Qt::RightButton) {
        if (!contextMenu) {
            setupContextMenu();
        }
        contextMenu->exec(event->globalPos());
    }
}

void ClevoMonitor::mouseMoveEvent(QMouseEvent *event)
{
    if (isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPos() - dragStartPos);
    }
}

void ClevoMonitor::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        isDragging = false;
    }
}

void ClevoMonitor::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Escape:
            close();
            break;
        case Qt::Key_T:
            toggleTransparency();
            break;
        case Qt::Key_S:
            openSettings();
            break;
        case Qt::Key_R:
            reconnectToDaemon();
            break;
    }
}

QColor ClevoMonitor::getTemperatureColor(int temp)
{
    if (temp >= 80) return Qt::red;
    if (temp >= 70) return Qt::yellow;
    if (temp >= 60) return Qt::cyan;
    return Qt::green;
}

QColor ClevoMonitor::getFanRpmColor(int rpm, int duty)
{
    if (rpm < 1000 && duty > 20) return Qt::red;
    if (rpm < 2000) return Qt::yellow;
    return Qt::green;
}

QString ClevoMonitor::getTemperatureStatus(int temp)
{
    if (temp >= 80) return "CRITICAL";
    if (temp >= 70) return "HIGH";
    if (temp >= 60) return "WARM";
    return "NORMAL";
}

void ClevoMonitor::closeEvent(QCloseEvent *event)
{
    unsubscribeFromStatus();
    QWidget::closeEvent(event);
}

void ClevoMonitor::showContextMenu()
{
    if (!contextMenu) {
        setupContextMenu();
    }
    contextMenu->popup(QCursor::pos());
}

void ClevoMonitor::quitApplication()
{
    QApplication::quit();
}

void ClevoMonitor::toggleCharts()
{
    // Charts not implemented in DBus version
    // This is a stub to satisfy the interface
}

void ClevoMonitor::cycleDisplayLayout()
{
    // Display layout cycling not implemented in DBus version
    // This is a stub to satisfy the interface
}

void ClevoMonitor::contextMenuEvent(QContextMenuEvent *event)
{
    if (!contextMenu) {
        setupContextMenu();
    }
    contextMenu->exec(event->globalPos());
}

void ClevoMonitor::onStatusChanged(int newCpuTemp, int newFanDuty, int newFanRpm, bool newAutoMode)
{
    cpuTemp = newCpuTemp;
    fanDuty = newFanDuty;
    fanRpm = newFanRpm;
    autoMode = newAutoMode;
    dataValid = true;
    dbusConnected = true;
    if (!dbusSubscribed) {
        // Try resubscribe once if we got a signal but think we're unsubscribed
        subscribeToStatus();
    }
    update();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    ClevoMonitor monitor;
    monitor.show();
    
    return app.exec();
} 