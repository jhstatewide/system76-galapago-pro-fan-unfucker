#include "clevo-settings.h"
#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QTime>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

const char* ClevoSettingsDialog::SOCKET_PATH = "/run/clevo-daemon.sock";

ClevoSettingsDialog::ClevoSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , tabWidget(nullptr)
    , settingsTab(nullptr)
    , commandsTab(nullptr)
    , connectionTab(nullptr)
    , daemonSocket(-1)
    , socketConnected(false)
    , statusTimer(new QTimer(this))
{
    setWindowTitle("Clevo Fan Control Settings");
    setModal(true);
    resize(600, 500);
    
    setupUI();
    setupSettingsTab();
    setupCommandsTab();
    setupConnectionTab();
    
    // Set up status timer
    connect(statusTimer, &QTimer::timeout, this, &ClevoSettingsDialog::updateConnectionStatus);
    statusTimer->start(1000); // Update every second
    
    // Initial connection attempt
    connectToDaemon();
}

ClevoSettingsDialog::~ClevoSettingsDialog()
{
    if (daemonSocket >= 0) {
        ::close(daemonSocket);
    }
}

void ClevoSettingsDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget);
    
    // Add tabs
    settingsTab = new QWidget();
    commandsTab = new QWidget();
    connectionTab = new QWidget();
    
    tabWidget->addTab(settingsTab, "Settings");
    tabWidget->addTab(commandsTab, "Commands");
    tabWidget->addTab(connectionTab, "Connection");
}

void ClevoSettingsDialog::setupSettingsTab()
{
    QVBoxLayout *layout = new QVBoxLayout(settingsTab);
    
    // Update Interval Group
    QGroupBox *updateGroup = new QGroupBox("Update Interval", settingsTab);
    QHBoxLayout *updateLayout = new QHBoxLayout(updateGroup);
    updateIntervalSpinBox = new QSpinBox(updateGroup);
    updateIntervalSpinBox->setRange(500, 10000);
    updateIntervalSpinBox->setValue(1000);
    updateIntervalSpinBox->setSuffix(" ms");
    updateLayout->addWidget(new QLabel("Interval:"));
    updateLayout->addWidget(updateIntervalSpinBox);
    updateLayout->addStretch();
    layout->addWidget(updateGroup);
    
    // Window Settings Group
    QGroupBox *windowGroup = new QGroupBox("Window Settings", settingsTab);
    QVBoxLayout *windowLayout = new QVBoxLayout(windowGroup);
    
    // Opacity slider
    QHBoxLayout *opacityLayout = new QHBoxLayout();
    opacitySlider = new QSlider(Qt::Horizontal, windowGroup);
    opacitySlider->setRange(10, 100);
    opacitySlider->setValue(85);
    opacityLayout->addWidget(new QLabel("Opacity:"));
    opacityLayout->addWidget(opacitySlider);
    opacityLayout->addWidget(new QLabel("85%"));
    windowLayout->addLayout(opacityLayout);
    
    // Checkboxes
    transparencyCheckBox = new QCheckBox("Enable Transparency", windowGroup);
    alwaysOnTopCheckBox = new QCheckBox("Always on Top", windowGroup);
    colorCodingCheckBox = new QCheckBox("Enable Color Coding", windowGroup);
    autoStartCheckBox = new QCheckBox("Auto-start with System", windowGroup);
    
    windowLayout->addWidget(transparencyCheckBox);
    windowLayout->addWidget(alwaysOnTopCheckBox);
    windowLayout->addWidget(colorCodingCheckBox);
    windowLayout->addWidget(autoStartCheckBox);
    
    layout->addWidget(windowGroup);
    layout->addStretch();
    
    // Connect signals
    connect(updateIntervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ClevoSettingsDialog::updateIntervalChanged);
    connect(opacitySlider, &QSlider::valueChanged,
            this, &ClevoSettingsDialog::windowOpacityChanged);
    connect(transparencyCheckBox, &QCheckBox::toggled,
            this, &ClevoSettingsDialog::transparencyToggled);
    connect(alwaysOnTopCheckBox, &QCheckBox::toggled,
            this, &ClevoSettingsDialog::alwaysOnTopToggled);
    connect(colorCodingCheckBox, &QCheckBox::toggled,
            this, &ClevoSettingsDialog::colorCodingToggled);
    connect(autoStartCheckBox, &QCheckBox::toggled,
            this, &ClevoSettingsDialog::autoStartToggled);
}

