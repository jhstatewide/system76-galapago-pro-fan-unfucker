#include "clevo-gui.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QDebug>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

#define SOCKET_PATH "/run/clevo-daemon.sock"
#define BUFFER_SIZE 1024

ClevoMonitor::ClevoMonitor(QWidget *parent)
    : QWidget(parent)
    , updateTimer(nullptr)
    , socketNotifier(nullptr)
    , contextMenu(nullptr)
    , daemonSocket(-1)
    , socketConnected(false)
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
    setupSocket();
    connectToDaemon();
}

ClevoMonitor::~ClevoMonitor()
{
    if (daemonSocket >= 0) {
        ::close(daemonSocket);
    }
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
    setWindowTitle("Clevo Fan Monitor");
    
    // Enable mouse tracking for dragging
    setMouseTracking(true);
}

void ClevoMonitor::setupTimer()
{
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &ClevoMonitor::updateStatus);
    updateTimer->start(updateInterval);
}

void ClevoMonitor::setupSocket()
{
    daemonSocket = -1;
    socketConnected = false;
}

void ClevoMonitor::setupContextMenu()
{
    contextMenu = new QMenu(this);
    
    QAction *transparencyAction = new QAction("Toggle Transparency", this);
    connect(transparencyAction, &QAction::triggered, this, &ClevoMonitor::toggleTransparency);
    contextMenu->addAction(transparencyAction);
    
    contextMenu->addSeparator();
    
    QAction *reconnectAction = new QAction("Reconnect to Daemon", this);
    connect(reconnectAction, &QAction::triggered, this, &ClevoMonitor::reconnectToDaemon);
    contextMenu->addAction(reconnectAction);
    
    contextMenu->addSeparator();
    
    QAction *quitAction = new QAction("Quit", this);
    connect(quitAction, &QAction::triggered, this, &ClevoMonitor::quitApplication);
    contextMenu->addAction(quitAction);
}

