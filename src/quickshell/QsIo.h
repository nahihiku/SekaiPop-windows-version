#pragma once

#include "QsTypes.h"

#include <QFileSystemWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

class QProcess;

namespace SekaiPop::Services {
class AudioSpectrumSource;
class MediaSource;
class QuickSource;
class StatusIconsService;
}

namespace SekaiPop::Win32 {
class WindowTracker;
}

namespace SekaiPop::Quickshell {

class QsStdioCollector : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(bool waitForEnd READ waitForEnd WRITE setWaitForEnd NOTIFY waitForEndChanged)

public:
    explicit QsStdioCollector(QObject* parent = nullptr) : QObject(parent) {}

    QString text() const { return m_text; }
    bool waitForEnd() const { return m_waitForEnd; }
    void setWaitForEnd(bool w) {
        if (m_waitForEnd != w) {
            m_waitForEnd = w;
            Q_EMIT waitForEndChanged();
        }
    }

    void reset() {
        m_text.clear();
        Q_EMIT textChanged();
    }

    void appendAndFinish(const QString& out) {
        m_text = out;
        Q_EMIT textChanged();
        Q_EMIT streamFinished();
    }

Q_SIGNALS:
    void textChanged();
    void waitForEndChanged();
    void streamFinished();
    void dataRead();

private:
    QString m_text;
    bool m_waitForEnd = true;
};

class QsSplitParser : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString splitMarker READ splitMarker WRITE setSplitMarker NOTIFY splitMarkerChanged)

public:
    explicit QsSplitParser(QObject* parent = nullptr) : QObject(parent) {}

    QString splitMarker() const { return m_splitMarker; }
    void setSplitMarker(const QString& m) {
        if (m_splitMarker != m) {
            m_splitMarker = m;
            Q_EMIT splitMarkerChanged();
        }
    }

    void feedChunk(const QString& chunk);
    void feedLine(const QString& line) { Q_EMIT read(line); }

Q_SIGNALS:
    void splitMarkerChanged();
    void read(const QString& data);

private:
    QString m_splitMarker = QStringLiteral("\n");
};

class QsProcess : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(QVariant command READ command WRITE setCommand NOTIFY commandChanged)
    Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY workingDirectoryChanged)
    Q_PROPERTY(QVariantMap environment READ environment WRITE setEnvironment NOTIFY environmentChanged)
    Q_PROPERTY(bool stdinEnabled READ stdinEnabled WRITE setStdinEnabled NOTIFY stdinEnabledChanged)
    Q_PROPERTY(QObject* stdout READ stdoutParser WRITE setStdoutParser NOTIFY stdoutParserChanged)
    Q_PROPERTY(QObject* stderr READ stderrParser WRITE setStderrParser NOTIFY stderrParserChanged)

public:
    explicit QsProcess(QObject* parent = nullptr);
    ~QsProcess() override;

    static void initBackends(
        Services::AudioSpectrumSource* spectrumSource,
        Services::QuickSource* quickSource,
        Services::StatusIconsService* statusIconsService,
        Win32::WindowTracker* windowTracker,
        Services::MediaSource* mediaSource = nullptr
    );
    static void executeDetachedCommand(const QStringList& args);

    bool running() const { return m_running; }
    void setRunning(bool r);

    QVariant command() const { return m_command; }
    void setCommand(const QVariant& c);

    QString workingDirectory() const { return m_workingDir; }
    void setWorkingDirectory(const QString& d);

    QVariantMap environment() const { return m_env; }
    void setEnvironment(const QVariantMap& e);

    bool stdinEnabled() const { return m_stdinEnabled; }
    void setStdinEnabled(bool e);

    QObject* stdoutParser() const { return m_stdout; }
    void setStdoutParser(QObject* p);

    QObject* stderrParser() const { return m_stderr; }
    void setStderrParser(QObject* p);

    Q_INVOKABLE void exec(const QVariant& cmd);
    Q_INVOKABLE void write(const QString& data);
    Q_INVOKABLE void signal(int sig);

    void classBegin() override {}
    void componentComplete() override;

Q_SIGNALS:
    void runningChanged();
    void commandChanged();
    void workingDirectoryChanged();
    void environmentChanged();
    void stdinEnabledChanged();
    void stdoutParserChanged();
    void stderrParserChanged();
    void started();
    void exited(int exitCode, int exitStatus);

