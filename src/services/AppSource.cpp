#include "AppSource.h"
#include "SettingsService.h"
#include "../win32/Win32Utils.h"
#include "../win32/WinIconProvider.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <algorithm>
#include <iterator>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <thread>

namespace SekaiPop::Services {

namespace {

bool isNoiseShortcutName(const QString& lowerName) {
    static const QStringList noise = {
        QStringLiteral("uninstall"),
        QStringLiteral("uninstaller"),
        QStringLiteral("readme"),
        QStringLiteral("help"),
        QStringLiteral("release notes"),
        QStringLiteral("website"),
        QStringLiteral("manual"),
        QStringLiteral("documentation")
    };
    for (const QString& word : noise) {
        if (lowerName.contains(word)) {
            return true;
        }
    }
    return false;
}

bool resolveShortcut(
    const QString& lnkPath,
    QString& outTarget,
    QString& outArgs,
    QString& outWorkDir,
    QString& outComment,
    QString& outIconSpec
) {
    IShellLinkW* psl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl))) || !psl) {
        return false;
    }

    bool ok = false;
    IPersistFile* ppf = nullptr;
    if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&ppf))) && ppf) {
        const std::wstring wLnk = Win32::toStdWString(QDir::toNativeSeparators(lnkPath));
        if (SUCCEEDED(ppf->Load(wLnk.c_str(), STGM_READ))) {
            wchar_t targetBuf[MAX_PATH * 2] = {};
            WIN32_FIND_DATAW wfd = {};
            if (SUCCEEDED(psl->GetPath(targetBuf, static_cast<int>(std::size(targetBuf)), &wfd, SLGP_RAWPATH))) {
                outTarget = QString::fromWCharArray(targetBuf).trimmed();
            }

            wchar_t argsBuf[1024] = {};
            if (SUCCEEDED(psl->GetArguments(argsBuf, static_cast<int>(std::size(argsBuf))))) {
                outArgs = QString::fromWCharArray(argsBuf).trimmed();
            }

            wchar_t dirBuf[MAX_PATH * 2] = {};
            if (SUCCEEDED(psl->GetWorkingDirectory(dirBuf, static_cast<int>(std::size(dirBuf))))) {
                outWorkDir = QString::fromWCharArray(dirBuf).trimmed();
            }

            wchar_t descBuf[512] = {};
            if (SUCCEEDED(psl->GetDescription(descBuf, static_cast<int>(std::size(descBuf))))) {
                outComment = QString::fromWCharArray(descBuf).trimmed();
            }

            wchar_t iconBuf[MAX_PATH * 2] = {};
            int iconIdx = 0;
            if (SUCCEEDED(psl->GetIconLocation(iconBuf, static_cast<int>(std::size(iconBuf)), &iconIdx))) {
                const QString iconPath = QString::fromWCharArray(iconBuf).trimmed();
                if (!iconPath.isEmpty()) {
                    wchar_t expanded[MAX_PATH * 2] = {};
                    const DWORD n = ExpandEnvironmentStringsW(Win32::toStdWString(iconPath).c_str(), expanded, static_cast<DWORD>(std::size(expanded)));
                    const QString expStr = (n > 0 && n < std::size(expanded)) ? QString::fromWCharArray(expanded) : iconPath;
                    if (QFileInfo::exists(expStr)) {
                        outIconSpec = QStringLiteral("%1,%2").arg(expStr).arg(iconIdx);
                    }
                }
            }
            ok = true;
        }
        ppf->Release();
    }
    psl->Release();
    return ok;
}

} // namespace

AppSource::AppSource(SettingsService* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings) {
    const QString dir = SettingsService::stateDir();
    QDir().mkpath(dir);
    m_usagePath = QDir(dir).filePath(QStringLiteral("app-usage.json"));
    loadUsage();

    if (m_settings) {
        connect(m_settings, &SettingsService::keyChanged, this, [this](const QString& key) {
            if (key == QStringLiteral("launcher.order")) {
                rebuildOrdered();
            }
        });
    }

    rescan();
}

void AppSource::loadUsage() {
    QFile f(m_usagePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        m_usage.insert(it.key(), it.value().toInt());
    }
}

