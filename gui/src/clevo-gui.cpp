#include "clevo-gui.h"
#include "clevo-settings.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QDebug>
#include "gui_config.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <QDateTime>

#define SOCKET_PATH "/run/clevo-daemon.sock"
#define BUFFER_SIZE 1024

ClevoMonitor::ClevoMonitor(QWidget *parent)
    : QWidget(parent)
    , updateTimer(nullptr)
    , socketNotifier(nullptr)
    , contextMenu(nullptr)
    , settingsDialog(nullptr)
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
    , chartDataIndex(0)
    , chartsEnabled(true)
    , chartsVisible(false)
    , lastSuccessfulResponse(0)  // Track last successful response time
    , connectionHealthWindow(5000)  // 5 second health window
    , wasConnected(false)        // Track previous connection state for logging
    , wasDisconnected(false)     // Track previous disconnection state for logging
{
    // Initialize chart data vectors
    tempHistory.resize(CHART_HISTORY_SIZE);
    fanRpmHistory.resize(CHART_HISTORY_SIZE);
    fanDutyHistory.resize(CHART_HISTORY_SIZE);
    tempHistory.fill(0);
    fanRpmHistory.fill(0);
    fanDutyHistory.fill(0);
    
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

    // Default layout from settings
    const QString layout = GuiConfig::instance().getDisplayLayout();
    if (layout.compare("compact", Qt::CaseInsensitive) == 0) {
        displayLayout = DisplayLayout::Compact;
    } else if (layout.compare("detailed", Qt::CaseInsensitive) == 0) {
        displayLayout = DisplayLayout::Detailed;
    } else if (layout.compare("mini", Qt::CaseInsensitive) == 0 ||
               layout.compare("minibar", Qt::CaseInsensitive) == 0) {
        displayLayout = DisplayLayout::MiniBar;
    } else {
        displayLayout = DisplayLayout::Compact;
    }
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
    
    QAction *settingsAction = new QAction("Settings and Commands", this);
    connect(settingsAction, &QAction::triggered, this, &ClevoMonitor::openSettings);
    contextMenu->addAction(settingsAction);
    
    contextMenu->addSeparator();

    // Display layout submenu
    QMenu *layoutMenu = new QMenu("Display Mode", contextMenu);
    QAction *compactAct = layoutMenu->addAction("Compact");
    QAction *detailedAct = layoutMenu->addAction("Detailed");
    QAction *miniBarAct = layoutMenu->addAction("Mini bar");
    connect(compactAct, &QAction::triggered, this, [this]() { setDisplayLayout(DisplayLayout::Compact); });
    connect(detailedAct, &QAction::triggered, this, [this]() { setDisplayLayout(DisplayLayout::Detailed); });
    connect(miniBarAct, &QAction::triggered, this, [this]() { setDisplayLayout(DisplayLayout::MiniBar); });
    contextMenu->addMenu(layoutMenu);
    
    contextMenu->addSeparator();
    
    QAction *chartsAction = new QAction("Toggle Charts", this);
    connect(chartsAction, &QAction::triggered, this, &ClevoMonitor::toggleCharts);
    contextMenu->addAction(chartsAction);
    
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
    // Connection logging disabled - GUI works fine without it
    wasConnected = true;
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
            // Disconnection logging disabled - GUI works fine without it
            wasConnected = false;
            wasDisconnected = true;
        } else {
            qDebug() << "Failed to send command:" << strerror(errno);
        }
        return false;
    }
    
    // Reset the disconnection flag when we successfully send
    wasConnected = true;
    wasDisconnected = false;
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
    // Check if we're within the connection health window
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    bool withinHealthWindow = (currentTime - lastSuccessfulResponse) < connectionHealthWindow;
    
    if (!socketConnected) {
        if (!connectToDaemon()) {
            // Only show disconnected if we're outside the health window
            if (!withinHealthWindow) {
                dataValid = false;
                update();  // Redraw with error state
            }
            return;
        }
    }
    
    if (!sendCommand("STATUS")) {
        socketConnected = false;
        // Only show disconnected if we're outside the health window
        if (!withinHealthWindow) {
            dataValid = false;
            update();
        }
        return;
    }
    
    QString response;
    if (receiveResponse(response)) {
        parseStatusResponse(response);
        updateChartData(); // Update chart data
        lastSuccessfulResponse = currentTime;  // Update successful response time
        update();  // Trigger redraw
    } else {
        socketConnected = false;
        // Only show disconnected if we're outside the health window
        if (!withinHealthWindow) {
            dataValid = false;
            update();
        }
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
        if (displayLayout == DisplayLayout::MiniBar) {
            drawMiniBar(painter);
        } else {
            drawTemperature(painter);
            drawFanInfo(painter);
            drawModeInfo(painter);
        }
        
        // Draw sparklines if enabled and visible
        if (chartsEnabled && chartsVisible) {
            drawSparklines(painter);
        }
    } else {
        // Draw error state
        painter.setPen(Qt::red);
        painter.setFont(QFont("Arial", 10));
        painter.drawText(rect(), Qt::AlignCenter, "No Connection\nCheck clevo-daemon");
    }
    
    // Draw status bar except in MiniBar layout
    if (displayLayout != DisplayLayout::MiniBar) {
        drawStatusBar(painter);
    }
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
        case Qt::Key_C:
            toggleCharts();
            break;
        case Qt::Key_M:
            cycleDisplayLayout();
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

