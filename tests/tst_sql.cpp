// Tester SqlStore mot en ekte SQL Server. Kjører bare når HVELV_SQL_SERVER er satt
// (f.eks. "localhost"); ellers hoppes de over. Hver test lager sin egen database
// HvelvTest_<tilfeldig> og sletter den etterpå. Andre databaser røres ikke.
#include "sqlstore.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QtTest>

using namespace hvelv;

namespace {

const crypto::KdfParams FastKdf{64, 1, 1};

// Rå SQL mot testdatabasen, for å se hva som faktisk ligger der og for å tukle med den.
class RawSql
{
public:
    explicit RawSql(const SqlLocation &loc)
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QODBC"), m_name);
        db.setDatabaseName(loc.odbcConnectionString());
        m_ok = db.open();
    }
    ~RawSql()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_name, false);
            db.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }
    bool ok() const { return m_ok; }
    bool exec(const QString &sql)
    {
        QSqlQuery q(QSqlDatabase::database(m_name, false));
        const bool ok = q.exec(sql);
        if (!ok)
            qWarning() << q.lastError().text();
        return ok;
    }
    // Alle verdier i en tabell som tekst/bytes, for å lete etter klartekst.
    QByteArrayList allValues(const QString &table)
    {
        QByteArrayList out;
        QSqlQuery q(QSqlDatabase::database(m_name, false));
        if (!q.exec(QStringLiteral("SELECT * FROM %1").arg(table)))
            return out;
        while (q.next())
            for (int i = 0; i < q.record().count(); ++i) {
                const QVariant v = q.value(i);
                out << (v.typeId() == QMetaType::QByteArray ? v.toByteArray() : v.toString().toUtf8());
            }
        return out;
    }

private:
    QString m_name = QStringLiteral("raw-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool m_ok = false;
};

bool anyContains(const QByteArrayList &values, const QByteArray &needle)
{
    const QByteArray utf16(reinterpret_cast<const char *>(QString::fromUtf8(needle).utf16()), needle.size() * 2);
    for (const auto &v : values)
        if (v.contains(needle) || v.contains(utf16))
            return true;
    return false;
}

Entry sampleEntry(const QString &title)
{
    Entry e = Entry::createNew();
    e.title = title;
    e.type = QStringLiteral("api");
    e.customer = QStringLiteral("Kunde AS");
    e.system = QStringLiteral("Azure");
    e.environment = QStringLiteral("prod");
    e.owner = QStringLiteral("Ola");
    e.url = QStringLiteral("https://portal.example.com");
    e.username = QStringLiteral("app-id");
    e.password = QStringLiteral("SuperHemmelig-%1").arg(title);
    e.totpSecret = QStringLiteral("JBSWY3DPEHPK3PXP");
    e.tags = {QStringLiteral("a"), QStringLiteral("b")};
    e.expiresAt = QDate(2027, 3, 1);
    e.reminderDays = 30;
    e.reminderChannel = ReminderChannel::Both;
    e.notes = QStringLiteral("Notat æøå");
    e.renewalProcedure = QStringLiteral("Forny i portalen");
    e.customFields << CustomField{QStringLiteral("Tenant"), QStringLiteral("t-123"), true};
    e.fields.insert(QStringLiteral("clientId"), QStringLiteral("cid-1"));
    e.passwordHistory << PasswordHistoryItem{QStringLiteral("gammelt"), QDateTime::currentDateTimeUtc()};
    return e;
}

} // namespace

class TestSql : public QObject
{
    Q_OBJECT

    SqlLocation m_loc;

    SqlStore::OpenResult createStore(const QByteArray &pw = "pw")
    {
        return SqlStore::create(m_loc, pw, FastKdf);
    }

private slots:
    void locationParsing()
    {
        const auto loc = SqlLocation::parse(QStringLiteral("sqlserver://(localdb)\\MSSQLLocalDB/Hvelv_1"));
        QVERIFY(loc);
        QCOMPARE(loc->server, QStringLiteral("(localdb)\\MSSQLLocalDB"));
        QCOMPARE(loc->database, QStringLiteral("Hvelv_1"));
        QCOMPARE(loc->toString(), QStringLiteral("sqlserver://(localdb)\\MSSQLLocalDB/Hvelv_1"));
        QVERIFY(SqlLocation::isSqlLocation(QStringLiteral("SQLSERVER://x/y")));
        QVERIFY(!SqlLocation::isSqlLocation(QStringLiteral("C:/x.hvelv")));
        QVERIFY(!SqlLocation::parse(QStringLiteral("sqlserver://localhost/")));
        QVERIFY(!SqlLocation::parse(QStringLiteral("sqlserver:///db")));
        QVERIFY(!SqlLocation::parse(QStringLiteral("sqlserver://localhost/x];DROP DATABASE y;--")));
        QVERIFY(!SqlLocation::isValidDatabaseName(QStringLiteral("navn med mellomrom")));
        QVERIFY(SqlLocation::isValidDatabaseName(QStringLiteral("Hvelv-Personlig_2")));
    }

