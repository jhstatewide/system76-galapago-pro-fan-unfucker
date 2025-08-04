#ifndef GUI_SOCKET_H
#define GUI_SOCKET_H

#include <QString>
#include <QObject>
#include <QTimer>

// Socket communication with clevo-daemon
class GuiSocket : public QObject {
    Q_OBJECT
    
public:
    explicit GuiSocket(QObject *parent = nullptr);
    ~GuiSocket();
    
    // Connection management
    bool connectToDaemon();
    void disconnectFromDaemon();
    bool isConnected() const;
    
    // Command sending
    bool sendCommand(const QString &command);
    bool sendStatusRequest();
    
    // Response handling
    QString getLastResponse() const;
    bool hasValidData() const;
    
    // Data parsing
    bool parseStatusResponse(const QString &response);
    
    // Getters for parsed data
    int getCpuTemperature() const;
    int getFanDuty() const;
    int getFanRpm() const;
    bool isAutoMode() const;
    
    // Error handling
    QString getLastError() const;
    void clearError();
    
signals:
    void connected();
    void disconnected();
    void dataUpdated();
    void error(const QString &error);
    
private slots:
    void checkConnection();
    void handleSocketError();
    
private:
    // Socket management
    bool createSocket();
    bool connectSocket();
    void closeSocket();
    
    // Data parsing
    void parseResponse(const QString &response);
    
    // Socket file descriptor
    int socketFd;
    bool socketConnected;
    
    // Parsed data
    int cpuTemp;
    int fanDuty;
    int fanRpm;
    bool autoMode;
    bool dataValid;
    
    // Error handling
    QString lastError;
    QString lastResponse;
    
    // Connection monitoring
    QTimer *connectionTimer;
    
    // Constants
    static const char* SOCKET_PATH;
    static const int BUFFER_SIZE = 1024;
    static const int CONNECTION_TIMEOUT = 5000;  // 5 seconds
};

#endif // GUI_SOCKET_H 