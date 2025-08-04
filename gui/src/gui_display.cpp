#include "gui_display.h"
#include <QFont>
#include <QPen>

namespace GuiDisplay {

QColor getTemperatureColor(int temp)
{
    if (temp >= 80) return Qt::red;
    if (temp >= 70) return Qt::yellow;
    if (temp >= 60) return QColor(0, 255, 255);  // Cyan
    return Qt::green;
}

QColor getFanRpmColor(int rpm, int duty)
{
    if (rpm < 1000 && duty > 20) return Qt::red;
    if (rpm < 2000) return Qt::yellow;
    return Qt::green;
}

QColor getStatusColor(bool connected)
{
    return connected ? Qt::green : Qt::red;
}

QString getTemperatureStatus(int temp)
{
    if (temp >= 80) return "CRITICAL";
    if (temp >= 70) return "HIGH";
    if (temp >= 60) return "WARM";
    return "NORMAL";
}

QString getFanHealthStatus(int rpm, int duty)
{
    if (rpm < 1000 && duty > 20) return "LOW";
    if (rpm < 2000) return "WARN";
    return "OK";
}

void drawCompactLayout(QPainter &painter, int cpuTemp, int fanDuty, 
                      int fanRpm, bool autoMode, bool connected)
{
    if (!connected) {
        painter.setPen(Qt::red);
        painter.setFont(QFont("Arial", 10));
        painter.drawText(painter.viewport(), Qt::AlignCenter, "No Connection");
        return;
    }
    
    // Temperature
    QColor tempColor = getTemperatureColor(cpuTemp);
    QString tempStatus = getTemperatureStatus(cpuTemp);
    
    painter.setPen(tempColor);
    painter.setFont(QFont("Arial", 12, QFont::Bold));
    QString tempText = QString("CPU: %1°C [%2]").arg(cpuTemp).arg(tempStatus);
    painter.drawText(10, 25, tempText);
    
    // Fan info
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 10));
    QString dutyText = QString("Fan: %1%").arg(fanDuty);
    painter.drawText(10, 45, dutyText);
    
    QColor rpmColor = getFanRpmColor(fanRpm, fanDuty);
    painter.setPen(rpmColor);
    QString rpmText = QString("RPM: %1").arg(fanRpm);
    painter.drawText(80, 45, rpmText);
    
    // Mode
    painter.setPen(Qt::white);
    QString modeText = QString("Mode: %1").arg(autoMode ? "Auto" : "Manual");
    painter.drawText(10, 65, modeText);
}

void drawDetailedLayout(QPainter &painter, int cpuTemp, int fanDuty,
                       int fanRpm, bool autoMode, bool connected)
{
    // For future implementation - more detailed display
    drawCompactLayout(painter, cpuTemp, fanDuty, fanRpm, autoMode, connected);
}

void drawTransparentBackground(QPainter &painter, const QRect &rect, 
                             bool isTransparent)
{
    QColor bgColor(0, 0, 0, isTransparent ? 120 : 180);
    painter.fillRect(rect, bgColor);
    
    painter.setPen(QPen(Qt::white, 1));
    painter.drawRect(rect.adjusted(0, 0, -1, -1));
}

void drawStatusBar(QPainter &painter, const QRect &rect, bool connected)
{
    painter.setPen(Qt::gray);
    painter.setFont(QFont("Arial", 8));
    
    QString statusText = connected ? "Connected" : "Disconnected";
    painter.drawText(rect.left() + 10, rect.bottom() - 5, statusText);
}

} // namespace GuiDisplay 