void AppSource::saveUsage() const {
    QFile f(m_usagePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QJsonObject obj;
    for (auto it = m_usage.constBegin(); it != m_usage.constEnd(); ++it) {
        obj.insert(it.key(), it.value());
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void AppSource::sync() {
    if (m_entries.isEmpty() && !m_scanning) {
        rescan();
    } else {
        rebuildOrdered();
    }
}

void AppSource::rescan() {
    if (m_scanning) {
        return;
    }
    m_scanning = true;
    Q_EMIT scanningChanged();

    QPointer<AppSource> self(this);
    std::thread([self]() {
        QList<AppEntry> found = scanWindowsApps();
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, found = std::move(found)]() mutable {
            if (!self) {
                return;
            }
            self->m_entries = std::move(found);
            self->m_scanning = false;
            Q_EMIT self->scanningChanged();
            self->rebuildOrdered();
        }, Qt::QueuedConnection);
    }).detach();
}

QList<AppSource::AppEntry> AppSource::scanWindowsApps() {
    Win32::ComInitGuard com;
    QList<AppEntry> out;
    QSet<QString> seenNames;

    // 1. Scan Start Menu .lnk shortcuts (user + all-users).
    const KNOWNFOLDERID folderIds[] = { FOLDERID_Programs, FOLDERID_CommonPrograms };
    for (const KNOWNFOLDERID& fid : folderIds) {
        PWSTR rawPath = nullptr;
        if (FAILED(SHGetKnownFolderPath(fid, 0, nullptr, &rawPath)) || !rawPath) {
            continue;
        }
        const QString rootDir = QString::fromWCharArray(rawPath);
        CoTaskMemFree(rawPath);

        QDirIterator it(rootDir, { QStringLiteral("*.lnk") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString lnkPath = it.next();
            const QFileInfo fi(lnkPath);
            const QString name = fi.completeBaseName().trimmed();
            const QString lowerName = name.toLower();
            if (name.isEmpty() || isNoiseShortcutName(lowerName) || seenNames.contains(lowerName)) {
                continue;
            }

            QString target, args, workDir, comment, iconSpec;
            if (!resolveShortcut(lnkPath, target, args, workDir, comment, iconSpec)) {
                continue;
            }
            if (target.endsWith(QStringLiteral(".url"), Qt::CaseInsensitive)) {
                continue;
            }

            // Expand target env vars if present and skip broken shortcuts pointing to deleted local files.
            if (!target.isEmpty()) {
                wchar_t expTarget[MAX_PATH * 2] = {};
                const DWORD n = ExpandEnvironmentStringsW(Win32::toStdWString(target).c_str(), expTarget, static_cast<DWORD>(std::size(expTarget)));
                if (n > 0 && n < std::size(expTarget)) {
                    target = QString::fromWCharArray(expTarget);
                }
                if (target.size() >= 3 && target.at(1) == QLatin1Char(':') && !QFileInfo::exists(target)) {
                    continue;
                }
            }

            const QString iconSource = !iconSpec.isEmpty() ? iconSpec : (!target.isEmpty() ? target : lnkPath);
            AppEntry e;
            e.key = QStringLiteral("lnk:") + lnkPath.toLower();
            e.name = name;
            e.comment = comment;
            e.target = QDir::toNativeSeparators(lnkPath); // launching .lnk preserves elevation/appId flags
            e.args = {};
            e.workDir = workDir;
            e.iconUrl = Win32::WinIconProvider::appIconUrl(iconSource);
            e.isUwp = false;
            e.nameLower = lowerName;
            e.commentLower = comment.toLower();

            seenNames.insert(lowerName);
            out.push_back(std::move(e));
        }
    }

    // 2. Scan UWP / Store / packaged apps from FOLDERID_AppsFolder.
    IShellItem* appsFolderItem = nullptr;
    if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&appsFolderItem))) && appsFolderItem) {
        IShellFolder* appsFolder = nullptr;
        if (SUCCEEDED(appsFolderItem->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&appsFolder))) && appsFolder) {
            IEnumIDList* enumList = nullptr;
            if (SUCCEEDED(appsFolder->EnumObjects(nullptr, SHCONTF_NONFOLDERS | SHCONTF_FOLDERS, &enumList)) && enumList) {
                LPITEMIDLIST pidl = nullptr;
                ULONG fetched = 0;
                while (enumList->Next(1, &pidl, &fetched) == S_OK && fetched == 1) {
                    STRRET srName = {};
                    QString dispName;
                    if (SUCCEEDED(appsFolder->GetDisplayNameOf(pidl, SHGDN_NORMAL, &srName))) {
                        wchar_t* buf = nullptr;
                        if (SUCCEEDED(StrRetToStrW(&srName, pidl, &buf)) && buf) {
                            dispName = QString::fromWCharArray(buf).trimmed();
                            CoTaskMemFree(buf);
                        }
                    }

                    STRRET srParsing = {};
                    QString parsingId;
                    if (SUCCEEDED(appsFolder->GetDisplayNameOf(pidl, SHGDN_FORPARSING, &srParsing))) {
                        wchar_t* buf = nullptr;
                        if (SUCCEEDED(StrRetToStrW(&srParsing, pidl, &buf)) && buf) {
                            parsingId = QString::fromWCharArray(buf).trimmed();
                            CoTaskMemFree(buf);
                        }
                    }
                    CoTaskMemFree(pidl);

                    const QString lowerName = dispName.toLower();
                    if (dispName.isEmpty() || parsingId.isEmpty() || isNoiseShortcutName(lowerName) || seenNames.contains(lowerName)) {
                        continue;
                    }

                    const QString appsFolderTarget = QStringLiteral("shell:AppsFolder\\") + parsingId;
                    AppEntry e;
                    e.key = QStringLiteral("uwp:") + parsingId.toLower();
                    e.name = dispName;
                    e.comment = QStringLiteral("Windows App");
                    e.target = appsFolderTarget;
                    e.iconUrl = Win32::WinIconProvider::appIconUrl(appsFolderTarget);
                    e.isUwp = true;
                    e.nameLower = lowerName;
                    e.commentLower = e.comment.toLower();

                    seenNames.insert(lowerName);
                    out.push_back(std::move(e));
                }
                enumList->Release();
            }
            appsFolder->Release();
        }
        appsFolderItem->Release();
    }

    std::sort(out.begin(), out.end(), [](const AppEntry& a, const AppEntry& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

void AppSource::rebuildOrdered() {
    QList<const AppEntry*> ptrs;
    ptrs.reserve(m_entries.size());
    for (const AppEntry& e : m_entries) {
        ptrs.push_back(&e);
    }

    const bool byUsage = m_settings && m_settings->get(QStringLiteral("launcher.order")).toString() == QStringLiteral("used");
    if (byUsage) {
        std::sort(ptrs.begin(), ptrs.end(), [this](const AppEntry* a, const AppEntry* b) {
            const int ua = m_usage.value(a->key, 0);
            const int ub = m_usage.value(b->key, 0);
            if (ua != ub) {
                return ua > ub;
            }
            return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
        });
    }

    QVariantList next;
    next.reserve(ptrs.size());
    for (const AppEntry* e : ptrs) {
        QVariantMap appMap{
            { QStringLiteral("key"), e->key },
            { QStringLiteral("name"), e->name },
            { QStringLiteral("comment"), e->comment },
            { QStringLiteral("target"), e->target },
            { QStringLiteral("args"), e->args },
            { QStringLiteral("workDir"), e->workDir },
            { QStringLiteral("icon"), e->iconUrl },
            { QStringLiteral("isUwp"), e->isUwp }
        };
        QVariantMap row{
            { QStringLiteral("key"), e->key },
            { QStringLiteral("name"), e->name },
            { QStringLiteral("icon"), e->iconUrl },
            { QStringLiteral("app"), appMap }
        };
        next.push_back(row);
    }

    m_ordered = std::move(next);
    Q_EMIT entriesChanged();
}

QVariantList AppSource::rankEntries(const QString& query) const {
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) {
        return m_ordered;
    }

    struct Scored {
        int score;
        int usage;
        QVariantMap row;
    };
    QList<Scored> matches;

    for (const QVariant& v : m_ordered) {
        const QVariantMap row = v.toMap();
        const QVariantMap app = row.value(QStringLiteral("app")).toMap();
        const QString name = row.value(QStringLiteral("name")).toString();
        const QString nameLower = name.toLower();
        const QString commentLower = app.value(QStringLiteral("comment")).toString().toLower();

        int score = 0;
        if (nameLower == q) {
            score = 1000;
        } else if (nameLower.startsWith(q)) {
            score = 700;
        } else if (nameLower.contains(QLatin1Char(' ') + q)) {
            score = 500;
        } else if (nameLower.contains(q)) {
            score = 300;
        } else if (commentLower.contains(q)) {
            score = 120;
        } else {
            // Subsequence match (acronym/fuzzy like "vsc" -> "Visual Studio Code").
            int qi = 0;
            for (int i = 0; i < nameLower.size() && qi < q.size(); ++i) {
                if (nameLower.at(i) == q.at(qi)) {
                    ++qi;
                }
            }
            if (qi == q.size()) {
                score = 80;
            }
        }

        if (score > 0) {
            matches.push_back({ score, m_usage.value(row.value(QStringLiteral("key")).toString(), 0), row });
        }
    }

    std::sort(matches.begin(), matches.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.usage != b.usage) return a.usage > b.usage;
        return a.row.value(QStringLiteral("name")).toString().compare(
            b.row.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });

    QVariantList result;
    result.reserve(matches.size());
    for (const Scored& s : matches) {
        result.push_back(s.row);
    }
    return result;
}

QString AppSource::iconFor(const QString& iconUrl) const {
    return iconUrl;
}

void AppSource::launch(const QVariantMap& app) {
    const QString key = app.value(QStringLiteral("key")).toString();
    const QString target = app.value(QStringLiteral("target")).toString();
    const QString args = app.value(QStringLiteral("args")).toString();
    const QString workDir = app.value(QStringLiteral("workDir")).toString();
    if (target.isEmpty()) {
        return;
    }

    if (!key.isEmpty()) {
        m_usage[key] = m_usage.value(key, 0) + 1;
        saveUsage();
    }

    Win32::launchShellTarget(target, args, workDir, SW_SHOWNORMAL);
}

} // namespace SekaiPop::Services
