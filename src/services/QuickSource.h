#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace SekaiPop::Services {

class OsdSource;
class SettingsService;

/**
 * Native Windows system controls backend:
 *   - CoreAudio speaker & microphone volume/mute
 *   - WlanApi Wi-Fi SSID/radio state + scan list + Ethernet state
 *   - Win32 Bluetooth radio & connected devices discovery
 *   - Display Brightness (WMI laptop panel / DDC/CI monitor + gamma fallback)
 *   - Night Light warm color temperature via GDI gamma ramp
 *   - Live network rx/tx throughput (downloadBps / uploadBps)
 *   - Windows power & session actions
 */
class QuickSource final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)

    Q_PROPERTY(bool wifiOn READ wifiOn NOTIFY changed)
    Q_PROPERTY(QString wifiName READ wifiName NOTIFY changed)
    Q_PROPERTY(QString ethernetName READ ethernetName NOTIFY changed)
    Q_PROPERTY(QString networkKind READ networkKind NOTIFY changed)
    Q_PROPERTY(bool networkOn READ networkOn NOTIFY changed)
    Q_PROPERTY(QString networkDetail READ networkDetail NOTIFY changed)
    Q_PROPERTY(QVariantList wifiNetworks READ wifiNetworks NOTIFY changed)

    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY changed)
    Q_PROPERTY(bool bluetoothOn READ bluetoothOn NOTIFY changed)
    Q_PROPERTY(bool bluetoothConnected READ bluetoothConnected NOTIFY changed)
    Q_PROPERTY(QString bluetoothDetail READ bluetoothDetail NOTIFY changed)

    Q_PROPERTY(bool speakerAvailable READ speakerAvailable NOTIFY changed)
    Q_PROPERTY(bool speakerOn READ speakerOn NOTIFY changed)
    Q_PROPERTY(qreal speakerVolume READ speakerVolume NOTIFY changed)

    Q_PROPERTY(bool microphoneAvailable READ microphoneAvailable NOTIFY changed)
    Q_PROPERTY(bool microphoneOn READ microphoneOn NOTIFY changed)
    Q_PROPERTY(qreal microphoneVolume READ microphoneVolume NOTIFY changed)

    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY changed)
    Q_PROPERTY(qreal brightness READ brightness NOTIFY changed)
    Q_PROPERTY(qreal gamma READ gamma NOTIFY changed)
    Q_PROPERTY(bool nightLightOn READ nightLightOn NOTIFY changed)
    Q_PROPERTY(bool keepAwakeOn READ keepAwakeOn NOTIFY changed)
    Q_PROPERTY(bool notificationsOn READ notificationsOn NOTIFY changed)
    Q_PROPERTY(bool gameModeOn READ gameModeOn NOTIFY changed)
    Q_PROPERTY(QString uptimeText READ uptimeText NOTIFY changed)

    Q_PROPERTY(qreal downloadBps READ downloadBps NOTIFY changed)
    Q_PROPERTY(qreal uploadBps READ uploadBps NOTIFY changed)

