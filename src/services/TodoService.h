#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

namespace SekaiPop::Services {

/**
 * Persistent desktop To-Do checklist and card placement model
 * stored in %LOCALAPPDATA%\SekaiPop\SekaiPopShell\todos.json.
 */
class TodoService final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY changed)
    Q_PROPERTY(qreal posX READ posX NOTIFY changed)
    Q_PROPERTY(qreal posY READ posY NOTIFY changed)

public:
    explicit TodoService(QObject* parent = nullptr);

    QVariantList items() const { return m_items; }
    int totalCount() const { return m_items.size(); }
    int doneCount() const;
    qreal posX() const { return m_posX; }
    qreal posY() const { return m_posY; }

    Q_INVOKABLE void addTask(const QString& text);
    Q_INVOKABLE void toggleDone(const QString& id);
    Q_INVOKABLE void editTask(const QString& id, const QString& text);
    Q_INVOKABLE void removeTask(const QString& id);
    Q_INVOKABLE void clearDone();
    Q_INVOKABLE void setPosition(qreal x, qreal y);
    Q_INVOKABLE void resetPosition();

Q_SIGNALS:
    void changed();

private:
    void load();
    void save() const;

    QString m_filePath;
    QVariantList m_items;
    qreal m_posX = -1.0;
    qreal m_posY = -1.0;
    int m_nextId = 1;
};

} // namespace SekaiPop::Services
