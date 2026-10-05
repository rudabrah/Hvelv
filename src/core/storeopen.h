#pragma once

#include "vaultstore.h"

#include <memory>

namespace hvelv {

struct OpenedStore
{
    VaultStore::Result result;
    std::shared_ptr<VaultStore> store; // satt når result.ok()
    QStringList rejected;              // endret utenom Hvelv (bare SQL)
};

// Åpner eller oppretter et hvelv ut fra plasseringen: filsti eller "sqlserver://server/database".
// Treg (Argon2): kjør utenfor GUI-tråden.
OpenedStore openStore(const QString &location, const QByteArray &password, bool create);

// Lesbar beskrivelse av plasseringen, f.eks. "Hvelv på localhost (SQL Server)".
QString describeLocation(const QString &location);

} // namespace hvelv
