// VaultController: oppføringer, kontakter, vedlegg, maler og sertifikater.
// (Hvelvets livssyklus, utklippstavle og påminnelser ligger i vaultcontroller.cpp.)

#include "vaultcontroller.h"

#include "certificateinfo.h"
#include "entryfiltermodel.h"
#include "entrymodel.h"
#include "entrytypes.h"
#include "totp.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QUuid>

#include <climits>

using namespace hvelv;

namespace {

constexpr qint64 MaxAttachmentBytes = 10 * 1024 * 1024;

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QStringList distinctSorted(const QList<Entry> &entries, QString Entry::*member)
{
    QMap<QString, QString> byLower; // dedup uten hensyn til store/små bokstaver
    for (const auto &e : entries) {
        const QString v = (e.*member).trimmed();
        if (!v.isEmpty() && !byLower.contains(v.toLower()))
            byLower.insert(v.toLower(), v);
    }
    QStringList out = byLower.values();
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    return out;
}

QVariantMap contactToMap(const Contact &c, int usage)
{
    QVariantMap m = c.toJson().toVariantMap();
    m[QStringLiteral("usage")] = usage;
    return m;
}

} // namespace

// --- Lister til skjemaer/filtre ---

QVariantList VaultController::contacts() const
{
    QList<Contact> sorted = m_data.contacts;
    std::sort(sorted.begin(), sorted.end(), [](const Contact &a, const Contact &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    QVariantList out;
    for (const auto &c : sorted)
        out << contactToMap(c, m_data.contactUsage(c.id));
    return out;
}

QStringList VaultController::customers() const { return distinctSorted(m_data.entries, &Entry::customer); }
QStringList VaultController::systems() const { return distinctSorted(m_data.entries, &Entry::system); }
QStringList VaultController::owners() const { return distinctSorted(m_data.entries, &Entry::owner); }
QVariantList VaultController::entryTypes() const { return entrytypes::types().toVariantList(); }
QVariantList VaultController::templates() const { return entrytypes::templates().toVariantList(); }

QVariantMap VaultController::entryType(const QString &key) const
{
    return entrytypes::type(key).toVariantMap();
}

// --- Oppføringer ---

Entry *VaultController::mutableEntry(const QString &id)
{
    for (auto &e : m_data.entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

QVariantMap VaultController::newEntry(int templateIndex) const
{
    Entry e = Entry::createNew();
    const QJsonArray all = entrytypes::templates();
    if (templateIndex >= 0 && templateIndex < all.size()) {
        const QJsonObject t = all.at(templateIndex).toObject();
        const QJsonObject p = t[QStringLiteral("preset")].toObject();
        e.type = t[QStringLiteral("type")].toString();
        e.system = p[QStringLiteral("system")].toString();
        e.url = p[QStringLiteral("url")].toString();
        e.twoFactorPlatform = p[QStringLiteral("twoFactorPlatform")].toString();
        e.renewalProcedure = p[QStringLiteral("renewalProcedure")].toString();
        e.reminderDays = p[QStringLiteral("reminderDays")].toInt(e.reminderDays);
        e.fields = p[QStringLiteral("fields")].toObject();
        for (const auto &tag : p[QStringLiteral("tags")].toArray())
            e.tags << tag.toString();
    }
    return e.toJson().toVariantMap();
}

QVariantMap VaultController::entry(const QString &id) const
{
    const Entry *e = m_data.entry(id);
    if (!e)
        return {};
    QVariantMap m = e->toJson(false).toVariantMap(); // vedleggsdata holdes i C++
    const int days = e->daysUntilExpiry();
    m[QStringLiteral("daysLeft")] = days == INT_MIN ? QVariant() : QVariant(days);
    m[QStringLiteral("expiryStatus")] = expiryStatusString(e->expiryStatus());
    QVariantList contacts;
    for (const auto &cid : e->contactIds)
        if (const Contact *c = m_data.contact(cid))
            contacts << contactToMap(*c, m_data.contactUsage(c->id));
    m[QStringLiteral("contacts")] = contacts;
    m[QStringLiteral("typeLabel")] = entrytypes::type(e->type)[QStringLiteral("label")].toString();
    return m;
}

QString VaultController::saveEntry(const QVariantMap &map)
{
    if (!isUnlocked())
        return {};

    QVariantMap clean = map;
    for (const char *k : {"daysLeft", "expiryStatus", "contacts", "typeLabel", "attachments", "passwordHistory"})
        clean.remove(QString::fromLatin1(k));
    Entry e = Entry::fromJson(QJsonObject::fromVariantMap(clean));

    e.title = e.title.trimmed();
    if (e.title.isEmpty()) {
        setError(QStringLiteral("Oppføringen må ha et navn."));
        return {};
    }
    const QString expiresRaw = map.value(QStringLiteral("expiresAt")).toString().trimmed();
    if (!expiresRaw.isEmpty() && !e.expiresAt.isValid()) {
        setError(QStringLiteral("Utløpsdatoen må ha formatet ÅÅÅÅ-MM-DD."));
        return {};
    }
    if (!e.totpSecret.trimmed().isEmpty() && !totp::parse(e.totpSecret)) {
        setError(QStringLiteral("TOTP-hemmeligheten er ikke gyldig base32 eller otpauth://-lenke."));
        return {};
    }
    e.reminderDays = qBound(0, e.reminderDays, 365);
    for (auto &t : e.tags)
        t = t.trimmed();
    e.tags.removeAll(QString());
    for (const auto &key : e.fields.keys())
        if (e.fields.value(key).toString().trimmed().isEmpty())
            e.fields.remove(key);
    e.customFields.removeIf([](const CustomField &f) { return f.name.trimmed().isEmpty() && f.value.isEmpty(); });
    for (auto &f : e.customFields)
        f.name = f.name.trimmed();
    e.contactIds.removeIf([this](const QString &cid) { return !m_data.contact(cid); });
    e.contactIds.removeDuplicates();

    const QList<Entry> before = m_data.entries;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (Entry *existing = mutableEntry(e.id)) {
        e.attachments = existing->attachments; // endres bare via add/removeAttachment
        e.recordPasswordChange(*existing, now);
        e.createdAt = existing->createdAt;
        e.updatedAt = now;
        *existing = e;
    } else {
        e.attachments.clear();
        e.passwordHistory.clear();
        e.passwordChangedAt = e.password.isEmpty() ? QDateTime() : now;
        e.createdAt = now;
        e.updatedAt = now;
        m_data.entries.append(e);
    }

    if (!persist()) {
        m_data.entries = before;
        return {};
    }
    clearError();
    m_model->upsert(e);
    afterDataChange();
    return e.id;
}

bool VaultController::deleteEntry(const QString &id)
{
    if (!isUnlocked())
        return false;
    const QList<Entry> before = m_data.entries;
    if (m_data.entries.removeIf([&](const Entry &e) { return e.id == id; }) == 0)
        return false;
    if (!persist()) {
        m_data.entries = before;
        return false;
    }
    m_model->remove(id);
    afterDataChange();
    emit toast(QStringLiteral("Oppføringen er slettet."));
    return true;
}

void VaultController::afterDataChange()
{
    recount();
    emit dataChanged();
    emit healthChanged();
}

// --- Kontakter ---

QVariantMap VaultController::contact(const QString &id) const
{
    const Contact *c = m_data.contact(id);
    return c ? contactToMap(*c, m_data.contactUsage(id)) : QVariantMap{};
}

QString VaultController::saveContact(const QVariantMap &map)
{
    if (!isUnlocked())
        return {};
    QVariantMap clean = map;
    clean.remove(QStringLiteral("usage"));
    Contact c = Contact::fromJson(QJsonObject::fromVariantMap(clean));
    c.name = c.name.trimmed();
    c.email = c.email.trimmed();
    if (c.name.isEmpty() && c.email.isEmpty()) {
        setError(QStringLiteral("Kontakten må ha navn eller e-post."));
        return {};
    }

    const QList<Contact> before = m_data.contacts;
    bool replaced = false;
    for (auto &existing : m_data.contacts) {
        if (existing.id == c.id) {
            existing = c;
            replaced = true;
        }
    }
    if (!replaced)
        m_data.contacts << c;
    if (!persist()) {
        m_data.contacts = before;
        return {};
    }
    clearError();
    m_model->setContacts(m_data.contacts);
    afterDataChange();
    return c.id;
}

bool VaultController::deleteContact(const QString &id)
{
    if (!isUnlocked())
        return false;
    const VaultData before = m_data;
    if (m_data.contacts.removeIf([&](const Contact &c) { return c.id == id; }) == 0)
        return false;
    for (auto &e : m_data.entries)
        e.contactIds.removeAll(id);
    if (!persist()) {
        m_data = before;
        return false;
    }
    m_model->setContacts(m_data.contacts);
    m_model->setEntries(m_data.entries);
    afterDataChange();
    emit toast(QStringLiteral("Kontakten er slettet."));
    return true;
}

// --- Vedlegg ---

bool VaultController::addAttachment(const QString &entryId, const QUrl &file)
{
    Entry *e = mutableEntry(entryId);
    if (!e)
        return false;
    QFile f(localPath(file));
    if (!f.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Kunne ikke lese filen."));
        return false;
    }
    if (f.size() > MaxAttachmentBytes) {
        setError(QStringLiteral("Vedlegg kan være maks 10 MB."));
        return false;
    }
    Attachment a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    a.name = QFileInfo(f).fileName();
    a.mimeType = QMimeDatabase().mimeTypeForFile(QFileInfo(f)).name();
    a.data = f.readAll();
    a.addedAt = QDateTime::currentDateTimeUtc();

    const Entry previous = *e;
    e->attachments << a;
    e->updatedAt = a.addedAt;
    if (!persist()) {
        *e = previous;
        return false;
    }
    clearError();
    m_model->upsert(*e);
    emit entryChanged(entryId);
    emit toast(QStringLiteral("«%1» er lagt til.").arg(a.name));
    return true;
}

bool VaultController::saveAttachment(const QString &entryId, const QString &attachmentId, const QUrl &target)
{
    const Entry *e = m_data.entry(entryId);
    if (!e)
        return false;
    for (const auto &a : e->attachments) {
        if (a.id != attachmentId)
            continue;
        QSaveFile out(localPath(target));
        if (!out.open(QIODevice::WriteOnly) || out.write(a.data) != a.data.size() || !out.commit()) {
            setError(QStringLiteral("Kunne ikke lagre filen: %1").arg(out.errorString()));
            return false;
        }
        emit toast(QStringLiteral("Lagret «%1» (ukryptert) på disk.").arg(QFileInfo(out.fileName()).fileName()));
        return true;
    }
    return false;
}

bool VaultController::removeAttachment(const QString &entryId, const QString &attachmentId)
{
    Entry *e = mutableEntry(entryId);
    if (!e)
        return false;
    const Entry previous = *e;
    if (e->attachments.removeIf([&](const Attachment &a) { return a.id == attachmentId; }) == 0)
        return false;
    e->updatedAt = QDateTime::currentDateTimeUtc();
    if (!persist()) {
        *e = previous;
        return false;
    }
    m_model->upsert(*e);
    emit entryChanged(entryId);
    return true;
}

// --- Sertifikater ---

QVariantMap VaultController::readCertificate(const QUrl &file, const QString &pfxPassword) const
{
    QFile f(localPath(file));
    if (!f.open(QIODevice::ReadOnly) || f.size() > MaxAttachmentBytes)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Kunne ikke lese filen.")}};
    const CertificateInfo info = ::readCertificate(f.readAll(), pfxPassword);
    if (!info.ok)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), info.error}};
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("subject"), info.subject},
        {QStringLiteral("issuer"), info.issuer},
        {QStringLiteral("thumbprint"), info.thumbprint},
        {QStringLiteral("serialNumber"), info.serialNumber},
        {QStringLiteral("validFrom"), info.validFrom.toLocalTime().date().toString(Qt::ISODate)},
        {QStringLiteral("expiresAt"), info.validTo.toLocalTime().date().toString(Qt::ISODate)},
    };
}
