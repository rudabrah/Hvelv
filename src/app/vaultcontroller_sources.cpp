// VaultController: flere åpne databaser samtidig.
//
// Hoveddatabasen (nøkkel "main") låses opp med hovedpassordet. Tilleggsdatabaser står i
// hoveddatabasens innstillinger ("connections", med passord – kryptert sammen med resten) og
// åpnes automatisk når hoveddatabasen låses opp.
//
// Resten av kontrolleren ser én sammenslått VaultData (data()). Id-er fra hoveddatabasen er
// uendret; id-er fra tilleggsdatabaser får prefikset "<nøkkel>:", så like id-er i to databaser
// (f.eks. en kopi) ikke kolliderer. commit() oversetter tilbake og lagrer i riktig database.

#include "vaultcontroller.h"

#include "entryfiltermodel.h"
#include "entrymodel.h"
#include "sqlstore.h"
#include "storeopen.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QtConcurrent>

using namespace hvelv;

namespace {

const QString ConnectionsKey = QStringLiteral("connections");

QString newUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QString normalizedLocation(const QString &location)
{
    if (const auto loc = SqlLocation::parse(location))
        return loc->toString().toLower();
    return QDir::cleanPath(QFileInfo(location).absoluteFilePath()).toLower();
}

bool sameContact(const Contact &a, const Contact &b)
{
    if (!a.email.trimmed().isEmpty() || !b.email.trimmed().isEmpty())
        return a.email.trimmed().compare(b.email.trimmed(), Qt::CaseInsensitive) == 0;
    return a.name.trimmed().compare(b.name.trimmed(), Qt::CaseInsensitive) == 0;
}

} // namespace

const QString VaultController::MainKey = QStringLiteral("main");

// --- Id-er ---

QString VaultController::sourceKeyOf(const QString &id)
{
    const qsizetype colon = id.indexOf(QLatin1Char(':'));
    return colon > 0 ? id.left(colon) : MainKey;
}

QString VaultController::localIdOf(const QString &id)
{
    const qsizetype colon = id.indexOf(QLatin1Char(':'));
    return colon > 0 ? id.mid(colon + 1) : id;
}

QString VaultController::qualify(const QString &key, const QString &localId)
{
    return key == MainKey ? localId : key + QLatin1Char(':') + localId;
}

// --- Kilder ---

VaultController::Source *VaultController::source(const QString &key)
{
    for (auto &s : m_sources)
        if (s.key == key)
            return &s;
    return nullptr;
}

const VaultController::Source *VaultController::source(const QString &key) const
{
    for (const auto &s : m_sources)
        if (s.key == key)
            return &s;
    return nullptr;
}

QString VaultController::sourceName(const QString &key) const
{
    const Source *s = source(key);
    return s ? s->name : QString();
}

QVariantList VaultController::sources() const
{
    QVariantList out;
    for (const auto &s : m_sources) {
        out << QVariantMap{
            {QStringLiteral("key"), s.key},
            {QStringLiteral("name"), s.name},
            {QStringLiteral("location"), s.location},
            {QStringLiteral("displayLocation"), describeLocation(s.location)},
            {QStringLiteral("isMain"), s.key == MainKey},
            {QStringLiteral("isSql"), SqlLocation::isSqlLocation(s.location)},
            {QStringLiteral("open"), s.store != nullptr},
            {QStringLiteral("opening"), s.opening},
            {QStringLiteral("error"), s.error},
            {QStringLiteral("count"), s.store ? int(s.store->data().entries.size()) : 0},
        };
    }
    return out;
}

int VaultController::openSourceCount() const
{
    int n = 0;
    for (const auto &s : m_sources)
        if (s.store)
            ++n;
    return n;
}

const VaultData &VaultController::data() const
{
    return m_merged;
}

const VaultData *VaultController::localData(const QString &key) const
{
    const Source *s = source(key.isEmpty() ? MainKey : key);
    return s && s->store ? &s->store->data() : nullptr;
}