void ClevoMonitor::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        isDragging = false;
    }
}

void ClevoMonitor::closeEvent(QCloseEvent *event)
{
    // Close socket connection if open
    if (daemonSocket >= 0) {
        ::close(daemonSocket);
        daemonSocket = -1;
    }
    QWidget::closeEvent(event);
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

void ClevoMonitor::openSettings()
{
    if (!settingsDialog) {
        settingsDialog = new ClevoSettingsDialog(this);
    }
    settingsDialog->show();
    settingsDialog->raise();
    settingsDialog->activateWindow();
}

// Chart methods
void ClevoMonitor::updateChartData()
{
    if (!dataValid) return;
    
    // Add current data to history
    tempHistory[chartDataIndex] = cpuTemp;
    fanRpmHistory[chartDataIndex] = fanRpm;
    fanDutyHistory[chartDataIndex] = fanDuty;
    
    // Increment index with wraparound
    chartDataIndex = (chartDataIndex + 1) % CHART_HISTORY_SIZE;
}

void ClevoMonitor::drawSparklines(QPainter &painter)
{
    // Calculate chart area (bottom portion of window)
    int chartHeight = 60;
    int chartY = height() - chartHeight - 20; // Leave space for status bar
    
    // Draw temperature sparkline
    QRect tempRect(10, chartY, 60, 15);
    drawSparkline(painter, tempHistory, tempRect, getTemperatureColor(cpuTemp), "T");
    
    // Draw fan RPM sparkline
    QRect rpmRect(80, chartY, 60, 15);
    drawSparkline(painter, fanRpmHistory, rpmRect, getFanRpmColor(fanRpm, fanDuty), "R");
    
    // Draw fan duty sparkline
    QRect dutyRect(150, chartY, 60, 15);
    drawSparkline(painter, fanDutyHistory, dutyRect, Qt::cyan, "D");
}

void ClevoMonitor::drawSparkline(QPainter &painter, const QVector<int> &data, 
                                 const QRect &rect, const QColor &color, const QString &label)
{
    if (data.isEmpty()) return;
    
    // Find min/max for scaling
    int minVal = data[0], maxVal = data[0];
    for (int val : data) {
        if (val > 0) { // Only consider valid data
            minVal = qMin(minVal, val);
            maxVal = qMax(maxVal, val);
        }
    }
    
    // Avoid division by zero
    if (maxVal == minVal) maxVal = minVal + 1;
    
    // Draw label
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 8));
    painter.drawText(rect.left(), rect.top() - 2, label);
    
    // Draw sparkline
    painter.setPen(QPen(color, 1));
    
    QVector<QPoint> points;
    points.reserve(CHART_HISTORY_SIZE);
    
    for (int i = 0; i < CHART_HISTORY_SIZE; ++i) {
        int dataIndex = (chartDataIndex + i) % CHART_HISTORY_SIZE;
        int val = data[dataIndex];
        
        if (val > 0) { // Only draw valid data points
            int x = rect.left() + (i * rect.width()) / CHART_HISTORY_SIZE;
            int y = rect.bottom() - ((val - minVal) * rect.height()) / (maxVal - minVal);
            points.append(QPoint(x, y));
        }
    }
    
    // Draw the sparkline as a polyline for maximum performance
    if (points.size() > 1) {
        painter.drawPolyline(points.data(), points.size());
    }
}

