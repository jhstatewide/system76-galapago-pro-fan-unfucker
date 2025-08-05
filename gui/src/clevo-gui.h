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
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void updateStatus();
    void toggleTransparency();
    void showContextMenu();
    void quitApplication();
    void reconnectToDaemon();
    void openSettings();
    void toggleCharts(); // New slot for chart toggle

private:
    // Window properties
    void setupWindow();
    void setupTimer();
    void setupSocket();
    void setupContextMenu();
    
    // Display methods
    void drawBackground(QPainter &painter);
    void drawTemperature(QPainter &painter);
    void drawFanInfo(QPainter &painter);
    void drawModeInfo(QPainter &painter);
    void drawStatusBar(QPainter &painter);
    
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

    // UI components
    QTimer *updateTimer;
    QSocketNotifier *socketNotifier;
    QMenu *contextMenu;
    ClevoSettingsDialog *settingsDialog;
    
    // Socket
    int daemonSocket;
    bool socketConnected;
    
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
};

#endif // CLEVO_GUI_H 