void VaultController::rebuildMerged()
{
    m_merged = {};
    for (const auto &s : std::as_const(m_sources)) {
        if (!s.store)
            continue;
        const VaultData &d = s.store->data();
        if (s.key == MainKey) {
            m_merged.entries += d.entries;
            m_merged.contacts += d.contacts;
            m_merged.settings = d.settings;
            continue;
        }
        for (Entry e : d.entries) {
            e.id = qualify(s.key, e.id);
            for (auto &cid : e.contactIds)
                cid = qualify(s.key, cid);
            m_merged.entries << e;
        }
        for (Contact c : d.contacts) {
            c.id = qualify(s.key, c.id);
            m_merged.contacts << c;
        }
    }
}

void VaultController::refreshModels()
{
    QHash<QString, QString> names;
    for (const auto &s : std::as_const(m_sources))
        names.insert(s.key, s.name);
    m_model->setSourceNames(names);
    m_model->setContacts(m_merged.contacts);
    m_model->setEntries(m_merged.entries);
    afterDataChange();
    emit sourcesChanged();
}

void VaultController::qualifyChanges(const QString &key, ChangeSet &changes)
{
    for (auto &e : changes.upsertEntries) {
        e.id = qualify(key, e.id);
        for (auto &cid : e.contactIds)
            cid = qualify(key, cid);
    }
    for (auto &id : changes.deleteEntryIds)
        id = qualify(key, id);
    for (auto &c : changes.upsertContacts)
        c.id = qualify(key, c.id);
    for (auto &id : changes.deleteContactIds)
        id = qualify(key, id);
}

bool VaultController::commit(const ChangeSet &changes)
{
    if (!isUnlocked())
        return false;
    if (m_busy) {
        setError(QStringLiteral("Vent til forrige operasjon er ferdig."));
        return false;
    }

    QSet<QString> keys;
    for (const auto &e : changes.upsertEntries)
        keys.insert(sourceKeyOf(e.id));
    for (const auto &id : changes.deleteEntryIds)
        keys.insert(sourceKeyOf(id));
    for (const auto &c : changes.upsertContacts)
        keys.insert(sourceKeyOf(c.id));
    for (const auto &id : changes.deleteContactIds)
        keys.insert(sourceKeyOf(id));
    if (changes.settings)
        keys.insert(MainKey);
    if (keys.isEmpty())
        return true;
    if (keys.size() > 1) {
        setError(QStringLiteral("Endringen gjelder flere databaser og kan ikke lagres samlet."));
        return false;
    }
    Source *src = source(*keys.cbegin());
    if (!src || !src->store) {
        setError(QStringLiteral("Databasen er ikke åpen."));
        return false;
    }

    // Tilbake til id-ene databasen selv bruker. Kontakter fra andre databaser kan ikke kobles.
    ChangeSet local;
    for (Entry e : changes.upsertEntries) {
        e.id = localIdOf(e.id);
        QStringList ids;
        for (const auto &cid : std::as_const(e.contactIds))
            if (sourceKeyOf(cid) == src->key)
                ids << localIdOf(cid);
        e.contactIds = ids;
        local.upsertEntries << e;
    }
    for (const auto &id : changes.deleteEntryIds)
        local.deleteEntryIds << localIdOf(id);
    for (Contact c : changes.upsertContacts) {
        c.id = localIdOf(c.id);
        local.upsertContacts << c;
    }
    for (const auto &id : changes.deleteContactIds)
        local.deleteContactIds << localIdOf(id);
    local.settings = changes.settings;

    const VaultStore::Result r = src->store->apply(local);
    if (!r.ok()) {
        setError(src->key == MainKey ? QStringLiteral("Kunne ikke lagre hvelvet: %1").arg(r.message)
                                     : QStringLiteral("Kunne ikke lagre i «%1»: %2").arg(src->name, r.message));
        return false;
    }
    rebuildMerged();
    return true;
}

// --- Åpning ---

void VaultController::startMain(std::shared_ptr<VaultStore> store)
{
    ++m_generation;
    m_sources.clear();
    Source main;
    main.key = MainKey;
    main.name = QStringLiteral("Personlig");
    main.location = m_path;
    main.store = std::move(store);
    m_sources << main;
    rebuildMerged();
    refreshModels();

    // Tilleggsdatabaser fra innstillingene åpnes i bakgrunnen.
    for (const auto &v : m_merged.settings.value(ConnectionsKey).toArray()) {
        const QJsonObject c = v.toObject();
        Source s;
        s.key = c.value(QStringLiteral("id")).toString();
        s.name = c.value(QStringLiteral("name")).toString();
        s.location = c.value(QStringLiteral("location")).toString();
        if (s.key.isEmpty() || s.key.contains(QLatin1Char(':')) || source(s.key))
            continue;
        m_sources << s;
        openSource(s.key, c.value(QStringLiteral("password")).toString().toUtf8(), false, {});
    }
    emit sourcesChanged();
}

