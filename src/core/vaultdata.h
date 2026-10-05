#pragma once

#include "entry.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>

namespace hvelv {

// Innholdet i et hvelv (det som krypteres).
//
// JSON-format 2: { format: 2, entries: [...], contacts: [...], settings: {...} }
// Format 1 hadde kontaktperson som felt på hver oppføring; de flyttes til
// felles kontakter ved lesing (like kontakter slås sammen på e-post, ellers navn).
struct VaultData
{
    static constexpr int CurrentFormat = 2;

    QList<Entry> entries;
    QList<Contact> contacts;
    QJsonObject settings;

    QByteArray toJson() const;
    static bool fromJson(const QByteArray &json, VaultData &out);

    const Entry *entry(const QString &id) const;
    const Contact *contact(const QString &id) const;
    int contactUsage(const QString &contactId) const;
};

} // namespace hvelv