public:
    explicit QuickSource(SettingsService* settings = nullptr, OsdSource* osd = nullptr, QObject* parent = nullptr);
    ~QuickSource() override;

    QString userName() const { return m_userName; }
    QString hostName() const { return m_hostName; }

    bool wifiOn() const { return m_wifiOn; }
    QString wifiName() const { return m_wifiName; }
    QString ethernetName() const { return m_ethernetName; }
    QString networkKind() const;
    bool networkOn() const;
    QString networkDetail() const;
    QVariantList wifiNetworks() const { return m_wifiNetworks; }

    bool bluetoothAvailable() const { return m_bluetoothAvailable; }
    bool bluetoothOn() const { return m_bluetoothOn; }
    bool bluetoothConnected() const { return m_bluetoothConnected; }
    QString bluetoothDetail() const { return m_bluetoothDetail; }

    bool speakerAvailable() const { return m_speakerAvailable; }
    bool speakerOn() const { return m_speakerOn; }
    qreal speakerVolume() const { return m_speakerVolume; }

    bool microphoneAvailable() const { return m_microphoneAvailable; }
    bool microphoneOn() const { return m_microphoneOn; }
    qreal microphoneVolume() const { return m_microphoneVolume; }

    bool brightnessAvailable() const { return m_brightnessAvailable; }
    qreal brightness() const { return m_brightness; }
    qreal gamma() const { return m_gamma; }
    int nightLightKelvin() const { return m_nightLightKelvin; }
    bool nightLightOn() const { return m_nightLightOn; }
    bool keepAwakeOn() const { return m_keepAwakeOn; }
    bool notificationsOn() const { return m_notificationsOn; }
    bool gameModeOn() const { return m_gameModeOn; }
    QString uptimeText() const;

    qreal downloadBps() const { return m_downloadBps; }
    qreal uploadBps() const { return m_uploadBps; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void scanWifiNetworks();
    Q_INVOKABLE void setWifiEnabled(bool enabled);
    Q_INVOKABLE void toggleWifi();
    Q_INVOKABLE void toggleNetwork() { toggleWifi(); }
    Q_INVOKABLE void connectWifi(const QString& ssid);
    Q_INVOKABLE void setBluetoothEnabled(bool enabled);
    Q_INVOKABLE void toggleBluetooth();

    Q_INVOKABLE void setSpeakerVolume(qreal percent);
    Q_INVOKABLE void toggleSpeakerMute();
    Q_INVOKABLE void toggleSpeaker() { toggleSpeakerMute(); }
    Q_INVOKABLE void setMicrophoneVolume(qreal percent);
    Q_INVOKABLE void toggleMicrophoneMute();
    Q_INVOKABLE void toggleMicrophone() { toggleMicrophoneMute(); }

    Q_INVOKABLE void setBrightness(qreal percent);
    Q_INVOKABLE void setGamma(qreal percent);
    Q_INVOKABLE void setNightLightTemperature(int kelvin);
    Q_INVOKABLE void toggleNightLight();
    Q_INVOKABLE void toggleKeepAwake();
    Q_INVOKABLE void toggleNotifications();
    Q_INVOKABLE void toggleGameMode();

    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE void openUrl(const QString& url);
    Q_INVOKABLE void runCommand(const QString& command, bool inTerminal);
    Q_INVOKABLE void openWindowsSettings(const QString& pageUri = {});
    Q_INVOKABLE void openNetworkSettings() { openWindowsSettings(QStringLiteral("ms-settings:network")); }
    Q_INVOKABLE void openBluetoothSettings() { openWindowsSettings(QStringLiteral("ms-settings:bluetooth")); }
    Q_INVOKABLE void openSoundSettings() { openWindowsSettings(QStringLiteral("ms-settings:sound")); }
    Q_INVOKABLE void openDisplaySettings() { openWindowsSettings(QStringLiteral("ms-settings:display")); }
    Q_INVOKABLE void openOnScreenKeyboard();
    Q_INVOKABLE void triggerScreenSnip();
    Q_INVOKABLE void openTaskManager();

    Q_INVOKABLE void lockWorkstation();
    Q_INVOKABLE void lockScreen() { lockWorkstation(); }
    Q_INVOKABLE void sleepSystem();
    Q_INVOKABLE void sleep() { sleepSystem(); }
    Q_INVOKABLE void hibernate();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void logout() { signOut(); }
    Q_INVOKABLE void restartSystem();
    Q_INVOKABLE void reboot() { restartSystem(); }
    Q_INVOKABLE void shutdownSystem();
    Q_INVOKABLE void powerOff() { shutdownSystem(); }

Q_SIGNALS:
    void changed();

private:
    void pollAudio();
    void pollNetwork();
    void pollBluetooth();
    void pollBrightness();
    void pollThroughput();
    void applyGammaState();
    void pollThreadLoop();
    void brightnessThreadLoop();

    SettingsService* m_settings = nullptr;
    OsdSource* m_osd = nullptr;
    QTimer m_timer;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_wakeNow{ true };
    std::thread m_pollThread;
    std::mutex m_brightnessMutex;
    std::condition_variable m_brightnessCv;
    int m_pendingBrightness = -1;
    std::thread m_brightnessThread;
    bool m_gammaModified = false;
    QString m_userName;
    QString m_hostName;

    bool m_wifiOn = false;
    QString m_wifiName;
    QString m_ethernetName;
    QVariantList m_wifiNetworks;

    bool m_bluetoothAvailable = false;
    bool m_bluetoothOn = false;
    bool m_bluetoothConnected = false;
    QString m_bluetoothDetail = QStringLiteral("OFF");

    bool m_speakerAvailable = false;
    bool m_speakerOn = true;
    qreal m_speakerVolume = 0.0;

    bool m_microphoneAvailable = false;
    bool m_microphoneOn = true;
    qreal m_microphoneVolume = 0.0;

    bool m_brightnessAvailable = true;
    bool m_hardwareBrightness = false;
    qreal m_brightness = 100.0;
    qreal m_gamma = 100.0;
    int m_nightLightKelvin = 4500;
    bool m_nightLightOn = false;
    bool m_keepAwakeOn = false;
    bool m_notificationsOn = true;
    bool m_gameModeOn = false;
    int m_pollTick = 0;

    qreal m_downloadBps = 0.0;
    qreal m_uploadBps = 0.0;
    quint64 m_prevInBytes = 0;
    quint64 m_prevOutBytes = 0;
    qint64 m_prevSampleMs = 0;
};

} // namespace SekaiPop::Services