void VaultController::openSource(const QString &key, const QByteArray &password, bool create,
                                 const std::function<void(bool, const QString &)> &done)
{
    Source *s = source(key);
    if (!s)
        return;
    s->opening = true;
    s->error.clear();
    const QString location = s->location;
    const int generation = m_generation;

    auto *watcher = new QFutureWatcher<OpenedStore>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, key, generation, done] {
        OpenedStore r = watcher->result();
        watcher->deleteLater();
        Source *s = source(key);
        if (generation != m_generation || !s)
            return; // låst eller fjernet i mellomtiden
        s->opening = false;
        if (r.result.ok()) {
            s->store = std::move(r.store);
            s->error.clear();
            rebuildMerged();
            refreshModels();
            checkReminders();
            if (!r.rejected.isEmpty())
                emit toast(QStringLiteral("«%1»: %2 oppføring(er) ble ikke lastet fordi de er endret utenom Hvelv.")
                               .arg(s->name).arg(r.rejected.size()));
        } else {
            s->error = r.result.message;
            emit sourcesChanged();
        }
        if (done)
            done(r.result.ok(), r.result.message);
    });
    watcher->setFuture(QtConcurrent::run([location, password, create] { return openStore(location, password, create); }));
}

// Lagrer tilkoblingslisten i hoveddatabasen. fn endrer listen.
bool VaultController::updateConnections(const std::function<void(QJsonArray &)> &fn)
{
    QJsonObject settings = m_merged.settings;
    QJsonArray list = settings.value(ConnectionsKey).toArray();
    fn(list);
    settings.insert(ConnectionsKey, list);
    ChangeSet changes;
    changes.settings = settings;
    return commit(changes);
}

void VaultController::addFileConnection(const QString &name, const QUrl &file, const QString &password, bool create)
{
    QString path = localPath(file);
    if (create && !path.endsWith(QLatin1String(".hvelv"), Qt::CaseInsensitive))
        path += QStringLiteral(".hvelv");
    addConnection(name, path, password, create);
}

void VaultController::addSqlConnection(const QString &name, const QString &server, const QString &database,
                                       const QString &password, bool create)
{
    const SqlLocation loc{server.trimmed(), database.trimmed()};
    if (loc.server.isEmpty() || !SqlLocation::isValidDatabaseName(loc.database)) {
        emit connectionFinished(false, QStringLiteral("Oppgi server og et gyldig databasenavn (bokstaver, tall, _ og -)."));
        return;
    }
    addConnection(name, loc.toString(), password, create);
}

void VaultController::addConnection(const QString &name, const QString &location, const QString &password, bool create)
{
    if (!isUnlocked())
        return;
    if (password.isEmpty()) {
        emit connectionFinished(false, QStringLiteral("Oppgi passordet til databasen."));
        return;
    }
    if (create && password.size() < 10) {
        emit connectionFinished(false, QStringLiteral("Passordet til en ny database må ha minst 10 tegn."));
        return;
    }
    const QString normalized = normalizedLocation(location);
    for (const auto &s : std::as_const(m_sources)) {
        if (normalizedLocation(s.location) == normalized) {
            emit connectionFinished(false, QStringLiteral("«%1» er allerede åpen.").arg(s.name));
            return;
        }
    }

    Source s;
    s.key = newUuid();
    s.name = name.trimmed();
    if (s.name.isEmpty()) {
        const auto sql = SqlLocation::parse(location);
        s.name = sql ? sql->database : QFileInfo(location).completeBaseName();
    }
    if (s.name.isEmpty())
        s.name = QStringLiteral("Database");
    s.location = location;
    m_sources << s;
    emit sourcesChanged();

    const QString key = s.key;
    openSource(key, password.toUtf8(), create, [this, key, password](bool ok, const QString &message) {
        Source *src = source(key);
        if (!src)
            return;
        if (ok) {
            const QString n = src->name;
            const QString loc = src->location;
            if (updateConnections([&](QJsonArray &list) {
                    list.append(QJsonObject{{"id", key}, {"name", n}, {"location", loc}, {"password", password}});
                })) {
                emit connectionFinished(true, QStringLiteral("«%1» er lagt til.").arg(n));
                return;
            }
        }
        const QString why = ok ? m_error : message;
        m_sources.removeIf([&](const Source &x) { return x.key == key; });
        rebuildMerged();
        refreshModels();
        emit connectionFinished(false, why);
    });
}

