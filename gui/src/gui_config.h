#ifndef GUI_CONFIG_H
#define GUI_CONFIG_H

#include <QString>
#include <QSettings>

// Configuration management for the GUI
class GuiConfig {
public:
    static GuiConfig& instance();
    
    // Window settings
    int getUpdateInterval() const;
    void setUpdateInterval(int interval);
    
    double getWindowOpacity() const;
    void setWindowOpacity(double opacity);
    
    QSize getWindowSize() const;
    void setWindowSize(const QSize &size);
    
    QPoint getWindowPosition() const;
    void setWindowPosition(const QPoint &pos);
    
    bool isAlwaysOnTop() const;
    void setAlwaysOnTop(bool onTop);
    
    // Display settings
    bool isTransparent() const;
    void setTransparent(bool transparent);
    
    QString getDisplayLayout() const;
    void setDisplayLayout(const QString &layout);
    
    // Color settings
    bool isColorCodingEnabled() const;
    void setColorCodingEnabled(bool enabled);
    
    // Auto-start settings
    bool isAutoStartEnabled() const;
    void setAutoStartEnabled(bool enabled);
    
    // Save/load settings
    void saveSettings();
    void loadSettings();
    void resetToDefaults();
    
private:
    GuiConfig();
    ~GuiConfig();
    
    QSettings *settings;
    
    // Default values
    static constexpr int DEFAULT_UPDATE_INTERVAL = 1000;  // 1 second
    static constexpr double DEFAULT_WINDOW_OPACITY = 0.85;
    static constexpr int DEFAULT_WINDOW_WIDTH = 200;
    static constexpr int DEFAULT_WINDOW_HEIGHT = 100;
    static constexpr const char* DEFAULT_DISPLAY_LAYOUT = "compact";
};

#endif // GUI_CONFIG_H 