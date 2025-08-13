#ifndef CLEVO_SETTINGS_H
#define CLEVO_SETTINGS_H

#include <QDialog>
#include <QTabWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QTextEdit>
#include <QComboBox>
#include <QSlider>
#include <QProgressBar>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QMessageBox>
#include <QApplication>

class ClevoSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ClevoSettingsDialog(QWidget *parent = nullptr);
    ~ClevoSettingsDialog();

private slots:
    // Settings tab slots
    void updateIntervalChanged(int value);
    void windowOpacityChanged(int value);
    void transparencyToggled(bool checked);
    void alwaysOnTopToggled(bool checked);
    void colorCodingToggled(bool checked);
    void autoStartToggled(bool checked);
    
    // Commands tab slots
    void sendFanCommand();
    void sendAutoCommand();
    void sendTargetTempCommand();
    void sendMaxDutyChangeCommand();
    void sendMaxIncreaseCommand();
    void sendMaxDecreaseCommand();
    void sendRecoverTempCommand();
    void refreshStatus();
    void clearOutput();
    
    // Connection slots
    bool connectToDaemon();
    void disconnectFromDaemon();
    void updateConnectionStatus();

private:
    void setupUI();
    void setupSettingsTab();
    void setupCommandsTab();
    void setupConnectionTab();
    
    // DBus helpers
    void appendOutput(const QString &text);
    bool callMethodNoArg(const char *method, QString &outResponse);
    bool callMethodIntArg(const char *method, int value, QString &outResponse);
    
    // UI Components
    QTabWidget *tabWidget;
    
    // Settings tab
    QWidget *settingsTab;
    QSpinBox *updateIntervalSpinBox;
    QSlider *opacitySlider;
    QCheckBox *transparencyCheckBox;
    QCheckBox *alwaysOnTopCheckBox;
    QCheckBox *colorCodingCheckBox;
    QCheckBox *autoStartCheckBox;
    
    // Commands tab
    QWidget *commandsTab;
    QSpinBox *fanDutySpinBox;
    QSpinBox *targetTempSpinBox;
    QSpinBox *maxDutyChangeSpinBox;
    QSpinBox *maxIncreaseSpinBox;
    QSpinBox *maxDecreaseSpinBox;
    QPushButton *sendFanButton;
    QPushButton *sendAutoButton;
    QPushButton *sendTargetTempButton;
    QPushButton *sendMaxDutyChangeButton;
    QPushButton *sendMaxIncreaseButton;
    QPushButton *sendMaxDecreaseButton;
    QPushButton *sendRecoverTempButton;
    QPushButton *refreshButton;
    QPushButton *clearButton;
    QTextEdit *outputTextEdit;
    
    // Connection tab
    QWidget *connectionTab;
    QPushButton *connectButton;
    QPushButton *disconnectButton;
    QLabel *connectionStatusLabel;
    QTimer *statusTimer;
    
    // DBus state
    bool dbusConnected;
    static constexpr const char* DBUS_SERVICE_NAME = "org.freedesktop.ClevoDaemon";
    static constexpr const char* DBUS_OBJECT_PATH = "/org/freedesktop/ClevoDaemon";
    static constexpr const char* DBUS_INTERFACE = "org.freedesktop.ClevoDaemon";
};

#endif // CLEVO_SETTINGS_H 