void VaultController::unlockConnection(const QString &key, const QString &password)
{
    Source *s = source(key);
    if (!s || key == MainKey || s->store || s->opening)
        return;
    openSource(key, password.toUtf8(), false, [this, key, password](bool ok, const QString &message) {
        if (!ok) {
            emit connectionFinished(false, message);
            return;
        }
        // Passordet er byttet et annet sted: husk det nye.
        updateConnections([&](QJsonArray &list) {
            for (int i = 0; i < list.size(); ++i) {
                QJsonObject c = list.at(i).toObject();
                if (c.value(QStringLiteral("id")).toString() == key) {
                    c.insert(QStringLiteral("password"), password);
                    list.replace(i, c);
                }
            }
        });
        emit connectionFinished(true, QStringLiteral("«%1» er låst opp.").arg(sourceName(key)));
    });
}

bool VaultController::removeConnection(const QString &key)
{
    if (key == MainKey || !source(key))
        return false;
    const QString name = sourceName(key);
    if (!updateConnections([&](QJsonArray &list) {
            for (int i = int(list.size()) - 1; i >= 0; --i)
                if (list.at(i).toObject().value(QStringLiteral("id")).toString() == key)
                    list.removeAt(i);
        }))
        return false;
    m_sources.removeIf([&](const Source &s) { return s.key == key; });
    if (m_filter->sourceFilter() == key)
        m_filter->setSourceFilter({});
    rebuildMerged();
    refreshModels();
    emit toast(QStringLiteral("«%1» er fjernet fra listen. Selve databasen er ikke slettet.").arg(name));
    return true;
}

QString VaultController::displayPath(const QUrl &file) const
{
    return QDir::toNativeSeparators(localPath(file));
}

// --- Flytt og kopier mellom databaser ---

QString VaultController::copyEntryTo(const QString &id, const QString &targetKey, bool move)
{
    const Entry *found = data().entry(id);
    const Source *target = source(targetKey);
    const QString fromKey = sourceKeyOf(id);
    if (!found || !target || !target->store || fromKey == targetKey)
        return {};
    const Entry original = *found;

    Entry copy = original;
    const QString wanted = qualify(targetKey, localIdOf(id));
    copy.id = data().entry(wanted) ? qualify(targetKey, newUuid()) : wanted;
    copy.updatedAt = QDateTime::currentDateTimeUtc();

    // Kontaktene: bruk en tilsvarende kontakt i måldatabasen, ellers kopier den.
    ChangeSet changes;
    QStringList contactIds;
    for (const auto &cid : original.contactIds) {
        const Contact *c = data().contact(cid);
        if (!c)
            continue;
        QString match;
        for (const auto &t : data().contacts)
            if (sourceKeyOf(t.id) == targetKey && sameContact(t, *c))
                match = t.id;
        for (const auto &t : std::as_const(changes.upsertContacts))
            if (match.isEmpty() && sameContact(t, *c))
                match = t.id;
        if (match.isEmpty()) {
            Contact nc = *c;
            nc.id = qualify(targetKey, newUuid());
            changes.upsertContacts << nc;
            match = nc.id;
        }
        contactIds << match;
    }
    copy.contactIds = contactIds;
    changes.upsertEntries << copy;
    if (!commit(changes))
        return {};

    const QString targetName = target->name;
    if (move) {
        ChangeSet removal;
        removal.deleteEntryIds << id;
        if (!commit(removal)) {
            refreshModels();
            setError(QStringLiteral("Oppføringen er kopiert til «%1», men kunne ikke fjernes fra «%2»: %3")
                         .arg(targetName, sourceName(fromKey), m_error));
            return copy.id;
        }
    }
    clearError();
    refreshModels();
    emit toast(move ? QStringLiteral("Flyttet til «%1».").arg(targetName)
                    : QStringLiteral("Kopiert til «%1».").arg(targetName));
    return copy.id;
}