void ClevoSettingsDialog::setupCommandsTab()
{
    QVBoxLayout *layout = new QVBoxLayout(commandsTab);
    
    // Fan Control Group
    QGroupBox *fanGroup = new QGroupBox("Fan Control", commandsTab);
    QGridLayout *fanLayout = new QGridLayout(fanGroup);
    
    fanDutySpinBox = new QSpinBox(fanGroup);
    fanDutySpinBox->setRange(1, 100);
    fanDutySpinBox->setValue(50);
    fanDutySpinBox->setSuffix("%");
    sendFanButton = new QPushButton("Set Fan Duty", fanGroup);
    
    fanLayout->addWidget(new QLabel("Fan Duty:"), 0, 0);
    fanLayout->addWidget(fanDutySpinBox, 0, 1);
    fanLayout->addWidget(sendFanButton, 0, 2);
    
    sendAutoButton = new QPushButton("Enable Auto Mode", fanGroup);
    fanLayout->addWidget(sendAutoButton, 1, 0, 1, 3);
    
    layout->addWidget(fanGroup);
    
    // Temperature Control Group
    QGroupBox *tempGroup = new QGroupBox("Temperature Control", commandsTab);
    QGridLayout *tempLayout = new QGridLayout(tempGroup);
    
    targetTempSpinBox = new QSpinBox(tempGroup);
    targetTempSpinBox->setRange(40, 100);
    targetTempSpinBox->setValue(70);
    targetTempSpinBox->setSuffix("°C");
    sendTargetTempButton = new QPushButton("Set Target Temp", tempGroup);
    
    tempLayout->addWidget(new QLabel("Target Temperature:"), 0, 0);
    tempLayout->addWidget(targetTempSpinBox, 0, 1);
    tempLayout->addWidget(sendTargetTempButton, 0, 2);
    
    layout->addWidget(tempGroup);
    
    // PID Control Group
    QGroupBox *pidGroup = new QGroupBox("PID Control Settings", commandsTab);
    QGridLayout *pidLayout = new QGridLayout(pidGroup);
    
    maxDutyChangeSpinBox = new QSpinBox(pidGroup);
    maxDutyChangeSpinBox->setRange(1, 100);
    maxDutyChangeSpinBox->setValue(30);
    maxDutyChangeSpinBox->setSuffix("%");
    sendMaxDutyChangeButton = new QPushButton("Set Max Change", pidGroup);
    
    maxIncreaseSpinBox = new QSpinBox(pidGroup);
    maxIncreaseSpinBox->setRange(1, 100);
    maxIncreaseSpinBox->setValue(30);
    maxIncreaseSpinBox->setSuffix("%");
    sendMaxIncreaseButton = new QPushButton("Set Max Increase", pidGroup);
    
    maxDecreaseSpinBox = new QSpinBox(pidGroup);
    maxDecreaseSpinBox->setRange(1, 100);
    maxDecreaseSpinBox->setValue(30);
    maxDecreaseSpinBox->setSuffix("%");
    sendMaxDecreaseButton = new QPushButton("Set Max Decrease", pidGroup);
    
    pidLayout->addWidget(new QLabel("Max Duty Change:"), 0, 0);
    pidLayout->addWidget(maxDutyChangeSpinBox, 0, 1);
    pidLayout->addWidget(sendMaxDutyChangeButton, 0, 2);
    
    pidLayout->addWidget(new QLabel("Max Increase:"), 1, 0);
    pidLayout->addWidget(maxIncreaseSpinBox, 1, 1);
    pidLayout->addWidget(sendMaxIncreaseButton, 1, 2);
    
    pidLayout->addWidget(new QLabel("Max Decrease:"), 2, 0);
    pidLayout->addWidget(maxDecreaseSpinBox, 2, 1);
    pidLayout->addWidget(sendMaxDecreaseButton, 2, 2);
    
    layout->addWidget(pidGroup);
    
    // Recovery Group
    QGroupBox *recoveryGroup = new QGroupBox("Recovery", commandsTab);
    QHBoxLayout *recoveryLayout = new QHBoxLayout(recoveryGroup);
    sendRecoverTempButton = new QPushButton("Force Temperature Recovery", recoveryGroup);
    recoveryLayout->addWidget(sendRecoverTempButton);
    layout->addWidget(recoveryGroup);
    
    // Output Group
    QGroupBox *outputGroup = new QGroupBox("Command Output", commandsTab);
    QVBoxLayout *outputLayout = new QVBoxLayout(outputGroup);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    refreshButton = new QPushButton("Refresh Status", outputGroup);
    clearButton = new QPushButton("Clear Output", outputGroup);
    buttonLayout->addWidget(refreshButton);
    buttonLayout->addWidget(clearButton);
    buttonLayout->addStretch();
    
    outputTextEdit = new QTextEdit(outputGroup);
    outputTextEdit->setMaximumHeight(150);
    outputTextEdit->setReadOnly(true);
    
    outputLayout->addLayout(buttonLayout);
    outputLayout->addWidget(outputTextEdit);
    layout->addWidget(outputGroup);
    
    // Connect signals
    connect(sendFanButton, &QPushButton::clicked, this, &ClevoSettingsDialog::sendFanCommand);
    connect(sendAutoButton, &QPushButton::clicked, this, &ClevoSettingsDialog::sendAutoCommand);
    connect(sendTargetTempButton, &QPushButton::clicked, this, &ClevoSettingsDialog::sendTargetTempCommand);
    connect(sendMaxDutyChangeButton, &QPushButton::clicked, this, &ClevoSettingsDialog::sendMaxDutyChangeCommand);
    connect(sendMaxIncreaseButton, &QPushButton::clicked, this, &ClevoSettingsDialog::sendMaxIncreaseCommand);
    connect(sendMaxDecreaseButton, &QPushButton::clicked, this, &ClevoSettingsDialog::sendMaxDecreaseCommand);
    connect(sendRecoverTempButton, &QPushButton::clicked, this, &ClevoSettingsDialog::sendRecoverTempCommand);
    connect(refreshButton, &QPushButton::clicked, this, &ClevoSettingsDialog::refreshStatus);
    connect(clearButton, &QPushButton::clicked, this, &ClevoSettingsDialog::clearOutput);
}

