#include "autotype.h"
#include "calendar.h"
#include "crypto.h"
#include "entry.h"
#include "entrytypes.h"
#include "filestore.h"
#include "health.h"
#include "passwordgenerator.h"
#include "totp.h"
#include "vaultfile.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

using namespace hvelv;

namespace {
// Lave parametre så testene går raskt. Appen bruker standardverdiene (64 MiB).
const crypto::KdfParams FastKdf{64, 1, 1};
} // namespace

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void sealOpenRoundTrip()
    {
        const auto key = crypto::randomBytes(crypto::KeySize);
        const auto sealed = crypto::seal(key, "ad", "hemmelig");
        auto plain = crypto::open(key, "ad", sealed.nonce, sealed.mac, sealed.cipherText);
        QVERIFY(plain);
        QCOMPARE(*plain, QByteArray("hemmelig"));

        QVERIFY(!crypto::open(key, "annen-ad", sealed.nonce, sealed.mac, sealed.cipherText));
        auto tampered = sealed.cipherText;
        tampered[0] = char(tampered[0] ^ 1);
        QVERIFY(!crypto::open(key, "ad", sealed.nonce, sealed.mac, tampered));
    }

    void argon2idIsDeterministic()
    {
        const auto salt = QByteArray(crypto::SaltSize, 'x');
        const auto a = crypto::deriveKey("passord", salt, FastKdf);
        const auto b = crypto::deriveKey("passord", salt, FastKdf);
        const auto c = crypto::deriveKey("Passord", salt, FastKdf);
        QCOMPARE(int(a.size()), crypto::KeySize);
        QCOMPARE(a, b);
        QVERIFY(a != c);
    }

    void vaultRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("test.hvelv");

        Entry e = Entry::createNew();
        e.title = "Visma.net";
        e.username = "rudi";
        e.password = "S3cret!";
        e.type = "api";
        e.customer = "Kunde A";
        e.system = "Visma.net";
        e.environment = "prod";
        e.fields = QJsonObject{{"clientId", "abc-123"}, {"tenantId", "t-1"}};
        e.customFields = {{"Avtalenr", "42", false}, {"PIN", "1234", true}};
        e.renewalProcedure = "Ring Kari";
        Contact kari = Contact::createNew();
        kari.name = "Kari Nordmann";
        kari.email = "kari@leverandor.no";
        e.contactIds = {kari.id};
        e.attachments = {{"a1", "cert.cer", "application/pkix-cert", QByteArray::fromHex("0001ff10c3a6"), QDateTime::currentDateTimeUtc()}};
        e.twoFactorPlatform = "Microsoft Authenticator";
        e.expiresAt = QDate(2026, 12, 31);
        e.reminderChannel = ReminderChannel::Both;
        e.tags = {"ERP", "Kunde A"};

        VaultData data;
        data.entries << e;
        data.contacts << kari;

        auto vf = VaultFile::create(path, "korrekt hest", FastKdf);
        QCOMPARE(vf.save(data), VaultFile::Status::Ok);

        auto r = VaultFile::open(path, "korrekt hest");
        QCOMPARE(r.status, VaultFile::Status::Ok);
        QCOMPARE(int(r.data.entries.size()), 1);
        const Entry &got = r.data.entries.first();
        QCOMPARE(got.id, e.id);
        QCOMPARE(got.password, e.password);
        QCOMPARE(got.type, QString("api"));
        QCOMPARE(got.system, e.system);
        QCOMPARE(got.environment, e.environment);
        QCOMPARE(got.fields, e.fields);
        QCOMPARE(int(got.customFields.size()), 2);
        QVERIFY(got.customFields.at(1).secret);
        QCOMPARE(got.renewalProcedure, e.renewalProcedure);
        QCOMPARE(got.contactIds, e.contactIds);
        QCOMPARE(int(r.data.contacts.size()), 1);
        QCOMPARE(r.data.contact(kari.id)->email, kari.email);
        QCOMPARE(r.data.contactUsage(kari.id), 1);
        QCOMPARE(int(got.attachments.size()), 1);
        QCOMPARE(got.attachments.first().data, e.attachments.first().data);
        QCOMPARE(got.expiresAt, e.expiresAt);
        QCOMPARE(got.reminderChannel, ReminderChannel::Both);
        QCOMPARE(got.tags, e.tags);

        QCOMPARE(VaultFile::open(path, "feil").status, VaultFile::Status::WrongPassword);
    }

    void plaintextNotInFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("p.hvelv");
        Entry e = Entry::createNew();
        e.password = "SuperUnikStreng123";
        VaultData data;
        data.entries << e;
        QCOMPARE(VaultFile::create(path, "pw", FastKdf).save(data), VaultFile::Status::Ok);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(!f.readAll().contains("SuperUnikStreng123"));
    }

    void tamperingIsDetected_data()
    {
        QTest::addColumn<int>("offset");
        QTest::newRow("magic") << 0;
        QTest::newRow("kdf-param") << 12;
        QTest::newRow("salt") << 30;
        QTest::newRow("innpakket-nokkel") << 100;
        QTest::newRow("data-nonce") << 115;
        QTest::newRow("data-mac") << 140;
        QTest::newRow("chiffertekst") << -1; // siste byte
    }

    void tamperingIsDetected()
    {
        QFETCH(int, offset);
        QTemporaryDir dir;
        const QString path = dir.filePath("t.hvelv");
        VaultData data;
        data.entries << Entry::createNew();
        QCOMPARE(VaultFile::create(path, "pw", FastKdf).save(data), VaultFile::Status::Ok);

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QByteArray raw = f.readAll();
        const int pos = offset < 0 ? int(raw.size()) - 1 : offset;
        raw[pos] = char(raw[pos] ^ 0x01);
        f.seek(0);
        f.write(raw);
        f.close();

        QVERIFY(VaultFile::open(path, "pw").status != VaultFile::Status::Ok);
    }

    void changePassword()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("c.hvelv");
        VaultData data;
        data.entries << Entry::createNew();
        auto vf = VaultFile::create(path, "gammelt", FastKdf);
        QCOMPARE(vf.save(data), VaultFile::Status::Ok);
        QCOMPARE(vf.changePassword("nytt", data, FastKdf), VaultFile::Status::Ok);
        QCOMPARE(VaultFile::open(path, "gammelt").status, VaultFile::Status::WrongPassword);
        QCOMPARE(VaultFile::open(path, "nytt").status, VaultFile::Status::Ok);
    }

    // RFC 6238, vedlegg B
    void totpRfc6238_data()
    {
        QTest::addColumn<qint64>("time");
        QTest::addColumn<QString>("sha1");
        QTest::addColumn<QString>("sha256");
        QTest::addColumn<QString>("sha512");
        QTest::newRow("59") << qint64(59) << QString("94287082") << QString("46119246") << QString("90693936");
        QTest::newRow("1111111109") << qint64(1111111109) << QString("07081804") << QString("68084774") << QString("25091201");
        QTest::newRow("1111111111") << qint64(1111111111) << QString("14050471") << QString("67062674") << QString("99943326");
        QTest::newRow("1234567890") << qint64(1234567890) << QString("89005924") << QString("91819424") << QString("93441116");
        QTest::newRow("2000000000") << qint64(2000000000) << QString("69279037") << QString("90698825") << QString("38618901");
        QTest::newRow("20000000000") << qint64(20000000000) << QString("65353130") << QString("77737706") << QString("47863826");
    }

    void totpRfc6238()
    {
        QFETCH(qint64, time);
        QFETCH(QString, sha1);
        QFETCH(QString, sha256);
        QFETCH(QString, sha512);

        totp::Config c;
        c.digits = 8;
        c.secret = "12345678901234567890";
        QCOMPARE(totp::code(c, time), sha1);
        c.algorithm = QCryptographicHash::Sha256;
        c.secret = "12345678901234567890123456789012";
        QCOMPARE(totp::code(c, time), sha256);
        c.algorithm = QCryptographicHash::Sha512;
        c.secret = "1234567890123456789012345678901234567890123456789012345678901234";
        QCOMPARE(totp::code(c, time), sha512);
    }

    void totpParse()
    {
        // "12345678901234567890" i base32
        const QString b32 = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
        auto plain = totp::parse("gezd gnbv gy3t qojq gezd gnbv gy3t qojq");
        QVERIFY(plain);
        QCOMPARE(plain->secret, QByteArray("12345678901234567890"));

        auto uri = totp::parse("otpauth://totp/Visma:rudi%40firma.no?secret=" + b32
                               + "&issuer=Visma&digits=8&period=60&algorithm=SHA256");
        QVERIFY(uri);
        QCOMPARE(uri->digits, 8);
        QCOMPARE(uri->period, 60);
        QCOMPARE(uri->algorithm, QCryptographicHash::Sha256);
        QCOMPARE(uri->issuer, QString("Visma"));
        QCOMPARE(uri->account, QString("rudi@firma.no"));

        QVERIFY(!totp::parse("ikke base32!"));
        QVERIFY(!totp::parse(""));
    }

    void migratesFormat1Contacts()
    {
        // To oppføringer med samme kontakt (samme e-post) og én uten kontakt.
        const QByteArray v1 = R"({"format":1,"entries":[
            {"id":"e1","title":"A","customer":"Kunde AS","contactName":"Kari","contactEmail":"KARI@x.no","contactPhone":"1","contactRole":"Support"},
            {"id":"e2","title":"B","contactName":"Kari N.","contactEmail":"kari@x.no"},
            {"id":"e3","title":"C"}],"settings":{}})";
        VaultData d;
        QVERIFY(VaultData::fromJson(v1, d));
        QCOMPARE(int(d.contacts.size()), 1);
        const Contact &c = d.contacts.first();
        QCOMPARE(c.name, QString("Kari"));
        QCOMPARE(c.role, QString("Support"));
        QCOMPARE(c.company, QString("Kunde AS"));
        QCOMPARE(d.entries[0].contactIds, QStringList{c.id});
        QCOMPARE(d.entries[1].contactIds, QStringList{c.id});
        QVERIFY(d.entries[2].contactIds.isEmpty());
        QCOMPARE(d.entries[0].type, QString("login"));

        // Lagret på nytt blir det format 2, og kontaktene består.
        VaultData again;
        QVERIFY(VaultData::fromJson(d.toJson(), again));
        QCOMPARE(int(again.contacts.size()), 1);
        QVERIFY(d.toJson().contains("\"format\":2"));

        QVERIFY(!VaultData::fromJson(R"({"format":99,"entries":[]})", d));
    }

    void passwordHistory()
    {
        const QDateTime t1(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC);
        const QDateTime t2(QDate(2026, 2, 1), QTime(12, 0), QTimeZone::UTC);
        Entry before = Entry::createNew();
        before.password = "gammelt";

        Entry same = before;
        same.recordPasswordChange(before, t1);
        QVERIFY(same.passwordHistory.isEmpty());

        Entry changed = before;
        changed.password = "nytt";
        changed.passwordHistory.clear(); // det klienten sender inn ignoreres
        changed.recordPasswordChange(before, t1);
        QCOMPARE(int(changed.passwordHistory.size()), 1);
        QCOMPARE(changed.passwordHistory.first().password, QString("gammelt"));
        QCOMPARE(changed.passwordChangedAt, t1);

        Entry again = changed;
        again.password = "nyest";
        again.recordPasswordChange(changed, t2);
        QCOMPARE(again.passwordHistory.first().password, QString("nytt"));
        QCOMPARE(again.passwordHistory.at(1).password, QString("gammelt"));

        Entry many = again;
        for (int i = 0; i < 30; ++i) {
            Entry prev = many;
            many.password = QString::number(i);
            many.recordPasswordChange(prev, t2);
        }
        QCOMPARE(int(many.passwordHistory.size()), Entry::MaxHistory);
    }

    void entryTypesAreConsistent()
    {
        QSet<QString> keys;
        for (const auto &t : entrytypes::types()) {
            const auto o = t.toObject();
            QVERIFY(!o["key"].toString().isEmpty());
            QVERIFY(!o["label"].toString().isEmpty());
            keys << o["key"].toString();
        }
        QVERIFY(keys.contains("login") && keys.contains("api") && keys.contains("certificate"));
        for (const auto &t : entrytypes::templates()) {
            const auto o = t.toObject();
            QVERIFY2(keys.contains(o["type"].toString()), qPrintable(o["name"].toString()));
            // Felt i malen må finnes på typen.
            const auto known = entrytypes::type(o["type"].toString())["fields"].toArray();
            QStringList knownKeys;
            for (const auto &f : known)
                knownKeys << f.toObject()["key"].toString();
            for (const auto &k : o["preset"].toObject()["fields"].toObject().keys())
                QVERIFY2(knownKeys.contains(k), qPrintable(k));
        }
        QVERIFY(!entrytypes::searchableFieldKeys("database").contains("connectionString"));
        QVERIFY(entrytypes::searchableFieldKeys("database").contains("host"));
        QCOMPARE(entrytypes::type("finnes-ikke")["key"].toString(), QString("login"));
    }

    void calendarIcs()
    {
        const QDate today(2026, 10, 4);
        VaultData d;
        Contact kari = Contact::createNew();
        kari.name = "Kari";
        kari.email = "kari@x.example";
        d.contacts << kari;

        Entry e = Entry::createNew();
        e.title = "Visma, prod; test\\sti";
        e.customer = "Kunde AS";
        e.password = "SKAL-IKKE-MED";
        e.totpSecret = "JBSWY3DPEHPK3PXP";
        e.fields = QJsonObject{{"connectionString", "Password=HEMMELIG"}};
        e.expiresAt = QDate(2026, 12, 31);
        e.reminderDays = 21;
        e.contactIds = {kari.id};
        e.renewalProcedure = QString("Steg 1\nSteg 2 med en veldig lang tekst som garantert må brytes over flere linjer i kalenderfilen ").repeated(2);

        Entry expired = Entry::createNew();
        expired.title = "Gammel";
        expired.expiresAt = today.addDays(-1);

        Entry none = Entry::createNew();
        none.title = "Uten dato";
        d.entries << e << expired << none;

        const QByteArray ics = calendar::buildIcs(d, {}, false, today);
        QVERIFY(ics.startsWith("BEGIN:VCALENDAR\r\n"));
        QVERIFY(ics.endsWith("END:VCALENDAR\r\n"));
        QCOMPARE(ics.count("BEGIN:VEVENT"), 1); // utløpt og uten dato er utelatt
        QVERIFY(ics.contains(("UID:hvelv-" + e.id + "@hvelv").toUtf8()));
        QVERIFY(ics.contains("DTSTART;VALUE=DATE:20261231"));
        QVERIFY(ics.contains("DTEND;VALUE=DATE:20270101"));
        QVERIFY(ics.contains("TRIGGER:-P21D"));
        QVERIFY(ics.contains("Visma\\, prod\\; test\\\\sti"));
        QVERIFY(!ics.contains("SKAL-IKKE-MED"));
        QVERIFY(!ics.contains("JBSWY3DPEHPK3PXP"));
        QVERIFY(!ics.contains("HEMMELIG"));

        // Utfoldet: kontakt og fornyelsesrutine er med i beskrivelsen
        QByteArray unfolded = ics;
        unfolded.replace("\r\n ", "");
        QVERIFY(unfolded.contains("Kontakt: Kari <kari@x.example>"));
        QVERIFY(unfolded.contains("Steg 1\\nSteg 2"));

        for (const QByteArray &l : ics.split('\n'))
            QVERIFY2(l.size() <= 76, l.constData()); // 75 + \r

        QCOMPARE(calendar::buildIcs(d, {}, true, today).count("BEGIN:VEVENT"), 2);
        QCOMPARE(calendar::buildIcs(d, {expired.id}, true, today).count("BEGIN:VEVENT"), 1);
        QCOMPARE(calendar::buildIcs(d, {none.id}, true, today).count("BEGIN:VEVENT"), 0);
    }

    void passwordStrength()
    {
        QCOMPARE(health::estimateBits(""), 0);
        QCOMPARE(health::estimateBits("password"), 0);      // vanlig passord
        QCOMPARE(health::estimateBits("Passord1!"), 0);
        QVERIFY(health::estimateBits("Sommer2026!") <= 30);  // ord + år + tegn
        QVERIFY(health::estimateBits("abcdefghij") < 20);    // sekvens
        QVERIFY(health::estimateBits("aaaaaaaaaaaa") < 20);
        QVERIFY(health::isWeak("kort1A!"));
        QVERIFY(!health::isWeak("correct horse battery staple"));
        QVERIFY(!health::isWeak(generatePassword({})));      // generert, 20 tegn
        QVERIFY(health::estimateBits(generatePassword({})) > 100);
    }

    void healthReport()
    {
        const QDate today(2026, 10, 4);
        VaultData d;
        auto make = [&](const char *title, const char *pw, QDate changed = {}) {
            Entry e = Entry::createNew();
            e.title = title;
            e.password = pw;
            e.createdAt = QDateTime(today, QTime(12, 0), QTimeZone::UTC);
            if (changed.isValid())
                e.passwordChangedAt = QDateTime(changed, QTime(12, 0), QTimeZone::UTC);
            d.entries << e;
            return e.id;
        };
        const QString weak = make("Svak", "Sommer2026!");
        const QString reuseA = make("A", "Gjenbrukt-Men-Langt-Passord-42");
        const QString reuseB = make("B", "Gjenbrukt-Men-Langt-Passord-42");
        const QString old = make("Gammel", "Xq9!vLr2#pZt8@mKw4$e", today.addDays(-400));
        make("Uten passord", "");
        const QString fine = make("Bra", "Hk3$uP9!qL2@vX7#rT5%");
        d.entries.last().expiresAt = today.addDays(-1); // utløpt

        QHash<QString, int> breaches{{health::sha1Hex("Sommer2026!"), 1234}};
        const auto r = health::analyze(d, today, 365, breaches);
        QCOMPARE(r.checked, 5);
        QCOMPARE(r.weak, 1);
        QCOMPARE(r.reused, 2);
        QCOMPARE(r.old, 1);
        QCOMPARE(r.breached, 1);
        QCOMPARE(r.expired, 1);

        QHash<QString, QStringList> kinds;
        for (const auto &i : r.issues)
            kinds.insert(i.entryId, i.kinds);
        QCOMPARE(kinds.value(weak), (QStringList{"weak", "breached"}));
        QCOMPARE(kinds.value(reuseA), QStringList{"reused"});
        QCOMPARE(kinds.value(reuseB), QStringList{"reused"});
        QCOMPARE(kinds.value(old), QStringList{"old"});
        QCOMPARE(kinds.value(fine), QStringList{"expired"});
    }

    void hibpParsing()
    {
        const QString sha = health::sha1Hex("password");
        QCOMPARE(sha, QString("5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8"));
        const QByteArray response =
            "003D68EB55068C33ACE09247EE4C639306B:3\r\n"
            "1E4C9B93F3F0682250B6CF8331B7EE68FD8:9659365\r\n"
            "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF:0\r\n";
        QCOMPARE(health::hibpCountInResponse(response, sha), 9659365);
        QCOMPARE(health::hibpCountInResponse(response, health::sha1Hex("noe helt annet")), 0);
    }

    void autoTypeSequence()
    {
        Entry e;
        e.username = "rudi";
        e.password = "p{w}d";
        e.title = "Visma";
        using A = autotype::Action;

        auto r = autotype::parseSequence(autotype::DefaultSequence, e);
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.actions.size(), 4);
        QCOMPARE(r.actions[0].kind, A::Text);
        QCOMPARE(r.actions[0].text, QString("rudi"));
        QCOMPARE(r.actions[1].kind, A::Key);
        QCOMPARE(r.actions[1].key, A::Tab);
        QCOMPARE(r.actions[2].text, QString("p{w}d")); // klammer i passordet skrives som de er
        QCOMPARE(r.actions[3].key, A::Enter);

        r = autotype::parseSequence("{username}@firma{TAB}{DELAY 300}{PASSWORD}{TAB}{TOTP}{{}x{}}", e, "123456");
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.actions.size(), 6);
        QCOMPARE(r.actions[0].text, QString("rudi@firma"));
        QCOMPARE(r.actions[2].kind, A::Delay);
        QCOMPARE(r.actions[2].ms, 300);
        QCOMPARE(r.actions[5].text, QString("123456{x}"));

        QVERIFY(!autotype::parseSequence("{FOO}", e).error.isEmpty());
        QVERIFY(!autotype::parseSequence("{USERNAME", e).error.isEmpty());
        QVERIFY(!autotype::parseSequence("{DELAY abc}", e).error.isEmpty());

        QCOMPARE(autotype::sequenceFor(e), QString(autotype::DefaultSequence));
        e.customFields << CustomField{"Auto-Type", "{PASSWORD}{ENTER}", false};
        QCOMPARE(autotype::sequenceFor(e), QString("{PASSWORD}{ENTER}"));
    }

    void autoTypeMatching()
    {
        Entry visma;
        visma.title = "Visma.net Kunde AS";
        visma.url = "https://login.visma.net/signin";
        visma.system = "Visma.net";
        Entry altinn;
        altinn.title = "Altinn";
        altinn.url = "altinn.no";
        Entry rdp;
        rdp.title = "Server A";
        rdp.customFields << CustomField{"Auto-Type-vindu", "*Windows-sikkerhet*", false};

        QVERIFY(autotype::matchScore(visma, "Sign in - login.visma.net - Microsoft Edge") >= 60);
        QVERIFY(autotype::matchScore(visma, "Visma.net Kunde AS – Chrome") >= 50);
        QVERIFY(autotype::matchScore(visma, "Visma Home") > 0); // hoveddomene «visma»
        QCOMPARE(autotype::matchScore(visma, "Altinn – Logg inn"), 0);
        QVERIFY(autotype::matchScore(altinn, "Altinn – Logg inn") > 0);
        QCOMPARE(autotype::matchScore(rdp, "Windows-sikkerhet"), 100);
        QCOMPARE(autotype::matchScore(rdp, "Notisblokk"), 0);
        QCOMPARE(autotype::matchScore(visma, ""), 0);
    }

    void fileStoreRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("s.hvelv");
        auto created = FileStore::create(path, "pw", FastKdf);
        QVERIFY(created.result.ok());
        QVERIFY(created.store->data().entries.isEmpty());
        QCOMPARE(FileStore::create(path, "pw", FastKdf).result.error, VaultStore::Error::Io);

        Contact c;
        c.id = "c1";
        c.name = "Kari";
        Entry a = Entry::createNew();
        a.title = "A";
        a.contactIds = {"c1"};
        Entry b = Entry::createNew();
        b.title = "B";
        b.contactIds = {"c1"};
        QVERIFY(created.store->apply({.upsertEntries = {a, b}, .upsertContacts = {c}}).ok());
        QCOMPARE(int(created.store->data().entries.size()), 2);

        a.title = "A2";
        QVERIFY(created.store->apply({.upsertEntries = {a}, .deleteEntryIds = {b.id}}).ok());
        QVERIFY(created.store->apply({.deleteContactIds = {"c1"}}).ok());
        QVERIFY(created.store->apply({}).ok());
        created.store.reset();

        auto opened = FileStore::open(path, "pw");
        QVERIFY(opened.result.ok());
        QCOMPARE(opened.store->kind(), VaultStore::Kind::File);
        QCOMPARE(opened.store->location(), path);
        const VaultData &d = opened.store->data();
        QCOMPARE(int(d.entries.size()), 1);
        QCOMPARE(d.entries.first().title, QStringLiteral("A2"));
        QVERIFY(d.entries.first().contactIds.isEmpty()); // kontakten ble fjernet fra oppføringen
        QVERIFY(d.contacts.isEmpty());

        const auto wrong = FileStore::open(path, "feil");
        QCOMPARE(wrong.result.error, VaultStore::Error::WrongPassword);
        QVERIFY(!wrong.store);
        QVERIFY(!wrong.result.message.isEmpty());
    }

    void fileStoreKeepsDataWhenSaveFails()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("x.hvelv");
        auto created = FileStore::create(path, "pw", FastKdf);
        QVERIFY(created.result.ok());
        // En mappe der filen skal ligge gjør lagringen umulig.
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));

        Entry e = Entry::createNew();
        e.title = "X";
        const auto r = created.store->apply({.upsertEntries = {e}});
        QVERIFY(!r.ok());
        QVERIFY(!r.message.isEmpty());
        QVERIFY(created.store->data().entries.isEmpty()); // minnet er uendret
    }

    void fileStoreChangePassword()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("p.hvelv");
        auto created = FileStore::create(path, "gammelt", FastKdf);
        QVERIFY(created.result.ok());
        Entry e = Entry::createNew();
        e.title = "Beholdes";
        QVERIFY(created.store->apply({.upsertEntries = {e}}).ok());

        QCOMPARE(created.store->changePassword("feil", "nytt").error, VaultStore::Error::WrongPassword);
        QVERIFY(created.store->changePassword("gammelt", "nytt").ok());
        QVERIFY(created.store->apply({.upsertEntries = {Entry::createNew()}}).ok()); // lagring virker etterpå

        QCOMPARE(FileStore::open(path, "gammelt").result.error, VaultStore::Error::WrongPassword);
        auto reopened = FileStore::open(path, "nytt");
        QVERIFY(reopened.result.ok());
        QCOMPARE(int(reopened.store->data().entries.size()), 2);
    }

    void expiryStatus()
    {
        const QDate today(2026, 10, 3);
        Entry e;
        QCOMPARE(e.expiryStatus(today), ExpiryStatus::None);
        e.reminderDays = 14;
        e.expiresAt = today.addDays(30);
        QCOMPARE(e.expiryStatus(today), ExpiryStatus::Ok);
        e.expiresAt = today.addDays(14);
        QCOMPARE(e.expiryStatus(today), ExpiryStatus::ExpiringSoon);
        e.expiresAt = today;
        QCOMPARE(e.expiryStatus(today), ExpiryStatus::ExpiringSoon);
        e.expiresAt = today.addDays(-1);
        QCOMPARE(e.expiryStatus(today), ExpiryStatus::Expired);
    }

    void passwordGenerator()
    {
        PasswordOptions o;
        o.length = 32;
        const QString pw = generatePassword(o);
        QCOMPARE(int(pw.size()), 32);
        bool lower = false, upper = false, digit = false, symbol = false;
        for (QChar c : pw) {
            lower |= c.isLower();
            upper |= c.isUpper();
            digit |= c.isDigit();
            symbol |= !c.isLetterOrNumber();
        }
        QVERIFY(lower && upper && digit && symbol);
        QVERIFY(!pw.contains('l') && !pw.contains('0') && !pw.contains('O'));
        QVERIFY(generatePassword(o) != pw);

        o.symbols = o.upper = o.digits = false;
        o.length = 4;
        for (QChar c : generatePassword(o))
            QVERIFY(c.isLower());
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
