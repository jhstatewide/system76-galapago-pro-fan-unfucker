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
};

#endif // CLEVO_GUI_H 