#pragma once

#include "vaultdata.h"

#include <QJsonObject>
#include <QStringList>
#include <optional>

namespace hvelv {

// Endringer som lagres samlet: alt eller ingenting.
struct ChangeSet
{
    QList<Entry> upsertEntries;     // erstatter oppføring med samme id, ellers ny
    QStringList deleteEntryIds;
    QList<Contact> upsertContacts;
    QStringList deleteContactIds;   // fjerner også koblingen fra oppføringene
    std::optional<QJsonObject> settings;

    bool isEmpty() const
    {
        return upsertEntries.isEmpty() && deleteEntryIds.isEmpty() && upsertContacts.isEmpty()
            && deleteContactIds.isEmpty() && !settings;
    }

    // Brukes av lagringer som holder hele innholdet i minnet.
    void applyTo(VaultData &data) const;
};

// Et åpent hvelv, uavhengig av hvor det ligger (fil nå, SQL Server i fase 2).
// Innholdet holdes i minnet; apply() lagrer og oppdaterer det bare hvis lagringen lyktes.
// Ikke trådsikker: kall fra én tråd om gangen.
class VaultStore
{
public:
    enum class Kind { File };
    enum class Error { None, WrongPassword, Io, BadFormat, UnsupportedVersion, Corrupt, Busy };

    struct Result
    {
        Error error = Error::None;
        QString message;
        bool ok() const { return error == Error::None; }
    };

    virtual ~VaultStore() = default;

    virtual Kind kind() const = 0;
    virtual QString location() const = 0; // filsti, senere server/database
    virtual const VaultData &data() const = 0;

    virtual Result apply(const ChangeSet &changes) = 0;
    // Treg (Argon2): kjør utenfor GUI-tråden. Bekrefter nåværende passord først.
    virtual Result changePassword(const QByteArray &current, const QByteArray &next) = 0;
};

} // namespace hvelv
