#include "TodoService.h"
#include "SettingsService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace SekaiPop::Services {

TodoService::TodoService(QObject* parent)
    : QObject(parent) {
    const QString dir = SettingsService::stateDir();
    QDir().mkpath(dir);
    m_filePath = QDir(dir).filePath(QStringLiteral("todos.json"));
    load();
}

int TodoService::doneCount() const {
    int count = 0;
    for (const QVariant& v : m_items) {
        if (v.toMap().value(QStringLiteral("done")).toBool()) {
            ++count;
        }
    }
    return count;
}

void TodoService::addTask(const QString& text) {
    const QString clean = text.trimmed();
    if (clean.isEmpty()) {
        return;
    }
    QVariantMap row;
    row.insert(QStringLiteral("id"), QString::number(m_nextId++));
    row.insert(QStringLiteral("text"), clean);
    row.insert(QStringLiteral("done"), false);
    m_items.push_back(row);
    save();
    Q_EMIT changed();
}

void TodoService::toggleDone(const QString& id) {
    for (int i = 0; i < m_items.size(); ++i) {
        QVariantMap row = m_items.at(i).toMap();
        if (row.value(QStringLiteral("id")).toString() == id) {
            row.insert(QStringLiteral("done"), !row.value(QStringLiteral("done")).toBool());
            m_items[i] = row;
            save();
            Q_EMIT changed();
            return;
        }
    }
}

void TodoService::editTask(const QString& id, const QString& text) {
    const QString clean = text.trimmed();
    if (clean.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_items.size(); ++i) {
        QVariantMap row = m_items.at(i).toMap();
        if (row.value(QStringLiteral("id")).toString() == id) {
            row.insert(QStringLiteral("text"), clean);
            m_items[i] = row;
            save();
            Q_EMIT changed();
            return;
        }
    }
}

void TodoService::removeTask(const QString& id) {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            m_items.removeAt(i);
            save();
            Q_EMIT changed();
            return;
        }
    }
}

void TodoService::clearDone() {
    QVariantList next;
    for (const QVariant& v : m_items) {
        if (!v.toMap().value(QStringLiteral("done")).toBool()) {
            next.push_back(v);
        }
    }
    if (next.size() != m_items.size()) {
        m_items = std::move(next);
        save();
        Q_EMIT changed();
    }
}

void TodoService::setPosition(qreal x, qreal y) {
    m_posX = x;
    m_posY = y;
    save();
    Q_EMIT changed();
}

void TodoService::resetPosition() {
    m_posX = -1.0;
    m_posY = -1.0;
    save();
    Q_EMIT changed();
}

void TodoService::load() {
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_items = {
            QVariantMap{ { QStringLiteral("id"), QStringLiteral("1") }, { QStringLiteral("text"), QStringLiteral("Alt+Space: Open App Launcher") }, { QStringLiteral("done"), false } },
            QVariantMap{ { QStringLiteral("id"), QStringLiteral("2") }, { QStringLiteral("text"), QStringLiteral("Alt+N: Open Quick Panel & Settings") }, { QStringLiteral("done"), false } },
            QVariantMap{ { QStringLiteral("id"), QStringLiteral("3") }, { QStringLiteral("text"), QStringLiteral("Ctrl+Alt+Shift+E: Restore Explorer") }, { QStringLiteral("done"), false } }
        };
        m_nextId = 4;
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject obj = doc.object();
    m_posX = obj.value(QStringLiteral("posX")).toDouble(-1.0);
    m_posY = obj.value(QStringLiteral("posY")).toDouble(-1.0);

    const QJsonArray arr = obj.value(QStringLiteral("items")).toArray();
    m_items.clear();
    int maxId = 0;
    for (const QJsonValue& val : arr) {
        const QJsonObject item = val.toObject();
        const QString idStr = item.value(QStringLiteral("id")).toString();
        const int numId = idStr.toInt();
        if (numId > maxId) {
            maxId = numId;
        }
        m_items.push_back(QVariantMap{
            { QStringLiteral("id"), idStr.isEmpty() ? QString::number(++maxId) : idStr },
            { QStringLiteral("text"), item.value(QStringLiteral("text")).toString() },
            { QStringLiteral("done"), item.value(QStringLiteral("done")).toBool() }
        });
    }
    m_nextId = maxId + 1;
}

void TodoService::save() const {
    QFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QJsonArray arr;
    for (const QVariant& v : m_items) {
        const QVariantMap row = v.toMap();
        QJsonObject o;
        o.insert(QStringLiteral("id"), row.value(QStringLiteral("id")).toString());
        o.insert(QStringLiteral("text"), row.value(QStringLiteral("text")).toString());
        o.insert(QStringLiteral("done"), row.value(QStringLiteral("done")).toBool());
        arr.append(o);
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("posX"), m_posX);
    obj.insert(QStringLiteral("posY"), m_posY);
    obj.insert(QStringLiteral("items"), arr);
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

} // namespace SekaiPop::Services
