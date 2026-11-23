#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

namespace SekaiPop::Services {

class SettingsService;

/**
 * In-shell notification center backend for QuickPanelFace's notification stack
 * and Do-Not-Disturb toggle.
 */
class NotificationSource final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool dnd READ dnd WRITE setDnd NOTIFY dndChanged)

public:
    explicit NotificationSource(SettingsService* settings, QObject* parent = nullptr);

    QVariantList list() const { return m_list; }
    int count() const { return m_list.size(); }
    bool dnd() const;
    void setDnd(bool value);

    Q_INVOKABLE void push(const QString& appName, const QString& summary, const QString& body = {}, const QString& icon = QStringLiteral("notifications"));
    Q_INVOKABLE void dismiss(int id);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void toggleDnd();

Q_SIGNALS:
    void changed();
    void dndChanged();

private:
    SettingsService* m_settings = nullptr;
    QVariantList m_list;
    int m_nextId = 1;
};

} // namespace SekaiPop::Services
