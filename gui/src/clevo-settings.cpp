#include "clevo-settings.h"
#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusError>

// DBus constants (use file-scope to avoid access issues)
static const char* CLEVO_DBUS_SERVICE_NAME = "org.freedesktop.ClevoDaemon";
static const char* CLEVO_DBUS_OBJECT_PATH = "/org/freedesktop/ClevoDaemon";
static const char* CLEVO_DBUS_INTERFACE = "org.freedesktop.ClevoDaemon";

ClevoSettingsDialog::ClevoSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , tabWidget(nullptr)
    , settingsTab(nullptr)
    , commandsTab(nullptr)
    , connectionTab(nullptr)
    , dbusConnected(false)
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
    // No explicit teardown for DBus
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
    QDBusConnection connection = QDBusConnection::systemBus();
    if (!connection.isConnected()) {
        appendOutput("Failed to connect to system bus");
        dbusConnected = false;
        return false;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(CLEVO_DBUS_SERVICE_NAME, CLEVO_DBUS_OBJECT_PATH, CLEVO_DBUS_INTERFACE, "GetStatus");
    QDBusReply<QString> reply = connection.call(msg, QDBus::BlockWithGui, 1000);
    if (reply.isValid()) {
        dbusConnected = true;
        appendOutput("Connected to clevo-daemon (DBus)");
        return true;
    }
    appendOutput("Failed to connect to daemon: " + reply.error().message());
    dbusConnected = false;
    return false;
}

void ClevoSettingsDialog::disconnectFromDaemon()
{
    dbusConnected = false;
    appendOutput("Disconnected from daemon (DBus)");
}

void ClevoSettingsDialog::updateConnectionStatus()
{
    if (dbusConnected) {
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

static QString callGetStatus()
{
    QDBusConnection connection = QDBusConnection::systemBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(CLEVO_DBUS_SERVICE_NAME,
                                                      CLEVO_DBUS_OBJECT_PATH,
                                                      CLEVO_DBUS_INTERFACE,
                                                      "GetStatus");
    QDBusReply<QString> reply = connection.call(msg, QDBus::BlockWithGui, 2000);
    if (reply.isValid()) return reply.value();
    return QString("DBus error: ") + reply.error().message();
}

static bool callInt(const char *method, int value, QString &out)
{
    QDBusConnection connection = QDBusConnection::systemBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(CLEVO_DBUS_SERVICE_NAME,
                                                      CLEVO_DBUS_OBJECT_PATH,
                                                      CLEVO_DBUS_INTERFACE,
                                                      method);
    msg << value;
    QDBusReply<QString> reply = connection.call(msg, QDBus::BlockWithGui, 2000);
    if (!reply.isValid()) { out = QString("DBus error: ") + reply.error().message(); return false; }
    out = reply.value();
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
    QString response;
    if (callInt("SetFanDuty", duty, response))
        appendOutput(QString("Set fan duty to %1%: %2").arg(duty).arg(response));
    else
        appendOutput(response);
}

void ClevoSettingsDialog::sendAutoCommand()
{
    QDBusConnection connection = QDBusConnection::systemBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, "SetAutoMode");
    msg << true;
    QDBusReply<QString> reply = connection.call(msg, QDBus::BlockWithGui, 2000);
    if (reply.isValid()) appendOutput("Enabled auto mode: " + reply.value());
    else appendOutput("Failed to enable auto mode: " + reply.error().message());
}

void ClevoSettingsDialog::sendTargetTempCommand()
{
    int temp = targetTempSpinBox->value();
    QString response;
    if (callInt("SetTargetTemp", temp, response)) appendOutput(QString("Set target temperature to %1°C: %2").arg(temp).arg(response));
    else appendOutput(response);
}

void ClevoSettingsDialog::sendMaxDutyChangeCommand()
{
    int rate = maxDutyChangeSpinBox->value();
    QString response;
    if (callInt("SetMaxDutyChange", rate, response)) appendOutput(QString("Set max duty change to %1%: %2").arg(rate).arg(response));
    else appendOutput(response);
}

void ClevoSettingsDialog::sendMaxIncreaseCommand()
{
    int rate = maxIncreaseSpinBox->value();
    QString response;
    if (callInt("SetMaxDutyIncrease", rate, response)) appendOutput(QString("Set max duty increase to %1%: %2").arg(rate).arg(response));
    else appendOutput(response);
}

void ClevoSettingsDialog::sendMaxDecreaseCommand()
{
    int rate = maxDecreaseSpinBox->value();
    QString response;
    if (callInt("SetMaxDutyDecrease", rate, response)) appendOutput(QString("Set max duty decrease to %1%: %2").arg(rate).arg(response));
    else appendOutput(response);
}

void ClevoSettingsDialog::sendRecoverTempCommand()
{
    QDBusConnection connection = QDBusConnection::systemBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, "RecoverTemp");
    QDBusReply<QString> reply = connection.call(msg, QDBus::BlockWithGui, 2000);
    if (reply.isValid()) appendOutput("Temperature recovery: " + reply.value());
    else appendOutput("Failed to recover temperature: " + reply.error().message());
}

void ClevoSettingsDialog::refreshStatus()
{
    QString response = callGetStatus();
    appendOutput("Current status: " + response);
}

void ClevoSettingsDialog::clearOutput()
{
    outputTextEdit->clear();
} 