#ifndef CLEVO_GUI_H
#define CLEVO_GUI_H

#include <QWidget>
#include <QTimer>
#include <QSocketNotifier>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QFont>
#include <QColor>
#include <QPoint>
#include <QVector>
#include <QRect>
#include <QCloseEvent>

// Forward declaration
class ClevoSettingsDialog;

class ClevoMonitor : public QWidget
{
    Q_OBJECT

public:
    explicit ClevoMonitor(QWidget *parent = nullptr);
    ~ClevoMonitor();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void updateStatus();
    void toggleTransparency();
    void showContextMenu();
    void quitApplication();
    void reconnectToDaemon();
    void openSettings();
    void toggleCharts(); // New slot for chart toggle
    void cycleDisplayLayout();
    // DBus signal handler
    void onStatusChanged(int cpuTemp, int fanDuty, int fanRpm, bool autoMode);
    void onStatusChangedMap(const QVariantMap &m);

private:
    // Window properties
    void setupWindow();
    void setupTimer();
    void setupSocket();
    void setupContextMenu();
    void setupDBus(); // Added for DBus implementation
    
    // Display methods
    void drawBackground(QPainter &painter);
    void drawTemperature(QPainter &painter);
    void drawFanInfo(QPainter &painter);
    void drawModeInfo(QPainter &painter);
    void drawStatusBar(QPainter &painter);
    void drawMiniBar(QPainter &painter);
    
    // Layout helpers
    enum class DisplayLayout { Compact, Detailed, MiniBar };
    void setDisplayLayout(DisplayLayout layout);
    
    // Chart methods (new)
    void drawSparklines(QPainter &painter);
    void drawSparkline(QPainter &painter, const QVector<int> &data, 
                      const QRect &rect, const QColor &color, const QString &label);
    void updateChartData();
    
    // Color helpers
    QColor getTemperatureColor(int temp);
    QColor getFanRpmColor(int rpm, int duty);
    QString getTemperatureStatus(int temp);
    
    // Socket communication
    bool connectToDaemon();
    bool sendCommand(const QString &command);
    bool receiveResponse(QString &response);
    void parseStatusResponse(const QString &response);
    
    // DBus methods (added for DBus implementation)
    void connectToDaemonDBus(); // Renamed to avoid conflict
    void sendCommandDBus(const QString &command); // Renamed to avoid conflict
    void subscribeToStatus();
    void unsubscribeFromStatus();

    // UI components
    QTimer *updateTimer;
    QSocketNotifier *socketNotifier;
    QMenu *contextMenu;
    ClevoSettingsDialog *settingsDialog;
    
    // Socket
    int daemonSocket;
    bool socketConnected;
    
    // DBus connection (added for DBus implementation)
    bool dbusConnected;
    bool dbusSubscribed;
    
    // Display data
    int cpuTemp;
    int fanDuty;
    int fanRpm;
    bool autoMode;
    bool dataValid;
    
    // Window state
    bool isTransparent;
    QPoint dragStartPos;
    bool isDragging;
    
    // Configuration
    int updateInterval;  // milliseconds
    double windowOpacity;
    DisplayLayout displayLayout;
    
    // Display cache for optimization
    int lastDisplayCpuTemp;
    int lastDisplayFanDuty;
    int lastDisplayFanRpm;
    bool lastDisplayAutoMode;
    
    // Chart data (new)
    static const int CHART_HISTORY_SIZE = 60; // 1 second at 60FPS
    QVector<int> tempHistory;
    QVector<int> fanRpmHistory;
    QVector<int> fanDutyHistory;
    int chartDataIndex;
    bool chartsEnabled;
    bool chartsVisible;
    
    // Connection health tracking
    qint64 lastSuccessfulResponse;  // Track last successful response time
    int connectionHealthWindow;      // Health window in milliseconds
    bool wasConnected;              // Track previous connection state for logging
    bool wasDisconnected;           // Track previous disconnection state for logging
};

#endif // CLEVO_GUI_H 