    void init()
    {
        const QString server = qEnvironmentVariable("HVELV_SQL_SERVER");
        if (server.isEmpty() && QString::fromLatin1(QTest::currentTestFunction()) != QLatin1String("locationParsing"))
            QSKIP("Sett HVELV_SQL_SERVER (f.eks. localhost) for å kjøre SQL-testene.");
        m_loc = {server, QStringLiteral("HvelvTest_") + QUuid::createUuid().toString(QUuid::Id128).left(12)};
    }

    void cleanup()
    {
        if (m_loc.server.isEmpty())
            return;
        // Slett testdatabasen (bare HvelvTest_-databaser).
        QVERIFY(m_loc.database.startsWith(QLatin1String("HvelvTest_")));
        SqlLocation master = m_loc;
        master.database = QStringLiteral("master");
        RawSql sql(master);
        sql.exec(QStringLiteral("IF DB_ID('%1') IS NOT NULL BEGIN "
                                "ALTER DATABASE [%1] SET SINGLE_USER WITH ROLLBACK IMMEDIATE; "
                                "DROP DATABASE [%1]; END").arg(m_loc.database));
    }

    void createOpenRoundTrip()
    {
        auto created = createStore();
        QVERIFY2(created.result.ok(), qPrintable(created.result.message));
        QCOMPARE(created.store->kind(), VaultStore::Kind::SqlServer);
        QCOMPARE(created.store->location(), m_loc.toString());
        QVERIFY(created.store->data().entries.isEmpty());

        Contact c = Contact::createNew();
        c.name = QStringLiteral("Kari Nordmann");
        c.email = QStringLiteral("kari@kunde.no");
        c.notes = QStringLiteral("Ring før 10");
        Entry e = sampleEntry(QStringLiteral("App-registrering"));
        e.contactIds = {c.id};
        Attachment a;
        a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        a.name = QStringLiteral("cert.pfx");
        a.mimeType = QStringLiteral("application/x-pkcs12");
        a.data = QByteArray(200 * 1024, 'x') + QByteArray("slutt");
        a.addedAt = QDateTime::currentDateTimeUtc();
        e.attachments << a;
        Entry plain = Entry::createNew();
        plain.title = QStringLiteral("Uten noe");
        plain.type.clear(); // tom type normaliseres til login

        const auto r = created.store->apply({.upsertEntries = {e, plain}, .upsertContacts = {c}});
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(int(created.store->data().entries.size()), 2);
        QCOMPARE(createStore().result.error, VaultStore::Error::Io); // finnes allerede
        created.store.reset();

        auto opened = SqlStore::open(m_loc, "pw");
        QVERIFY2(opened.result.ok(), qPrintable(opened.result.message));
        QVERIFY(opened.rejected.isEmpty());
        const VaultData &d = opened.store->data();
        QCOMPARE(int(d.entries.size()), 2);
        QCOMPARE(int(d.contacts.size()), 1);
        QCOMPARE(d.contacts.first().notes, c.notes);
        QCOMPARE(d.contacts.first().email, c.email);

        const Entry *got = d.entry(e.id);
        QVERIFY(got);
        QCOMPARE(got->title, e.title);
        QCOMPARE(got->type, e.type);
        QCOMPARE(got->customer, e.customer);
        QCOMPARE(got->system, e.system);
        QCOMPARE(got->environment, e.environment);
        QCOMPARE(got->owner, e.owner);
        QCOMPARE(got->url, e.url);
        QCOMPARE(got->username, e.username);
        QCOMPARE(got->password, e.password);
        QCOMPARE(got->totpSecret, e.totpSecret);
        QCOMPARE(got->tags, e.tags);
        QCOMPARE(got->expiresAt, e.expiresAt);
        QCOMPARE(got->reminderDays, 30);
        QCOMPARE(got->reminderChannel, ReminderChannel::Both);
        QCOMPARE(got->notes, e.notes);
        QCOMPARE(got->renewalProcedure, e.renewalProcedure);
        QCOMPARE(got->fields, e.fields);
        QCOMPARE(int(got->customFields.size()), 1);
        QCOMPARE(got->customFields.first().value, QStringLiteral("t-123"));
        QCOMPARE(int(got->passwordHistory.size()), 1);
        QCOMPARE(got->contactIds, QStringList{c.id});
        QCOMPARE(got->createdAt.toMSecsSinceEpoch(), e.createdAt.toMSecsSinceEpoch());
        QCOMPARE(got->createdAt.timeSpec(), Qt::UTC);
        QCOMPARE(int(got->attachments.size()), 1);
        QCOMPARE(got->attachments.first().data, a.data);
        QCOMPARE(got->attachments.first().name, a.name);
        QCOMPARE(d.entry(plain.id)->type, QStringLiteral("login"));

        QCOMPARE(SqlStore::open(m_loc, "feil").result.error, VaultStore::Error::WrongPassword);
    }

