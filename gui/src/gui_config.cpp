#include "gui_config.h"
#include <QSize>
#include <QPoint>

GuiConfig& GuiConfig::instance()
{
    static GuiConfig instance;
    return instance;
}

GuiConfig::GuiConfig()
    : settings(new QSettings("clevo-gui", "clevo-gui"))
{
    loadSettings();
}

GuiConfig::~GuiConfig()
{
    saveSettings();
    delete settings;
}

int GuiConfig::getUpdateInterval() const
{
    return settings->value("updateInterval", DEFAULT_UPDATE_INTERVAL).toInt();
}

void GuiConfig::setUpdateInterval(int interval)
{
    settings->setValue("updateInterval", interval);
}

double GuiConfig::getWindowOpacity() const
{
    return settings->value("windowOpacity", DEFAULT_WINDOW_OPACITY).toDouble();
}

void GuiConfig::setWindowOpacity(double opacity)
{
    settings->setValue("windowOpacity", opacity);
}

QSize GuiConfig::getWindowSize() const
{
    return settings->value("windowSize", QSize(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT)).toSize();
}

void GuiConfig::setWindowSize(const QSize &size)
{
    settings->setValue("windowSize", size);
}

QPoint GuiConfig::getWindowPosition() const
{
    return settings->value("windowPosition", QPoint(100, 100)).toPoint();
}

void GuiConfig::setWindowPosition(const QPoint &pos)
{
    settings->setValue("windowPosition", pos);
}

bool GuiConfig::isAlwaysOnTop() const
{
    return settings->value("alwaysOnTop", true).toBool();
}

void GuiConfig::setAlwaysOnTop(bool onTop)
{
    settings->setValue("alwaysOnTop", onTop);
}

bool GuiConfig::isTransparent() const
{
    return settings->value("transparent", false).toBool();
}

void GuiConfig::setTransparent(bool transparent)
{
    settings->setValue("transparent", transparent);
}

QString GuiConfig::getDisplayLayout() const
{
    return settings->value("displayLayout", QString(DEFAULT_DISPLAY_LAYOUT)).toString();
}

void GuiConfig::setDisplayLayout(const QString &layout)
{
    settings->setValue("displayLayout", layout);
}

bool GuiConfig::isColorCodingEnabled() const
{
    return settings->value("colorCoding", true).toBool();
}

void GuiConfig::setColorCodingEnabled(bool enabled)
{
    settings->setValue("colorCoding", enabled);
}

bool GuiConfig::isAutoStartEnabled() const
{
    return settings->value("autoStart", false).toBool();
}

void GuiConfig::setAutoStartEnabled(bool enabled)
{
    settings->setValue("autoStart", enabled);
}

void GuiConfig::saveSettings()
{
    settings->sync();
}

void GuiConfig::loadSettings()
{
    // Settings are loaded automatically when accessed
}

void GuiConfig::resetToDefaults()
{
    settings->clear();
    settings->setValue("updateInterval", DEFAULT_UPDATE_INTERVAL);
    settings->setValue("windowOpacity", DEFAULT_WINDOW_OPACITY);
    settings->setValue("windowSize", QSize(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT));
    settings->setValue("windowPosition", QPoint(100, 100));
    settings->setValue("alwaysOnTop", true);
    settings->setValue("transparent", false);
    settings->setValue("displayLayout", QString(DEFAULT_DISPLAY_LAYOUT));
    settings->setValue("colorCoding", true);
    settings->setValue("autoStart", false);
} 