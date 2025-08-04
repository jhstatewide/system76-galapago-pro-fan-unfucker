#include "gui_socket.h"
#include <QDebug>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

const char* GuiSocket::SOCKET_PATH = "/run/clevo-daemon.sock";

GuiSocket::GuiSocket(QObject *parent)
    : QObject(parent)
    , socketFd(-1)
    , socketConnected(false)
    , cpuTemp(0)
    , fanDuty(0)
    , fanRpm(0)
    , autoMode(false)
    , dataValid(false)
    , connectionTimer(new QTimer(this))
{
    connect(connectionTimer, &QTimer::timeout, this, &GuiSocket::checkConnection);
    connectionTimer->start(CONNECTION_TIMEOUT);
}

GuiSocket::~GuiSocket()
{
    closeSocket();
}

bool GuiSocket::connectToDaemon()
{
    if (socketConnected) {
        return true;
    }
    
    if (!createSocket()) {
        return false;
    }
    
    if (!connectSocket()) {
        closeSocket();
        return false;
    }
    
    socketConnected = true;
    emit connected();
    return true;
}

void GuiSocket::disconnectFromDaemon()
{
    if (socketConnected) {
        closeSocket();
        socketConnected = false;
        dataValid = false;
        emit disconnected();
    }
}

bool GuiSocket::isConnected() const
{
    return socketConnected;
}

bool GuiSocket::sendCommand(const QString &command)
{
    if (!socketConnected || socketFd < 0) {
        return false;
    }
    
    QByteArray data = command.toUtf8();
    ssize_t sent = send(socketFd, data.constData(), data.size(), MSG_NOSIGNAL);
    
    if (sent < 0) {
        if (errno == EPIPE) {
            socketConnected = false;
            emit disconnected();
        } else {
            lastError = QString("Failed to send command: %1").arg(strerror(errno));
            emit error(lastError);
        }
        return false;
    }
    
    return true;
}

bool GuiSocket::sendStatusRequest()
{
    return sendCommand("STATUS");
}

QString GuiSocket::getLastResponse() const
{
    return lastResponse;
}

bool GuiSocket::hasValidData() const
{
    return dataValid;
}

bool GuiSocket::parseStatusResponse(const QString &response)
{
    int temp, duty, rpm, auto_val;
    
    if (sscanf(response.toUtf8().constData(), "CPU:%d FAN_DUTY:%d FAN_RPM:%d AUTO:%d",
               &temp, &duty, &rpm, &auto_val) == 4) {
        cpuTemp = temp;
        fanDuty = duty;
        fanRpm = rpm;
        autoMode = (auto_val != 0);
        dataValid = true;
        emit dataUpdated();
        return true;
    }
    
    dataValid = false;
    lastError = "Failed to parse response";
    emit error(lastError);
    return false;
}

int GuiSocket::getCpuTemperature() const
{
    return cpuTemp;
}

int GuiSocket::getFanDuty() const
{
    return fanDuty;
}

int GuiSocket::getFanRpm() const
{
    return fanRpm;
}

bool GuiSocket::isAutoMode() const
{
    return autoMode;
}

QString GuiSocket::getLastError() const
{
    return lastError;
}

void GuiSocket::clearError()
{
    lastError.clear();
}

void GuiSocket::checkConnection()
{
    if (!socketConnected) {
        connectToDaemon();
    }
}

void GuiSocket::handleSocketError()
{
    if (socketConnected) {
        socketConnected = false;
        dataValid = false;
        emit disconnected();
    }
}

bool GuiSocket::createSocket()
{
    socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFd < 0) {
        lastError = QString("Failed to create socket: %1").arg(strerror(errno));
        emit error(lastError);
        return false;
    }
    return true;
}

bool GuiSocket::connectSocket()
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (::connect(socketFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        lastError = QString("Failed to connect to daemon: %1").arg(strerror(errno));
        emit error(lastError);
        return false;
    }
    
    return true;
}

void GuiSocket::closeSocket()
{
    if (socketFd >= 0) {
        close(socketFd);
        socketFd = -1;
    }
}

void GuiSocket::parseResponse(const QString &response)
{
    lastResponse = response;
    parseStatusResponse(response);
} 