private:
    QStringList parseCommandArgs(const QVariant& cmd) const;
    void startExecution();
    void stopExecution();
    void finishWithOutput(const QString& out, const QString& err = QString(), int exitCode = 0);
    void onCavaSpectrumTick();

    bool m_running = false;
    bool m_complete = false;
    bool m_isCavaStream = false;
    int m_cavaBars = 24;
    int m_runGeneration = 0;
    QVariant m_command;
    QString m_workingDir;
    QVariantMap m_env;
    bool m_stdinEnabled = false;
    QPointer<QObject> m_stdout;
    QPointer<QObject> m_stderr;
    QPointer<QNetworkReply> m_activeReply;
    QPointer<QProcess> m_childProc;
    QByteArray m_childStdoutBuf;
    QString m_lastStreamLine;
};

class QsFileView : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool watchChanges READ watchChanges WRITE setWatchChanges NOTIFY watchChangesChanged)
    Q_PROPERTY(bool blockLoading READ blockLoading WRITE setBlockLoading NOTIFY blockLoadingChanged)
    Q_PROPERTY(bool blockWrites READ blockWrites WRITE setBlockWrites NOTIFY blockWritesChanged)
    Q_PROPERTY(bool atomicWrites READ atomicWrites WRITE setAtomicWrites NOTIFY atomicWritesChanged)
    Q_PROPERTY(bool printErrors READ printErrors WRITE setPrintErrors NOTIFY printErrorsChanged)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY loadedChanged)

public:
    explicit QsFileView(QObject* parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString& p);

    bool watchChanges() const { return m_watchChanges; }
    void setWatchChanges(bool w);

    bool blockLoading() const { return m_blockLoading; }
    void setBlockLoading(bool b);

    bool blockWrites() const { return m_blockWrites; }
    void setBlockWrites(bool b) { if (m_blockWrites != b) { m_blockWrites = b; Q_EMIT blockWritesChanged(); } }

    bool atomicWrites() const { return m_atomicWrites; }
    void setAtomicWrites(bool a) { if (m_atomicWrites != a) { m_atomicWrites = a; Q_EMIT atomicWritesChanged(); } }

    bool printErrors() const { return m_printErrors; }
    void setPrintErrors(bool p) { if (m_printErrors != p) { m_printErrors = p; Q_EMIT printErrorsChanged(); } }

    bool isLoaded() const { return m_isLoaded; }

    Q_INVOKABLE QString text();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setText(const QString& content);
    Q_INVOKABLE void waitForJob() {}

    void classBegin() override {}
    void componentComplete() override;

Q_SIGNALS:
    void pathChanged();
    void watchChangesChanged();
    void blockLoadingChanged();
    void blockWritesChanged();
    void atomicWritesChanged();
    void printErrorsChanged();
    void loadedChanged();
    void loaded();
    void saved();
    void loadFailed(int error);
    void saveFailed(int error);
    void fileChanged();

private:
    void performLoad(bool emitSignals);
    void updateWatcher();

    QString m_path;
    QString m_cachedText;
    bool m_watchChanges = false;
    bool m_blockLoading = false;
    bool m_blockWrites = false;
    bool m_atomicWrites = true;
    bool m_printErrors = true;
    bool m_isLoaded = false;
    bool m_complete = false;
    QFileSystemWatcher m_watcher;
};

class QsSocket : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY connectedChanged)
    Q_PROPERTY(QObject* parser READ parser WRITE setParser NOTIFY parserChanged)

public:
    using QObject::QObject;

    QString path() const { return m_path; }
    void setPath(const QString& p) { if (m_path != p) { m_path = p; Q_EMIT pathChanged(); } }

    bool connected() const { return false; }
    void setConnected(bool) { Q_EMIT connectedChanged(); }

    QObject* parser() const { return m_parser; }
    void setParser(QObject* p) { if (m_parser != p) { m_parser = p; Q_EMIT parserChanged(); } }

    Q_INVOKABLE void write(const QString&) {}
    Q_INVOKABLE void flush() {}

Q_SIGNALS:
    void pathChanged();
    void connectedChanged();
    void parserChanged();
    void connectionStateChanged();
    void error(const QString& error);

private:
    QString m_path;
    QPointer<QObject> m_parser;
};

} // namespace SekaiPop::Quickshell