bool ClevoMonitor::connectToDaemon()
{
    if (daemonSocket >= 0) {
        ::close(daemonSocket);
    }
    
    daemonSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (daemonSocket < 0) {
        qDebug() << "Failed to create socket:" << strerror(errno);
        return false;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (::connect(daemonSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        qDebug() << "Failed to connect to daemon:" << strerror(errno);
        ::close(daemonSocket);
        daemonSocket = -1;
        return false;
    }
    
    socketConnected = true;
    qDebug() << "Connected to clevo-daemon";
    return true;
}

bool ClevoMonitor::sendCommand(const QString &command)
{
    if (daemonSocket < 0) {
        return false;
    }
    
    QByteArray data = command.toUtf8();
    ssize_t sent = send(daemonSocket, data.constData(), data.size(), MSG_NOSIGNAL);
    
    if (sent < 0) {
        if (errno == EPIPE) {
            socketConnected = false;
            qDebug() << "Connection to daemon lost";
        } else {
            qDebug() << "Failed to send command:" << strerror(errno);
        }
        return false;
    }
    
    return true;
}

bool ClevoMonitor::receiveResponse(QString &response)
{
    if (daemonSocket < 0) {
        return false;
    }
    
    char buffer[BUFFER_SIZE];
    ssize_t received = recv(daemonSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (received < 0) {
        if (errno == EINTR) {
            return false;  // Interrupted, try again later
        }
        qDebug() << "Failed to receive response:" << strerror(errno);
        return false;
    }
    
    buffer[received] = '\0';
    response = QString::fromUtf8(buffer);
    return true;
}

void ClevoMonitor::parseStatusResponse(const QString &response)
{
    // Parse format: "CPU:XX FAN_DUTY:XX FAN_RPM:XX AUTO:XX"
    int temp, duty, rpm, auto_val;
    
    if (sscanf(response.toUtf8().constData(), "CPU:%d FAN_DUTY:%d FAN_RPM:%d AUTO:%d",
               &temp, &duty, &rpm, &auto_val) == 4) {
        cpuTemp = temp;
        fanDuty = duty;
        fanRpm = rpm;
        autoMode = (auto_val != 0);
        dataValid = true;
    } else {
        dataValid = false;
        qDebug() << "Failed to parse response:" << response;
    }
}

void ClevoMonitor::updateStatus()
{
    if (!socketConnected) {
        if (!connectToDaemon()) {
            dataValid = false;
            update();  // Redraw with error state
            return;
        }
    }
    
    if (!sendCommand("STATUS")) {
        socketConnected = false;
        dataValid = false;
        update();
        return;
    }
    
    QString response;
    if (receiveResponse(response)) {
        parseStatusResponse(response);
        update();  // Trigger redraw
    } else {
        socketConnected = false;
        dataValid = false;
        update();
    }
}

void ClevoMonitor::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw background
    drawBackground(painter);
    
    if (dataValid) {
        // Draw status information
        drawTemperature(painter);
        drawFanInfo(painter);
        drawModeInfo(painter);
    } else {
        // Draw error state
        painter.setPen(Qt::red);
        painter.setFont(QFont("Arial", 10));
        painter.drawText(rect(), Qt::AlignCenter, "No Connection\nCheck clevo-daemon");
    }
    
    // Draw status bar
    drawStatusBar(painter);
}

void ClevoMonitor::drawBackground(QPainter &painter)
{
    // Semi-transparent dark background
    QColor bgColor(0, 0, 0, isTransparent ? 120 : 180);
    painter.fillRect(rect(), bgColor);
    
    // Draw border
    painter.setPen(QPen(Qt::white, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void ClevoMonitor::drawTemperature(QPainter &painter)
{
    QColor tempColor = getTemperatureColor(cpuTemp);
    QString status = getTemperatureStatus(cpuTemp);
    
    painter.setPen(tempColor);
    painter.setFont(QFont("Arial", 12, QFont::Bold));
    
    QString text = QString("CPU: %1°C [%2]").arg(cpuTemp).arg(status);
    painter.drawText(10, 25, text);
}

void ClevoMonitor::drawFanInfo(QPainter &painter)
{
    QColor rpmColor = getFanRpmColor(fanRpm, fanDuty);
    
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 10));
    
    QString dutyText = QString("Fan: %1%").arg(fanDuty);
    painter.drawText(10, 45, dutyText);
    
    painter.setPen(rpmColor);
    QString rpmText = QString("RPM: %1").arg(fanRpm);
    painter.drawText(80, 45, rpmText);
}

void ClevoMonitor::drawModeInfo(QPainter &painter)
{
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 10));
    
    QString modeText = QString("Mode: %1").arg(autoMode ? "Auto" : "Manual");
    painter.drawText(10, 65, modeText);
}

void ClevoMonitor::drawStatusBar(QPainter &painter)
{
    painter.setPen(Qt::gray);
    painter.setFont(QFont("Arial", 8));
    
    QString statusText = socketConnected ? "Connected" : "Disconnected";
    painter.drawText(10, height() - 5, statusText);
}

QColor ClevoMonitor::getTemperatureColor(int temp)
{
    if (temp >= 80) return Qt::red;
    if (temp >= 70) return Qt::yellow;
    if (temp >= 60) return QColor(0, 255, 255);  // Cyan
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

void ClevoMonitor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (event->modifiers() & Qt::ControlModifier) {
            // Ctrl+click to toggle transparency
            toggleTransparency();
        } else {
            // Start dragging
            dragStartPos = event->pos();
            isDragging = true;
        }
    } else if (event->button() == Qt::RightButton) {
        showContextMenu();
    }
}

void ClevoMonitor::mouseMoveEvent(QMouseEvent *event)
{
    if (isDragging && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->pos() - dragStartPos;
        move(pos() + delta);
    }
}

void ClevoMonitor::contextMenuEvent(QContextMenuEvent *event)
{
    if (!contextMenu) {
        setupContextMenu();
    }
    contextMenu->exec(event->globalPos());
}

void ClevoMonitor::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Q:
            if (event->modifiers() & Qt::ControlModifier) {
                quitApplication();
            }
            break;
        case Qt::Key_R:
            updateStatus();
            break;
        case Qt::Key_T:
            toggleTransparency();
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

void ClevoMonitor::toggleTransparency()
{
    isTransparent = !isTransparent;
    setWindowOpacity(isTransparent ? 0.6 : windowOpacity);
    update();
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

void ClevoMonitor::reconnectToDaemon()
{
    connectToDaemon();
    updateStatus();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    ClevoMonitor monitor;
    monitor.show();
    
    return app.exec();
} 