void ClevoMonitor::toggleCharts()
{
    // In MiniBar layout, charts are not shown; ignore toggle
    if (displayLayout == DisplayLayout::MiniBar) {
        chartsVisible = false;
        return;
    }
    // Resize window based on chart visibility
    if (chartsVisible) {
        resize(220, 160); // Larger window for charts
    } else {
        resize(200, 100); // Original size
    }
    
    update();
}

void ClevoMonitor::drawMiniBar(QPainter &painter)
{
    // Minimal horizontal bar: [CPU 47°C • 24% • 2736 RPM • Auto]
    // Compact vertical padding; no status bar in MiniBar
    QRect r = rect().adjusted(8, 4, -8, -4);
    QFont font("Arial", 10);
    painter.setFont(font);
    QFontMetrics fm(font);
    int baselineY = r.top() + fm.ascent();

    // Left colored segment for temperature
    QString left = QString("CPU %1°C").arg(cpuTemp);
    painter.setPen(getTemperatureColor(cpuTemp));
    painter.drawText(r.left(), baselineY, left);

    // Separator width accounts for spacing and bullet
    const QString sep = "  •  ";
    int leftWidth = fm.horizontalAdvance(left + sep);

    // Right segment text, elided to fit available width
    painter.setPen(Qt::white);
    QString right = QString("%1%  •  %2 RPM  •  %3")
                        .arg(fanDuty)
                        .arg(fanRpm)
                        .arg(autoMode ? "Auto" : "Manual");
    int availableWidth = qMax(0, r.width() - leftWidth);
    QString elidedRight = fm.elidedText(right, Qt::ElideRight, availableWidth);
    painter.drawText(r.left() + leftWidth, baselineY, elidedRight);
}

void ClevoMonitor::setDisplayLayout(DisplayLayout layout)
{
    displayLayout = layout;
    // Persist setting
    switch (displayLayout) {
        case DisplayLayout::Compact:
            GuiConfig::instance().setDisplayLayout("compact");
            // Restore typical window size for compact
            resize(200, 100);
            break;
        case DisplayLayout::Detailed:
            GuiConfig::instance().setDisplayLayout("detailed");
            // Provide a bit more space for detailed layout
            resize(240, 140);
            break;
        case DisplayLayout::MiniBar:
            GuiConfig::instance().setDisplayLayout("minibar");
            // Compact height for MiniBar; width can be adjusted by user
            resize(qMax(width(), 360), 28);
            chartsVisible = false; // No charts in MiniBar
            break;
    }
    update();
}

void ClevoMonitor::cycleDisplayLayout()
{
    switch (displayLayout) {
        case DisplayLayout::Compact:
            setDisplayLayout(DisplayLayout::Detailed);
            break;
        case DisplayLayout::Detailed:
            setDisplayLayout(DisplayLayout::MiniBar);
            break;
        case DisplayLayout::MiniBar:
            setDisplayLayout(DisplayLayout::Compact);
            break;
    }
}

// DBus slot stub for socket-based build
void ClevoMonitor::onStatusChanged(int newCpuTemp, int newFanDuty, int newFanRpm, bool newAutoMode)
{
    cpuTemp = newCpuTemp;
    fanDuty = newFanDuty;
    fanRpm = newFanRpm;
    autoMode = newAutoMode;
    dataValid = true;
    update();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    ClevoMonitor monitor;
    monitor.show();
    
    return app.exec();
} 