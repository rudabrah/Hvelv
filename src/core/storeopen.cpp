#include "storeopen.h"

#include "filestore.h"
#include "sqlstore.h"

#include <QDir>

namespace hvelv {

OpenedStore openStore(const QString &location, const QByteArray &password, bool create)
{
    OpenedStore o;
    if (SqlLocation::isSqlLocation(location)) {
        const auto loc = SqlLocation::parse(location);
        if (!loc) {
            o.result = {VaultStore::Error::BadFormat, QStringLiteral("Ugyldig SQL Server-adresse: %1").arg(location)};
            return o;
        }
        auto r = create ? SqlStore::create(*loc, password) : SqlStore::open(*loc, password);
        o.result = r.result;
        o.store = std::move(r.store);
        o.rejected = r.rejected;
    } else {
        auto r = create ? FileStore::create(location, password) : FileStore::open(location, password);
        o.result = r.result;
        o.store = std::move(r.store);
    }
    return o;
}

QString describeLocation(const QString &location)
{
    if (const auto loc = SqlLocation::parse(location))
        return QStringLiteral("%1 på %2 (SQL Server)").arg(loc->database, loc->server);
    return QDir::toNativeSeparators(location);
}

} // namespace hvelv
