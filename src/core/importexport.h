#pragma once

#include "vaultdata.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace hvelv::importexport {

struct ImportResult
{
    bool ok = false;
    QString error;
    QString format; // menneskelig lesbart navn på formatet som ble gjenkjent
    QList<Entry> entries;
    QList<Contact> contacts;
    QStringList warnings;
};

// Gjenkjenner formatet automatisk:
//  - KeePass / KeePassXC XML-eksport
//  - Bitwarden JSON (ukryptert)
//  - CSV fra Bitwarden, 1Password, KeePassXC, Chrome/Edge, eller generell CSV med kolonnenavn
// (Hvelv-filer importeres i VaultController, siden de krever passord og Argon2.)
ImportResult importData(const QByteArray &data, const QString &fileName = {});

struct MergeOptions
{
    bool skipDuplicates = true; // samme navn + brukernavn + URL + passord
    QString customer;           // overstyr kunde på alle importerte (tom = behold)
    QString tag;                // ekstra tagg på alle importerte (tom = ingen)
};

struct MergeStats
{
    int added = 0;
    int skipped = 0;
    int contactsAdded = 0;
};

// Legger importerte oppføringer og kontakter inn i target med nye id-er.
MergeStats merge(VaultData &target, const ImportResult &imported, const MergeOptions &options = {});

// Ukryptert CSV med alle vanlige felt (ment for å flytte data ut av Hvelv).
QByteArray exportCsv(const VaultData &data);

// RFC 4180-CSV: anførselstegn, doble anførselstegn og linjeskift i felt.
QList<QStringList> parseCsv(const QString &text, QChar delimiter = QLatin1Char(','));

} // namespace hvelv::importexport