void ClevoSettingsDialog::setupConnectionTab()
{
    QVBoxLayout *layout = new QVBoxLayout(connectionTab);
    
    QGroupBox *statusGroup = new QGroupBox("Connection Status", connectionTab);
    QVBoxLayout *statusLayout = new QVBoxLayout(statusGroup);
    
    connectionStatusLabel = new QLabel("Disconnected", statusGroup);
    connectionStatusLabel->setStyleSheet("color: red; font-weight: bold;");
    statusLayout->addWidget(connectionStatusLabel);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    connectButton = new QPushButton("Connect", statusGroup);
    disconnectButton = new QPushButton("Disconnect", statusGroup);
    disconnectButton->setEnabled(false);
    
    buttonLayout->addWidget(connectButton);
    buttonLayout->addWidget(disconnectButton);
    buttonLayout->addStretch();
    
    statusLayout->addLayout(buttonLayout);
    layout->addWidget(statusGroup);
    layout->addStretch();
    
    // Connect signals
    connect(connectButton, &QPushButton::clicked, this, &ClevoSettingsDialog::connectToDaemon);
    connect(disconnectButton, &QPushButton::clicked, this, &ClevoSettingsDialog::disconnectFromDaemon);
}

bool ClevoSettingsDialog::connectToDaemon()
{
    if (daemonSocket >= 0) {
        ::close(daemonSocket);
    }
    
    daemonSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (daemonSocket < 0) {
        appendOutput("Failed to create socket: " + QString(strerror(errno)));
        return false;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (::connect(daemonSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        appendOutput("Failed to connect to daemon: " + QString(strerror(errno)));
        ::close(daemonSocket);
        daemonSocket = -1;
        return false;
    }
    
    socketConnected = true;
    appendOutput("Connected to clevo-daemon");
    return true;
}

void ClevoSettingsDialog::disconnectFromDaemon()
{
    if (daemonSocket >= 0) {
        ::close(daemonSocket);
        daemonSocket = -1;
    }
    socketConnected = false;
    appendOutput("Disconnected from daemon");
}

void ClevoSettingsDialog::updateConnectionStatus()
{
    if (socketConnected) {
        connectionStatusLabel->setText("Connected");
        connectionStatusLabel->setStyleSheet("color: green; font-weight: bold;");
        connectButton->setEnabled(false);
        disconnectButton->setEnabled(true);
    } else {
        connectionStatusLabel->setText("Disconnected");
        connectionStatusLabel->setStyleSheet("color: red; font-weight: bold;");
        connectButton->setEnabled(true);
        disconnectButton->setEnabled(false);
    }
}

bool ClevoSettingsDialog::sendCommand(const QString &command)
{
    // Ensure connection (server expects single-command connections)
    if (!socketConnected || daemonSocket < 0) {
        if (!connectToDaemon()) {
            return false;
        }
    }
    
    QByteArray data = command.toUtf8();
    ssize_t sent = send(daemonSocket, data.constData(), data.size(), MSG_NOSIGNAL);
    
    if (sent < 0) {
        if (errno == EPIPE) {
            // Server likely closed previous connection; reconnect and retry once
            socketConnected = false;
            ::close(daemonSocket);
            daemonSocket = -1;
            if (!connectToDaemon()) {
                return false;
            }
            sent = send(daemonSocket, data.constData(), data.size(), MSG_NOSIGNAL);
            if (sent < 0) {
                appendOutput("Failed to send command after reconnect: " + QString(strerror(errno)));
                return false;
            }
        } else {
            appendOutput("Failed to send command: " + QString(strerror(errno)));
            return false;
        }
    }
    
    return true;
}

bool ClevoSettingsDialog::receiveResponse(QString &response)
{
    if (daemonSocket < 0) {
        return false;
    }
    
    // Set up select() for timeout
    fd_set readfds;
    struct timeval timeout;
    
    FD_ZERO(&readfds);
    FD_SET(daemonSocket, &readfds);
    timeout.tv_sec = 2;  // 2 second timeout
    timeout.tv_usec = 0;
    
    int select_result = select(daemonSocket + 1, &readfds, NULL, NULL, &timeout);
    
    if (select_result == 0) {
        appendOutput("Timeout waiting for response");
        return false;
    } else if (select_result < 0) {
        appendOutput("Select error: " + QString(strerror(errno)));
        return false;
    }
    
    // Now safe to recv() without blocking
    char buffer[BUFFER_SIZE];
    ssize_t received = recv(daemonSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (received < 0) {
        appendOutput("Failed to receive response: " + QString(strerror(errno)));
        return false;
    }
    
    buffer[received] = '\0';
    response = QString::fromUtf8(buffer);
    // Server uses one-request-per-connection; close locally to avoid EPIPE on next command
    if (daemonSocket >= 0) {
        ::close(daemonSocket);
        daemonSocket = -1;
        socketConnected = false;
    }
    return true;
}

void ClevoSettingsDialog::appendOutput(const QString &text)
{
    outputTextEdit->append(QString("[%1] %2")
                          .arg(QTime::currentTime().toString("hh:mm:ss"))
                          .arg(text));
}

// Settings tab slots
void ClevoSettingsDialog::updateIntervalChanged(int value)
{
    appendOutput(QString("Update interval changed to %1 ms").arg(value));
}

void ClevoSettingsDialog::windowOpacityChanged(int value)
{
    appendOutput(QString("Window opacity changed to %1%").arg(value));
}

void ClevoSettingsDialog::transparencyToggled(bool checked)
{
    appendOutput(QString("Transparency %1").arg(checked ? "enabled" : "disabled"));
}

void ClevoSettingsDialog::alwaysOnTopToggled(bool checked)
{
    appendOutput(QString("Always on top %1").arg(checked ? "enabled" : "disabled"));
}

void ClevoSettingsDialog::colorCodingToggled(bool checked)
{
    appendOutput(QString("Color coding %1").arg(checked ? "enabled" : "disabled"));
}

void ClevoSettingsDialog::autoStartToggled(bool checked)
{
    appendOutput(QString("Auto-start %1").arg(checked ? "enabled" : "disabled"));
}

// Commands tab slots
void ClevoSettingsDialog::sendFanCommand()
{
    int duty = fanDutySpinBox->value();
    QString command = QString("SET_FAN %1").arg(duty);
    
    if (sendCommand(command)) {
        QString response;
        if (receiveResponse(response)) {
            appendOutput(QString("Set fan duty to %1%: %2").arg(duty).arg(response));
        }
    }
}

void ClevoSettingsDialog::sendAutoCommand()
{
    if (sendCommand("SET_AUTO")) {
        QString response;
        if (receiveResponse(response)) {
            appendOutput("Enabled auto mode: " + response);
        }
    }
}

void ClevoSettingsDialog::sendTargetTempCommand()
{
    int temp = targetTempSpinBox->value();
    QString command = QString("SET_TARGET_TEMP %1").arg(temp);
    
    if (sendCommand(command)) {
        QString response;
        if (receiveResponse(response)) {
            appendOutput(QString("Set target temperature to %1°C: %2").arg(temp).arg(response));
        }
    }
}

void ClevoSettingsDialog::sendMaxDutyChangeCommand()
{
    int rate = maxDutyChangeSpinBox->value();
    QString command = QString("SET_MAX_DUTY_CHANGE %1").arg(rate);
    
    if (sendCommand(command)) {
        QString response;
        if (receiveResponse(response)) {
            appendOutput(QString("Set max duty change to %1%: %2").arg(rate).arg(response));
        }
    }
}

void ClevoSettingsDialog::sendMaxIncreaseCommand()
{
    int rate = maxIncreaseSpinBox->value();
    QString command = QString("SET_MAX_DUTY_INCREASE %1").arg(rate);
    
    if (sendCommand(command)) {
        QString response;
        if (receiveResponse(response)) {
            appendOutput(QString("Set max duty increase to %1%: %2").arg(rate).arg(response));
        }
    }
}

void ClevoSettingsDialog::sendMaxDecreaseCommand()
{
    int rate = maxDecreaseSpinBox->value();
    QString command = QString("SET_MAX_DUTY_DECREASE %1").arg(rate);
    
    if (sendCommand(command)) {
        QString response;
        if (receiveResponse(response)) {
            appendOutput(QString("Set max duty decrease to %1%: %2").arg(rate).arg(response));
        }
    }
}

void ClevoSettingsDialog::sendRecoverTempCommand()
{
    if (sendCommand("RECOVER_TEMP")) {
        QString response;
        if (receiveResponse(response)) {
            appendOutput("Temperature recovery: " + response);
        }
    }
}

void ClevoSettingsDialog::refreshStatus()
{
    if (sendCommand("STATUS")) {
        QString response;
        if (receiveResponse(response)) {
            appendOutput("Current status: " + response);
        }
    }
}

void ClevoSettingsDialog::clearOutput()
{
    outputTextEdit->clear();
} 