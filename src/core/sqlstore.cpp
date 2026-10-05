#include "sqlstore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QTimeZone>
#include <QUuid>

namespace hvelv {

namespace {

const QString Prefix = QStringLiteral("sqlserver://");
constexpr char KindPersonal[] = "personal";

// Samme grenser som for filer: en manipulert database skal ikke kunne kreve gigabytes minne.
constexpr quint32 MaxMemoryKiB = 1024 * 1024;
constexpr quint32 MaxPasses = 64;
constexpr quint32 MaxLanes = 16;

using Result = VaultStore::Result;
using Error = VaultStore::Error;

QString odbcDriver()
{
#ifdef Q_OS_WIN
    const QSettings drivers(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\ODBC\\ODBCINST.INI\\ODBC Drivers"),
                            QSettings::NativeFormat);
    for (const char *name : {"ODBC Driver 18 for SQL Server", "ODBC Driver 17 for SQL Server"})
        if (drivers.contains(QLatin1String(name)))
            return QLatin1String(name);
    return {};
#else
    return QStringLiteral("ODBC Driver 18 for SQL Server");
#endif
}

bool isLocalServer(const QString &server)
{
    const QString s = server.trimmed().toLower();
    const QString host = s.section(QLatin1Char('\\'), 0, 0).section(QLatin1Char(','), 0, 0);
    return host == QLatin1String("localhost") || host == QLatin1String(".") || host == QLatin1String("(local)")
        || host == QLatin1String("127.0.0.1") || s.startsWith(QLatin1String("(localdb)"))
        || host == QSysInfo::machineHostName().toLower();
}

// ---- Tilkobling og spørringer ----------------------------------------------------------------

// Egen ODBC-tilkobling for én operasjon. Fjernes når objektet går ut av skop, så den aldri
// deles mellom tråder.
class Connection
{
public:
    explicit Connection(const QString &connectionString)
        : m_name(QStringLiteral("hvelv-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QODBC"), m_name);
        db.setDatabaseName(connectionString);
        db.setConnectOptions(QStringLiteral("SQL_ATTR_LOGIN_TIMEOUT=10"));
        if (!db.open())
            m_error = db.lastError();
    }
    ~Connection()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_name, false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    bool isOpen() const { return !m_error.isValid(); }
    QSqlDatabase db() const { return QSqlDatabase::database(m_name, false); }
    QSqlError error() const { return m_error; }

private:
    QString m_name;
    QSqlError m_error;
};

// Fjerner ODBC-prefiksene ("[Microsoft][ODBC Driver 17 for SQL Server][SQL Server]").
QString cleanMessage(const QSqlError &e)
{
    QString text = e.databaseText().isEmpty() ? e.text() : e.databaseText();
    static const QRegularExpression prefix(QStringLiteral("^(\\[[^\\]]*\\]\\s*)+"));
    return text.remove(prefix).trimmed();
}

Result sqlFailure(const QSqlError &e, const QString &context = {})
{
    Result r;
    r.error = e.nativeErrorCode().contains(QLatin1String("50001")) ? Error::Conflict : Error::Io;
    r.message = context.isEmpty() ? cleanMessage(e) : QStringLiteral("%1: %2").arg(context, cleanMessage(e));
    return r;
}

Result connectFailure(const Connection &c, const SqlLocation &loc)
{
    return {Error::Io, QStringLiteral("Kunne ikke koble til %1 på %2: %3")
                           .arg(loc.database, loc.server, cleanMessage(c.error()))};
}

QVariant nullable(const QByteArray &b)
{
    return b.isEmpty() ? QVariant(QMetaType::fromType<QByteArray>()) : QVariant(b);
}

QVariant nullable(const QDate &d)
{
    return d.isValid() ? QVariant(d) : QVariant(QMetaType::fromType<QDate>());
}

QDateTime utc(const QVariant &v)
{
    const QDateTime dt = v.toDateTime();
    return dt.isValid() ? QDateTime(dt.date(), dt.time(), QTimeZone::UTC) : QDateTime();
}

// Kjører EXEC/SQL med posisjonsparametre. Returnerer feil, eller ok.
Result run(QSqlQuery &q, const QString &sql, const QVariantList &args = {})
{
    q.setForwardOnly(true);
    if (!q.prepare(sql))
        return sqlFailure(q.lastError());
    for (const auto &a : args) {
        // En null-QString blir NULL i SQL; tekstkolonnene skal ha tom tekst.
        if (a.typeId() == QMetaType::QString && a.toString().isNull())
            q.addBindValue(QStringLiteral(""));
        else
            q.addBindValue(a);
    }
    if (!q.exec())
        return sqlFailure(q.lastError());
    return {};
}

// ---- Kryptering -----------------------------------------------------------------------------

QByteArray pack(const crypto::Sealed &s)
{
    return s.nonce + s.mac + s.cipherText;
}

std::optional<QByteArray> unpackOpen(const QByteArray &key, const QByteArray &ad, const QByteArray &payload)
{
    constexpr int header = crypto::NonceSize + crypto::MacSize;
    if (payload.size() < header)
        return std::nullopt;
    return crypto::open(key, ad, payload.left(crypto::NonceSize), payload.mid(crypto::NonceSize, crypto::MacSize),
                        payload.mid(header));
}

QByteArray adPrefix(const char *purpose, const QByteArray &databaseId, int keyGeneration)
{
    return QByteArray(purpose) + '\n' + databaseId + '\n' + QByteArray::number(keyGeneration) + '\n';
}

QByteArray keyAd(const QByteArray &databaseId, const crypto::KdfParams &p, const QByteArray &salt)
{
    return QByteArray("hvelv-sql-key-v1\n") + databaseId + '\n'
        + QStringLiteral("argon2id:m=%1,t=%2,p=%3").arg(p.memoryKiB).arg(p.passes).arg(p.lanes).toLatin1() + '\n'
        + salt;
}

QByteArray compact(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Klartekstkolonnene til en oppføring i fast form (QJsonObject sorterer nøklene).
QJsonObject entryMeta(const Entry &e)
{
    return {{"id", e.id},
            {"type", e.type},
            {"title", e.title},
            {"customer", e.customer},
            {"system", e.system},
            {"environment", e.environment},
            {"owner", e.owner},
            {"expiresAt", e.expiresAt.isValid() ? e.expiresAt.toString(Qt::ISODate) : QString()},
            {"reminderDays", e.reminderDays},
            {"reminderChannel", reminderChannelToString(e.reminderChannel)}};
}

QByteArray entryAd(const QByteArray &dbId, int gen, const Entry &e)
{
    return adPrefix("hvelv-entry-v1", dbId, gen) + compact(entryMeta(e));
}

QByteArray contactAd(const QByteArray &dbId, int gen, const Contact &c)
{
    return adPrefix("hvelv-contact-v1", dbId, gen)
        + compact({{"id", c.id}, {"name", c.name}, {"company", c.company}, {"role", c.role},
                   {"email", c.email}, {"phone", c.phone}});
}

QByteArray attachmentAd(const QByteArray &dbId, int gen, const QString &attachmentId, const QString &entryId)
{
    return adPrefix("hvelv-attachment-v1", dbId, gen) + attachmentId.toUtf8() + '\n' + entryId.toUtf8();
}

QByteArray settingsAd(const QByteArray &dbId, int gen)
{
    return adPrefix("hvelv-settings-v1", dbId, gen);
}

// Alt i oppføringen som ikke har egen kolonne/tabell.
QJsonObject entrySecretPart(const Entry &e)
{
    QJsonObject o = e.toJson(false);
    for (const char *key : {"id", "type", "title", "customer", "system", "environment", "owner", "expiresAt",
                            "reminderDays", "reminderChannel", "contactIds", "attachments", "createdAt", "updatedAt"})
        o.remove(QLatin1String(key));
    return o;
}

// En oppføring slik den blir etter en runde gjennom JSON (f.eks. tom type -> "login"),
// så AD-en som lagres er den samme som beregnes ved lesing.
Entry normalized(const Entry &e)
{
    Entry n = Entry::fromJson(e.toJson(true));
    n.createdAt = e.createdAt;
    n.updatedAt = e.updatedAt;
    return n;
}

// ---- Meta ---------------------------------------------------------------------------------

struct Meta
{
    int schemaVersion = 0;
    QString kind;
    QByteArray databaseId;
    crypto::KdfParams params;
    QByteArray salt;
    int keyGeneration = 1;
    QByteArray wrappedKey;
    QByteArray settings;
};

Result readMeta(const QSqlDatabase &db, Meta &m)
{
    QSqlQuery q(db);
    if (Result r = run(q, QStringLiteral("EXEC hv.GetMeta")); !r.ok())
        return {Error::BadFormat, QStringLiteral("Databasen er ikke et Hvelv (%1).").arg(r.message)};
    QHash<QString, QString> v;
    while (q.next())
        v.insert(q.value(0).toString(), q.value(1).toString());

    bool ok = true;
    auto num = [&](const char *key) {
        bool good = false;
        const qint64 n = v.value(QLatin1String(key)).toLongLong(&good);
        ok = ok && good && n >= 0 && n <= UINT32_MAX;
        return quint32(n);
    };
    m.schemaVersion = int(num("SchemaVersion"));
    m.kind = v.value(QStringLiteral("Kind"));
    m.databaseId = v.value(QStringLiteral("DatabaseId")).toLatin1();
    m.params.memoryKiB = num("KdfMemoryKiB");
    m.params.passes = num("KdfPasses");
    m.params.lanes = num("KdfLanes");
    m.salt = QByteArray::fromBase64(v.value(QStringLiteral("KdfSalt")).toLatin1());
    m.keyGeneration = int(num("KeyGeneration"));
    m.wrappedKey = QByteArray::fromBase64(v.value(QStringLiteral("WrappedKey")).toLatin1());
    m.settings = QByteArray::fromBase64(v.value(QStringLiteral("Settings")).toLatin1());
    if (!ok || m.databaseId.isEmpty() || m.salt.size() != crypto::SaltSize
        || v.value(QStringLiteral("KdfAlgorithm")) != QLatin1String("argon2id"))
        return {Error::BadFormat, QStringLiteral("Hvelv-oppsettet i databasen er ufullstendig eller skadet.")};
    if (m.schemaVersion > SqlStore::SchemaVersion)
        return {Error::UnsupportedVersion,
                QStringLiteral("Databasen er laget av en nyere versjon av Hvelv (skjema %1).").arg(m.schemaVersion)};
    if (m.kind != QLatin1String(KindPersonal))
        return {Error::UnsupportedVersion, QStringLiteral("Databasetypen «%1» støttes ikke ennå.").arg(m.kind)};
    if (m.params.memoryKiB < 8 || m.params.memoryKiB > MaxMemoryKiB || m.params.passes < 1
        || m.params.passes > MaxPasses || m.params.lanes < 1 || m.params.lanes > MaxLanes)
        return {Error::BadFormat, QStringLiteral("Ugyldige nøkkelparametre i databasen.")};
    return {};
}

Result setMeta(const QSqlDatabase &db, const QString &name, const QString &value)
{
    QSqlQuery q(db);
    return run(q, QStringLiteral("EXEC hv.SetMeta @Name = ?, @Value = ?"), {name, value});
}

// Nye nøkkelparametre: lagrer salt, parametre og innpakket nøkkel (i en åpen transaksjon).
Result writeKeyMeta(const QSqlDatabase &db, const QByteArray &databaseId, const QByteArray &key,
                    const QByteArray &password, const crypto::KdfParams &params)
{
    const QByteArray salt = crypto::randomBytes(crypto::SaltSize);
    QByteArray kek = crypto::deriveKey(password, salt, params);
    const QByteArray wrapped = pack(crypto::seal(kek, keyAd(databaseId, params, salt), key));
    crypto::wipe(kek);
    const QList<std::pair<QString, QString>> values{
        {QStringLiteral("KdfAlgorithm"), QStringLiteral("argon2id")},
        {QStringLiteral("KdfMemoryKiB"), QString::number(params.memoryKiB)},
        {QStringLiteral("KdfPasses"), QString::number(params.passes)},
        {QStringLiteral("KdfLanes"), QString::number(params.lanes)},
        {QStringLiteral("KdfSalt"), QString::fromLatin1(salt.toBase64())},
        {QStringLiteral("WrappedKey"), QString::fromLatin1(wrapped.toBase64())},
    };
    for (const auto &[name, value] : values)
        if (Result r = setMeta(db, name, value); !r.ok())
            return r;
    return {};
}

std::optional<QByteArray> unwrapKey(const Meta &m, const QByteArray &password)
{
    QByteArray kek = crypto::deriveKey(password, m.salt, m.params);
    auto key = unpackOpen(kek, keyAd(m.databaseId, m.params, m.salt), m.wrappedKey);
    crypto::wipe(kek);
    if (key && key->size() != crypto::KeySize)
        return std::nullopt;
    return key;
}

QStringList scriptBatches(const QString &resource)
{
    QFile f(resource);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    static const QRegularExpression go(QStringLiteral("^\\s*GO\\s*$"),
                                       QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    QStringList batches = QString::fromUtf8(f.readAll()).split(go);
    for (auto &b : batches)
        b = b.trimmed();
    batches.removeAll(QString());
    return batches;
}

void dropDatabase(const SqlLocation &loc)
{
    Connection master(loc.odbcConnectionString(QStringLiteral("master")));
    if (!master.isOpen())
        return;
    QSqlQuery q(master.db());
    const QString name = QStringLiteral("[%1]").arg(loc.database);
    q.exec(QStringLiteral("ALTER DATABASE %1 SET SINGLE_USER WITH ROLLBACK IMMEDIATE").arg(name));
    q.exec(QStringLiteral("DROP DATABASE %1").arg(name));
}

} // namespace

// ---- SqlLocation ----------------------------------------------------------------------------

QString SqlLocation::toString() const
{
    return Prefix + server + QLatin1Char('/') + database;
}

std::optional<SqlLocation> SqlLocation::parse(const QString &text)
{
    if (!isSqlLocation(text))
        return std::nullopt;
    const QString rest = text.mid(Prefix.size());
    const qsizetype slash = rest.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return std::nullopt;
    SqlLocation loc{rest.left(slash).trimmed(), rest.mid(slash + 1).trimmed()};
    if (loc.server.isEmpty() || !isValidDatabaseName(loc.database))
        return std::nullopt;
    return loc;
}

bool SqlLocation::isSqlLocation(const QString &text)
{
    return text.startsWith(Prefix, Qt::CaseInsensitive);
}

bool SqlLocation::isValidDatabaseName(const QString &name)
{
    // Strengere enn SQL Server krever, så navnet trygt kan settes inn i CREATE DATABASE [..].
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_\\-]{0,99}$"));
    return re.match(name).hasMatch();
}

QString SqlLocation::odbcConnectionString(const QString &databaseOverride) const
{
    const QString driver = odbcDriver();
    if (driver.isEmpty())
        return {};
    QString s = QStringLiteral("DRIVER={%1};SERVER=%2;DATABASE=%3;Trusted_Connection=yes;APP=Hvelv;")
                    .arg(driver, server, databaseOverride.isEmpty() ? database : databaseOverride);
    // Driver 18 krever kryptert forbindelse. Lokale servere har som regel selvsignert sertifikat.
    if (driver.contains(QLatin1String("18")))
        s += isLocalServer(server) ? QStringLiteral("Encrypt=yes;TrustServerCertificate=yes;")
                                   : QStringLiteral("Encrypt=yes;");
    return s;
}

// ---- SqlStore -------------------------------------------------------------------------------

SqlStore::~SqlStore()
{
    crypto::wipe(m_key);
}

SqlStore::OpenResult SqlStore::create(const SqlLocation &location, const QByteArray &masterPassword,
                                      const crypto::KdfParams &params)
{
    OpenResult out;
    if (!SqlLocation::isValidDatabaseName(location.database)) {
        out.result = {Error::BadFormat, QStringLiteral("Ugyldig databasenavn. Bruk bokstaver, tall, _ og -.")};
        return out;
    }
    if (location.odbcConnectionString().isEmpty()) {
        out.result = {Error::Io, QStringLiteral("Fant ingen ODBC-driver for SQL Server. "
                                                "Installer «ODBC Driver 18 for SQL Server» fra Microsoft.")};
        return out;
    }
    const QStringList batches = scriptBatches(QStringLiteral(":/sql/v1.sql"));
    if (batches.isEmpty()) {
        out.result = {Error::Io, QStringLiteral("Fant ikke databaseskjemaet i programmet.")};
        return out;
    }

    bool created = false;
    {
        Connection master(location.odbcConnectionString(QStringLiteral("master")));
        if (!master.isOpen()) {
            out.result = connectFailure(master, location);
            return out;
        }
        QSqlQuery q(master.db());
        if (Result r = run(q, QStringLiteral("SELECT DB_ID(?)"), {location.database}); !r.ok()) {
            out.result = r;
            return out;
        }
        const bool exists = q.next() && !q.value(0).isNull();
        q.finish();
        if (!exists) {
            QSqlQuery c(master.db());
            if (!c.exec(QStringLiteral("CREATE DATABASE [%1]").arg(location.database))) {
                out.result = sqlFailure(c.lastError(), QStringLiteral("Kunne ikke opprette databasen"));
                return out;
            }
            created = true;
        }
    }

    auto store = std::shared_ptr<SqlStore>(new SqlStore);
    store->m_location = location;
    store->m_databaseId = QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
    store->m_key = crypto::randomBytes(crypto::KeySize);

    Result result;
    {
        Connection conn(location.odbcConnectionString());
        if (!conn.isOpen()) {
            result = connectFailure(conn, location);
        } else {
            QSqlDatabase db = conn.db();
            QSqlQuery q(db);
            if (Result r = run(q, QStringLiteral("SELECT COUNT(*) FROM sys.schemas WHERE name = 'hv'")); !r.ok())
                result = r;
            else if (q.next() && q.value(0).toInt() > 0)
                result = {Error::Io, QStringLiteral("Databasen inneholder allerede et Hvelv. Velg «Åpne» i stedet.")};
            q.finish();

            for (qsizetype i = 0; result.ok() && i < batches.size(); ++i) {
                QSqlQuery b(db);
                if (!b.exec(batches.at(i)))
                    result = sqlFailure(b.lastError(), QStringLiteral("Feil i databaseskjemaet"));
            }
            if (result.ok()) {
                db.transaction();
                const QList<std::pair<QString, QString>> values{
                    {QStringLiteral("SchemaVersion"), QString::number(SchemaVersion)},
                    {QStringLiteral("Kind"), QLatin1String(KindPersonal)},
                    {QStringLiteral("DatabaseId"), QString::fromLatin1(store->m_databaseId)},
                    {QStringLiteral("KeyGeneration"), QString::number(store->m_keyGeneration)},
                };
                for (const auto &[name, value] : values)
                    if (result.ok())
                        result = setMeta(db, name, value);
                if (result.ok())
                    result = writeKeyMeta(db, store->m_databaseId, store->m_key, masterPassword, params);
                if (result.ok() && !db.commit())
                    result = sqlFailure(db.lastError());
                if (!result.ok())
                    db.rollback();
            }
        }
    }

    if (!result.ok()) {
        if (created)
            dropDatabase(location);
        out.result = result;
        return out;
    }
    out.store = std::move(store);
    return out;
}

SqlStore::OpenResult SqlStore::open(const SqlLocation &location, const QByteArray &masterPassword)
{
    OpenResult out;
    if (location.odbcConnectionString().isEmpty()) {
        out.result = {Error::Io, QStringLiteral("Fant ingen ODBC-driver for SQL Server. "
                                                "Installer «ODBC Driver 18 for SQL Server» fra Microsoft.")};
        return out;
    }
    Connection conn(location.odbcConnectionString());
    if (!conn.isOpen()) {
        out.result = connectFailure(conn, location);
        return out;
    }
    const QSqlDatabase db = conn.db();

    Meta meta;
    if (Result r = readMeta(db, meta); !r.ok()) {
        out.result = r;
        return out;
    }
    const auto key = unwrapKey(meta, masterPassword);
    if (!key) {
        out.result = {Error::WrongPassword, QStringLiteral("Feil hovedpassord.")};
        return out;
    }

    auto store = std::shared_ptr<SqlStore>(new SqlStore);
    store->m_location = location;
    store->m_databaseId = meta.databaseId;
    store->m_key = *key;
    store->m_keyGeneration = meta.keyGeneration;
    const QByteArray &k = store->m_key;
    const QByteArray &dbId = meta.databaseId;
    VaultData &data = store->m_data;

    if (!meta.settings.isEmpty()) {
        const auto plain = unpackOpen(k, settingsAd(dbId, meta.keyGeneration), meta.settings);
        if (plain)
            data.settings = QJsonDocument::fromJson(*plain).object();
        else
            out.rejected << QStringLiteral("Innstillinger");
    }

    // Kontakter
    {
        QSqlQuery q(db);
        if (Result r = run(q, QStringLiteral("EXEC hv.ListContacts")); !r.ok()) {
            out.result = r;
            return out;
        }
        while (q.next()) {
            Contact c;
            c.id = q.value(0).toString();
            c.name = q.value(1).toString();
            c.company = q.value(2).toString();
            c.role = q.value(3).toString();
            c.email = q.value(4).toString();
            c.phone = q.value(5).toString();
            const int gen = q.value(6).toInt();
            const QByteArray payload = q.value(7).toByteArray();
            const auto plain = gen == meta.keyGeneration ? unpackOpen(k, contactAd(dbId, gen, c), payload) : std::nullopt;
            if (!plain) {
                out.rejected << QStringLiteral("Kontakt: %1").arg(c.name.isEmpty() ? c.email : c.name);
                continue;
            }
            c.notes = QJsonDocument::fromJson(*plain).object().value(QLatin1String("notes")).toString();
            data.contacts << c;
            store->m_contactRowVer.insert(c.id, q.value(8).toByteArray());
        }
    }

    // Koblinger og vedlegg (leses før oppføringene, som de hører til)
    QHash<QString, QStringList> links;
    {
        QSqlQuery q(db);
        if (Result r = run(q, QStringLiteral("EXEC hv.ListEntryContacts")); !r.ok()) {
            out.result = r;
            return out;
        }
        while (q.next())
            links[q.value(0).toString()] << q.value(1).toString();
    }
    QHash<QString, QList<Attachment>> attachments;
    QSet<QString> badAttachments;
    {
        QSqlQuery q(db);
        if (Result r = run(q, QStringLiteral("EXEC hv.ListAttachments")); !r.ok()) {
            out.result = r;
            return out;
        }
        while (q.next()) {
            const QString id = q.value(0).toString();
            const QString entryId = q.value(1).toString();
            const int gen = q.value(2).toInt();
            const auto plain = gen == meta.keyGeneration
                ? unpackOpen(k, attachmentAd(dbId, gen, id, entryId), q.value(3).toByteArray())
                : std::nullopt;
            Attachment a = plain ? Attachment::fromJson(QJsonDocument::fromJson(*plain).object()) : Attachment{};
            if (!plain || a.id != id) {
                badAttachments.insert(entryId);
                continue;
            }
            attachments[entryId] << a;
        }
    }

    // Oppføringer
    {
        QSqlQuery q(db);
        if (Result r = run(q, QStringLiteral("EXEC hv.ListEntries")); !r.ok()) {
            out.result = r;
            return out;
        }
        while (q.next()) {
            QJsonObject cols{{"id", q.value(0).toString()},
                             {"type", q.value(1).toString()},
                             {"title", q.value(2).toString()},
                             {"customer", q.value(3).toString()},
                             {"system", q.value(4).toString()},
                             {"environment", q.value(5).toString()},
                             {"owner", q.value(6).toString()},
                             {"expiresAt", q.value(7).isNull() ? QString() : q.value(7).toDate().toString(Qt::ISODate)},
                             {"reminderDays", q.value(8).toInt()},
                             {"reminderChannel", q.value(9).toString()}};
            const int gen = q.value(10).toInt();
            const Entry columnsOnly = Entry::fromJson(cols);
            const auto plain = gen == meta.keyGeneration
                ? unpackOpen(k, entryAd(dbId, gen, columnsOnly), q.value(11).toByteArray())
                : std::nullopt;
            if (!plain || badAttachments.contains(columnsOnly.id)) {
                out.rejected << columnsOnly.title;
                continue;
            }
            QJsonObject o = QJsonDocument::fromJson(*plain).object();
            for (auto it = cols.constBegin(); it != cols.constEnd(); ++it)
                o.insert(it.key(), it.value());
            Entry e = Entry::fromJson(o);
            e.createdAt = utc(q.value(12));
            e.updatedAt = utc(q.value(13));
            e.contactIds = links.value(e.id);
            e.attachments = attachments.value(e.id);
            data.entries << e;
            store->m_entryRowVer.insert(e.id, q.value(14).toByteArray());
        }
    }

    out.store = std::move(store);
    return out;
}

VaultStore::Result SqlStore::apply(const ChangeSet &changes)
{
    if (changes.isEmpty())
        return {};
    Connection conn(m_location.odbcConnectionString());
    if (!conn.isOpen())
        return connectFailure(conn, m_location);
    QSqlDatabase db = conn.db();
    if (!db.transaction())
        return sqlFailure(db.lastError());

    QHash<QString, QByteArray> entryVers;
    QHash<QString, QByteArray> contactVers;
    const auto fail = [&](const Result &r) {
        db.rollback();
        return r;
    };

    for (const Contact &c : changes.upsertContacts) {
        const QByteArray notes = compact({{"notes", c.notes}});
        QSqlQuery q(db);
        const Result r = run(q,
            QStringLiteral("EXEC hv.SaveContact @ContactId = ?, @Name = ?, @Company = ?, @Role = ?, @Email = ?, "
                           "@Phone = ?, @KeyGeneration = ?, @Payload = ?, @ExpectedRowVer = ?"),
            {c.id, c.name, c.company, c.role, c.email, c.phone, m_keyGeneration,
             pack(crypto::seal(m_key, contactAd(m_databaseId, m_keyGeneration, c), notes)),
             nullable(m_contactRowVer.value(c.id))});
        if (!r.ok())
            return fail(r);
        if (q.next())
            contactVers.insert(c.id, q.value(0).toByteArray());
    }

    for (const Entry &original : changes.upsertEntries) {
        const Entry e = normalized(original);
        const QByteArray secret = compact(entrySecretPart(e));
        {
            QSqlQuery q(db);
            const Result r = run(q,
                QStringLiteral("EXEC hv.SaveEntry @EntryId = ?, @EntryType = ?, @Title = ?, @Customer = ?, "
                               "@SystemName = ?, @Environment = ?, @Owner = ?, @ExpiresAt = ?, @ReminderDays = ?, "
                               "@ReminderChannel = ?, @KeyGeneration = ?, @Payload = ?, @CreatedAt = ?, "
                               "@UpdatedAt = ?, @ExpectedRowVer = ?"),
                {e.id, e.type, e.title, e.customer, e.system, e.environment, e.owner, nullable(e.expiresAt),
                 e.reminderDays, reminderChannelToString(e.reminderChannel), m_keyGeneration,
                 pack(crypto::seal(m_key, entryAd(m_databaseId, m_keyGeneration, e), secret)),
                 (e.createdAt.isValid() ? e.createdAt : QDateTime::currentDateTimeUtc()).toUTC(),
                 (e.updatedAt.isValid() ? e.updatedAt : QDateTime::currentDateTimeUtc()).toUTC(),
                 nullable(m_entryRowVer.value(e.id))});
            if (!r.ok())
                return fail(r);
            if (q.next())
                entryVers.insert(e.id, q.value(0).toByteArray());
        }
        {
            QSqlQuery q(db);
            const QString ids = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(e.contactIds)).toJson(QJsonDocument::Compact));
            if (Result r = run(q, QStringLiteral("EXEC hv.SetEntryContacts @EntryId = ?, @ContactIds = ?"), {e.id, ids}); !r.ok())
                return fail(r);
        }
        // Vedlegg endres ikke, så det holder å legge til nye og fjerne de som er borte.
        QSet<QString> before;
        if (const Entry *old = m_data.entry(e.id))
            for (const auto &a : old->attachments)
                before.insert(a.id);
        QSet<QString> after;
        for (const auto &a : e.attachments) {
            after.insert(a.id);
            if (before.contains(a.id))
                continue;
            QSqlQuery q(db);
            const QByteArray plain = compact(a.toJson(true));
            const Result r = run(q,
                QStringLiteral("EXEC hv.AddAttachment @AttachmentId = ?, @EntryId = ?, @KeyGeneration = ?, @Payload = ?"),
                {a.id, e.id, m_keyGeneration,
                 pack(crypto::seal(m_key, attachmentAd(m_databaseId, m_keyGeneration, a.id, e.id), plain))});
            if (!r.ok())
                return fail(r);
        }
        for (const auto &id : std::as_const(before)) {
            if (after.contains(id))
                continue;
            QSqlQuery q(db);
            if (Result r = run(q, QStringLiteral("EXEC hv.DeleteAttachment @AttachmentId = ?"), {id}); !r.ok())
                return fail(r);
        }
    }

    for (const QString &id : changes.deleteEntryIds) {
        QSqlQuery q(db);
        const QByteArray ver = entryVers.contains(id) ? entryVers.value(id) : m_entryRowVer.value(id);
        if (Result r = run(q, QStringLiteral("EXEC hv.DeleteEntry @EntryId = ?, @ExpectedRowVer = ?"), {id, nullable(ver)}); !r.ok())
            return fail(r);
    }

    for (const QString &id : changes.deleteContactIds) {
        QSqlQuery q(db);
        const QByteArray ver = contactVers.contains(id) ? contactVers.value(id) : m_contactRowVer.value(id);
        if (Result r = run(q, QStringLiteral("EXEC hv.DeleteContact @ContactId = ?, @ExpectedRowVer = ?"), {id, nullable(ver)}); !r.ok())
            return fail(r);
    }

    if (changes.settings) {
        const QByteArray payload = pack(crypto::seal(m_key, settingsAd(m_databaseId, m_keyGeneration), compact(*changes.settings)));
        if (Result r = setMeta(db, QStringLiteral("Settings"), QString::fromLatin1(payload.toBase64())); !r.ok())
            return fail(r);
    }

    if (!db.commit())
        return fail(sqlFailure(db.lastError()));

    // Lagret: oppdater kopien i minnet og radversjonene.
    ChangeSet stored = changes;
    for (auto &e : stored.upsertEntries)
        e = normalized(e);
    stored.applyTo(m_data);
    for (auto it = entryVers.constBegin(); it != entryVers.constEnd(); ++it)
        m_entryRowVer.insert(it.key(), it.value());
    for (auto it = contactVers.constBegin(); it != contactVers.constEnd(); ++it)
        m_contactRowVer.insert(it.key(), it.value());
    for (const auto &id : changes.deleteEntryIds)
        m_entryRowVer.remove(id);
    for (const auto &id : changes.deleteContactIds)
        m_contactRowVer.remove(id);
    return {};
}

VaultStore::Result SqlStore::changePassword(const QByteArray &current, const QByteArray &next)
{
    Connection conn(m_location.odbcConnectionString());
    if (!conn.isOpen())
        return connectFailure(conn, m_location);
    QSqlDatabase db = conn.db();
    Meta meta;
    if (Result r = readMeta(db, meta); !r.ok())
        return r;
    auto key = unwrapKey(meta, current);
    if (!key || *key != m_key)
        return {Error::WrongPassword, QStringLiteral("Feil hovedpassord.")};
    crypto::wipe(*key);

    if (!db.transaction())
        return sqlFailure(db.lastError());
    if (Result r = writeKeyMeta(db, m_databaseId, m_key, next, crypto::KdfParams{}); !r.ok()) {
        db.rollback();
        return r;
    }
    if (!db.commit()) {
        const Result r = sqlFailure(db.lastError());
        db.rollback();
        return r;
    }
    return {};
}

} // namespace hvelv
