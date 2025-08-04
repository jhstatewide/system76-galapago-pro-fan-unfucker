#ifndef GUI_DISPLAY_H
#define GUI_DISPLAY_H

#include <QPainter>
#include <QColor>
#include <QString>

// Display utility functions for the GUI
namespace GuiDisplay {
    // Color schemes
    QColor getTemperatureColor(int temp);
    QColor getFanRpmColor(int rpm, int duty);
    QColor getStatusColor(bool connected);
    
    // Status text helpers
    QString getTemperatureStatus(int temp);
    QString getFanHealthStatus(int rpm, int duty);
    
    // Layout helpers
    void drawCompactLayout(QPainter &painter, int cpuTemp, int fanDuty, 
                          int fanRpm, bool autoMode, bool connected);
    void drawDetailedLayout(QPainter &painter, int cpuTemp, int fanDuty,
                           int fanRpm, bool autoMode, bool connected);
    
    // Background drawing
    void drawTransparentBackground(QPainter &painter, const QRect &rect, 
                                 bool isTransparent);
    void drawStatusBar(QPainter &painter, const QRect &rect, bool connected);
}

#endif // GUI_DISPLAY_H 