    void secretsAreNotInPlaintext()
    {
        auto created = createStore();
        QVERIFY2(created.result.ok(), qPrintable(created.result.message));
        Contact c = Contact::createNew();
        c.name = QStringLiteral("Per");
        c.notes = QStringLiteral("HemmeligKontaktnotat");
        Entry e = sampleEntry(QStringLiteral("Synlig tittel"));
        QVERIFY(created.store->apply({.upsertEntries = {e}, .upsertContacts = {c}}).ok());

        RawSql sql(m_loc);
        QVERIFY(sql.ok());
        const QByteArrayList entries = sql.allValues(QStringLiteral("hv.Entries"));
        QVERIFY(anyContains(entries, "Synlig tittel")); // metadata er lesbart i SSMS
        QVERIFY(anyContains(entries, "Kunde AS"));
        for (const QByteArray secret : {e.password.toUtf8(), e.username.toUtf8(), e.totpSecret.toUtf8(),
                                         e.url.toUtf8(), QByteArray("t-123"), QByteArray("gammelt")})
            QVERIFY2(!anyContains(entries, secret), secret.constData());
        QVERIFY(!anyContains(sql.allValues(QStringLiteral("hv.Contacts")), "HemmeligKontaktnotat"));
    }

    void tamperingIsRejected()
    {
        auto created = createStore();
        QVERIFY2(created.result.ok(), qPrintable(created.result.message));
        Entry a = sampleEntry(QStringLiteral("A"));
        Entry b = sampleEntry(QStringLiteral("B"));
        Entry c = sampleEntry(QStringLiteral("C"));
        QVERIFY(created.store->apply({.upsertEntries = {a, b, c}}).ok());

        RawSql sql(m_loc);
        // Endret tittel direkte i databasen
        QVERIFY(sql.exec(QStringLiteral("UPDATE hv.Entries SET Title = N'Falsk' WHERE EntryId = '%1'").arg(a.id)));
        // Byttet hemmeligheten til en annen oppføring inn
        QVERIFY(sql.exec(QStringLiteral("UPDATE hv.Entries SET Payload = (SELECT Payload FROM hv.Entries WHERE EntryId = '%1') "
                                        "WHERE EntryId = '%2'").arg(c.id, b.id)));

        auto opened = SqlStore::open(m_loc, "pw");
        QVERIFY(opened.result.ok());
        QStringList rejected = opened.rejected;
        rejected.sort();
        QCOMPARE(rejected, (QStringList{QStringLiteral("B"), QStringLiteral("Falsk")}));
        QCOMPARE(int(opened.store->data().entries.size()), 1);
        QCOMPARE(opened.store->data().entries.first().password, c.password);
    }

    void conflictIsDetected()
    {
        auto created = createStore();
        QVERIFY2(created.result.ok(), qPrintable(created.result.message));
        Entry e = sampleEntry(QStringLiteral("Delt"));
        QVERIFY(created.store->apply({.upsertEntries = {e}}).ok());

        auto other = SqlStore::open(m_loc, "pw");
        QVERIFY(other.result.ok());

        Entry first = e;
        first.password = QStringLiteral("fra-A");
        QVERIFY(created.store->apply({.upsertEntries = {first}}).ok());

        Entry second = e;
        second.password = QStringLiteral("fra-B");
        const auto r = other.store->apply({.upsertEntries = {second}});
        QCOMPARE(r.error, VaultStore::Error::Conflict);
        QVERIFY(!r.message.isEmpty());
        QCOMPARE(other.store->data().entry(e.id)->password, e.password); // uendret i minnet
        QCOMPARE(other.store->apply({.deleteEntryIds = {e.id}}).error, VaultStore::Error::Conflict);

        // Den som lagret først kan fortsette å lagre.
        first.notes = QStringLiteral("videre");
        QVERIFY(created.store->apply({.upsertEntries = {first}}).ok());
        QCOMPARE(SqlStore::open(m_loc, "pw").store->data().entry(e.id)->password, QStringLiteral("fra-A"));
    }

