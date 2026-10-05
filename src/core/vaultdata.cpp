#include "vaultdata.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace hvelv {

namespace {

// Format 1 → 2: kontaktfeltene på oppføringen blir felles Contact-objekter.
void migrateLegacyContact(const QJsonObject &raw, Entry &e, QList<Contact> &contacts)
{
    Contact legacy;
    legacy.name = raw["contactName"].toString().trimmed();
    legacy.email = raw["contactEmail"].toString().trimmed();
    legacy.phone = raw["contactPhone"].toString().trimmed();
    legacy.role = raw["contactRole"].toString().trimmed();
    if (legacy.name.isEmpty() && legacy.email.isEmpty() && legacy.phone.isEmpty())
        return;

    for (const auto &c : std::as_const(contacts)) {
        const bool sameEmail = !legacy.email.isEmpty()
                               && c.email.compare(legacy.email, Qt::CaseInsensitive) == 0;
        const bool sameName = legacy.email.isEmpty() && c.email.isEmpty() && !legacy.name.isEmpty()
                              && c.name.compare(legacy.name, Qt::CaseInsensitive) == 0;
        if (sameEmail || sameName) {
            if (!e.contactIds.contains(c.id))
                e.contactIds << c.id;
            return;
        }
    }
    Contact c = Contact::createNew();
    c.name = legacy.name;
    c.email = legacy.email;
    c.phone = legacy.phone;
    c.role = legacy.role;
    c.company = e.customer;
    contacts << c;
    e.contactIds << c.id;
}

} // namespace

QByteArray VaultData::toJson() const
{
    QJsonArray entryArray;
    for (const auto &e : entries)
        entryArray.append(e.toJson());
    QJsonArray contactArray;
    for (const auto &c : contacts)
        contactArray.append(c.toJson());
    QJsonObject root;
    root["format"] = CurrentFormat;
    root["entries"] = entryArray;
    root["contacts"] = contactArray;
    root["settings"] = settings;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool VaultData::fromJson(const QByteArray &json, VaultData &out)
{
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const auto root = doc.object();
    const int format = root["format"].toInt(1);
    if (format > CurrentFormat)
        return false;

    out = {};
    for (const auto &v : root["contacts"].toArray())
        out.contacts << Contact::fromJson(v.toObject());
    for (const auto &v : root["entries"].toArray()) {
        const QJsonObject raw = v.toObject();
        Entry e = Entry::fromJson(raw);
        if (format < 2)
            migrateLegacyContact(raw, e, out.contacts);
        out.entries << e;
    }
    out.settings = root["settings"].toObject();
    return true;
}

const Entry *VaultData::entry(const QString &id) const
{
    for (const auto &e : entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

const Contact *VaultData::contact(const QString &id) const
{
    for (const auto &c : contacts)
        if (c.id == id)
            return &c;
    return nullptr;
}

int VaultData::contactUsage(const QString &contactId) const
{
    int n = 0;
    for (const auto &e : entries)
        if (e.contactIds.contains(contactId))
            ++n;
    return n;
}

} // namespace hvelv
