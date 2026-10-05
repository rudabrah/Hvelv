#pragma once

#include "crypto.h"
#include "vaultstore.h"

#include <QHash>
#include <memory>
#include <optional>

namespace hvelv {

// Hvor en SQL Server-database ligger. Skrives som "sqlserver://<server>/<database>",
// f.eks. "sqlserver://localhost/HvelvPersonlig" eller "sqlserver://(localdb)\MSSQLLocalDB/Hvelv".
struct SqlLocation
{
    QString server;
    QString database;

    QString toString() const;
    static std::optional<SqlLocation> parse(const QString &text);
    static bool isSqlLocation(const QString &text);
    static bool isValidDatabaseName(const QString &name);

    // ODBC-tilkoblingsstreng med Windows-pålogging. Tom hvis ingen SQL Server-driver er installert.
    QString odbcConnectionString(const QString &databaseOverride = {}) const;
};

// Hvelv i en SQL Server-database (skjema i sql/v1.sql). Hemmeligheter krypteres her før de
// sendes til databasen; metadata ligger i klartekst og er bundet til hemmelighetene som AD.
//
// Hver operasjon åpner sin egen tilkobling, så objektet kan brukes fra én tråd om gangen,
// men ikke nødvendigvis den samme (åpning skjer i en arbeidstråd, lagring i GUI-tråden).
class SqlStore final : public VaultStore
{
public:
    static constexpr int SchemaVersion = 1;

    struct OpenResult
    {
        Result result;
        std::shared_ptr<SqlStore> store; // satt når result.ok()
        // Oppføringer/kontakter som ble avvist fordi de er endret utenom Hvelv (titler).
        QStringList rejected;
    };

    // Oppretter databasen (eller tar i bruk en tom eksisterende) og legger inn skjemaet.
    static OpenResult create(const SqlLocation &location, const QByteArray &masterPassword,
                             const crypto::KdfParams &params = {});
    static OpenResult open(const SqlLocation &location, const QByteArray &masterPassword);

    ~SqlStore() override;

    Kind kind() const override { return Kind::SqlServer; }
    QString location() const override { return m_location.toString(); }
    const VaultData &data() const override { return m_data; }

    Result apply(const ChangeSet &changes) override;
    Result changePassword(const QByteArray &current, const QByteArray &next) override;

private:
    SqlStore() = default;

    SqlLocation m_location;
    QByteArray m_databaseId;
    QByteArray m_key;
    int m_keyGeneration = 1;
    VaultData m_data;
    QHash<QString, QByteArray> m_entryRowVer;
    QHash<QString, QByteArray> m_contactRowVer;
};

} // namespace hvelv