    void deletesAndLinks()
    {
        auto created = createStore();
        QVERIFY2(created.result.ok(), qPrintable(created.result.message));
        auto &store = created.store;
        Contact c = Contact::createNew();
        c.name = QStringLiteral("Kontakt");
        Entry a = sampleEntry(QStringLiteral("A"));
        a.contactIds = {c.id};
        Attachment att;
        att.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        att.name = QStringLiteral("x.txt");
        att.data = "innhold";
        a.attachments << att;
        Entry b = sampleEntry(QStringLiteral("B"));
        QVERIFY(store->apply({.upsertEntries = {a, b}, .upsertContacts = {c}}).ok());

        a.attachments.clear();
        QVERIFY(store->apply({.upsertEntries = {a}}).ok());                 // fjerner vedlegget
        QVERIFY(store->apply({.deleteContactIds = {c.id}}).ok());           // fjerner koblingen
        QVERIFY(store->apply({.deleteEntryIds = {b.id}}).ok());
        QVERIFY(store->data().entry(a.id)->contactIds.isEmpty());

        auto opened = SqlStore::open(m_loc, "pw");
        QVERIFY(opened.result.ok());
        const VaultData &d = opened.store->data();
        QCOMPARE(int(d.entries.size()), 1);
        QVERIFY(d.entries.first().attachments.isEmpty());
        QVERIFY(d.entries.first().contactIds.isEmpty());
        QVERIFY(d.contacts.isEmpty());

        RawSql sql(m_loc);
        QVERIFY(!sql.allValues(QStringLiteral("hv.Entries")).contains("B")); // slettet rad er tømt
        QVERIFY(sql.allValues(QStringLiteral("hv.Attachments")).isEmpty());
    }

    void failedChangeSetRollsBack()
    {
        auto created = createStore();
        QVERIFY2(created.result.ok(), qPrintable(created.result.message));
        Entry ok = sampleEntry(QStringLiteral("Ok"));
        Entry broken = sampleEntry(QStringLiteral("Brutt"));
        broken.contactIds = {QStringLiteral("finnes-ikke")}; // fremmednøkkel feiler
        const auto r = created.store->apply({.upsertEntries = {ok, broken}});
        QVERIFY(!r.ok());
        QVERIFY(created.store->data().entries.isEmpty());

        auto opened = SqlStore::open(m_loc, "pw");
        QVERIFY(opened.result.ok());
        QVERIFY(opened.store->data().entries.isEmpty()); // ingenting ble lagret
        QVERIFY(created.store->apply({.upsertEntries = {ok}}).ok()); // og lagring virker etterpå
    }

    void changePassword()
    {
        auto created = createStore("gammelt");
        QVERIFY2(created.result.ok(), qPrintable(created.result.message));
        QVERIFY(created.store->apply({.upsertEntries = {sampleEntry(QStringLiteral("X"))}}).ok());
        QCOMPARE(created.store->changePassword("feil", "nytt").error, VaultStore::Error::WrongPassword);
        QVERIFY(created.store->changePassword("gammelt", "nytt").ok());
        QCOMPARE(SqlStore::open(m_loc, "gammelt").result.error, VaultStore::Error::WrongPassword);
        auto opened = SqlStore::open(m_loc, "nytt");
        QVERIFY2(opened.result.ok(), qPrintable(opened.result.message));
        QCOMPARE(int(opened.store->data().entries.size()), 1);
    }

    void openingOtherDatabasesFails()
    {
        SqlLocation master = m_loc;
        master.database = QStringLiteral("master");
        QCOMPARE(SqlStore::open(master, "pw").result.error, VaultStore::Error::BadFormat);
        // Databasen finnes ikke
        QCOMPARE(SqlStore::open(m_loc, "pw").result.error, VaultStore::Error::Io);
        SqlLocation badServer = m_loc;
        badServer.server = QStringLiteral("finnes-ikke.invalid");
        QCOMPARE(SqlStore::open(badServer, "pw").result.error, VaultStore::Error::Io);
    }
};

QTEST_GUILESS_MAIN(TestSql)
#include "tst_sql.moc"
