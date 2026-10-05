#include "importexport.h"

#include <QtTest>

using namespace hvelv;
using namespace hvelv::importexport;

static QByteArray readData(const char *name)
{
    QFile f(QFINDTESTDATA(QStringLiteral("data/") + QLatin1String(name)));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("Fant ikke testdata %s", name);
    return f.readAll();
}

class TestImport : public QObject
{
    Q_OBJECT

private slots:
    void csvParser()
    {
        const auto rows = parseCsv(QStringLiteral("a,b,c\r\n\"x, y\",\"si \"\"hei\"\"\",\"linje1\nlinje2\"\n,,\n"));
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[1], (QStringList{"x, y", "si \"hei\"", "linje1\nlinje2"}));
        QCOMPARE(rows[2], (QStringList{"", "", ""}));
        QCOMPARE(parseCsv("a;b\n1;2", ';')[1], (QStringList{"1", "2"}));
    }

    void keePassXml()
    {
        const QByteArray xml = readData("keepass.xml");
        const auto r = importData(xml, "eksport.xml");
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.format, QString("KeePass XML"));
        QCOMPARE(r.entries.size(), 2); // historikk og papirkurv er utelatt
        const Entry &v = r.entries[0];
        QCOMPARE(v.title, QString("Visma"));
        QCOMPARE(v.username, QString("rudi"));
        QCOMPARE(v.password, QString("hemmelig"));
        QCOMPARE(v.url, QString("https://visma.example"));
        QCOMPARE(v.notes, QString("Notat"));
        QVERIFY(v.totpSecret.startsWith("otpauth://"));
        QCOMPARE(v.tags, QStringList{"Kunde A"});
        QCOMPARE(v.expiresAt, QDate(2027, 3, 1));
        QCOMPARE(v.customFields.size(), 1);
        QCOMPARE(v.customFields[0].name, QString("PIN"));
        QVERIFY(v.customFields[0].secret);
        QVERIFY(!r.entries[1].expiresAt.isValid()); // Expires=False
        QVERIFY(r.entries[1].tags.isEmpty());
        QCOMPARE(r.warnings.size(), 1); // vedlegg ikke importert
    }

    void bitwardenJson()
    {
        const QByteArray json = readData("bitwarden.json");
        const auto r = importData(json, "bitwarden.json");
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.entries.size(), 2);
        const Entry &gh = r.entries[0];
        QCOMPARE(gh.url, QString("https://github.com"));
        QCOMPARE(gh.totpSecret, QString("JBSWY3DPEHPK3PXP"));
        QCOMPARE(gh.tags, QStringList{"Kunder"});
        QCOMPARE(gh.customFields.size(), 3); // ekstra URL + 2 felt
        QVERIFY(gh.customFields[1].secret);
        QVERIFY(!gh.customFields[2].secret);
        QCOMPARE(r.entries[1].type, QString("note"));
        QCOMPARE(r.entries[1].fields.value("content").toString(), QString("1111 2222"));
        QCOMPARE(r.warnings.size(), 1);

        QVERIFY(!importData("{\"encrypted\":true,\"items\":[]}").ok);
    }

    void csvFormats_data()
    {
        QTest::addColumn<QByteArray>("csv");
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("password");
        QTest::addColumn<QStringList>("tags");

        QTest::newRow("bitwarden")
            << QByteArray("folder,favorite,type,name,notes,fields,reprompt,login_uri,login_username,login_password,login_totp\n"
                          "Kunder,,login,Visma,,,0,https://v.example,rudi,pw1,\n")
            << "Bitwarden CSV" << "Visma" << "pw1" << QStringList{"Kunder"};
        QTest::newRow("1password")
            << QByteArray("\"Title\",\"Url\",\"Username\",\"Password\",\"OTPAuth\",\"Favorite\",\"Archived\",\"Tags\",\"Notes\"\n"
                          "\"Jira\",\"https://jira.example\",\"rudi\",\"pw,2\",\"\",\"false\",\"false\",\"jobb;atlassian\",\"\"\n")
            << "1Password CSV" << "Jira" << "pw,2" << QStringList{"jobb", "atlassian"};
        QTest::newRow("keepassxc")
            << QByteArray("\"Group\",\"Title\",\"Username\",\"Password\",\"URL\",\"Notes\",\"TOTP\",\"Icon\",\"Last Modified\",\"Created\"\n"
                          "\"Root/Kunde B\",\"SFTP\",\"ftp\",\"pw3\",\"\",\"\",\"\",\"0\",\"2026-01-01\",\"2026-01-01\"\n")
            << "KeePassXC CSV" << "SFTP" << "pw3" << QStringList{"Kunde B"};
        QTest::newRow("chrome")
            << QByteArray("name,url,username,password,note\nportal.example,https://portal.example/,u,pw4,\n")
            << "Chrome/Edge CSV" << "portal.example" << "pw4" << QStringList{};
        QTest::newRow("norsk-semikolon")
            << QByteArray("Navn;Brukernavn;Passord;Kunde;Avtalenr\nAltinn;12345678901;pw5;Kunde C;A-77\n")
            << "CSV" << "Altinn" << "pw5" << QStringList{};
    }

    void csvFormats()
    {
        QFETCH(QByteArray, csv);
        QFETCH(QString, format);
        QFETCH(QString, title);
        QFETCH(QString, password);
        QFETCH(QStringList, tags);
        const auto r = importData(csv, "fil.csv");
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.format, format);
        QCOMPARE(r.entries.size(), 1);
        QCOMPARE(r.entries[0].title, title);
        QCOMPARE(r.entries[0].password, password);
        QCOMPARE(r.entries[0].tags, tags);
    }

    void csvUnknownColumnsBecomeCustomFields()
    {
        const auto r = importData("Navn;Brukernavn;Passord;Kunde;Avtalenr\nAltinn;123;pw;Kunde C;A-77\n");
        QCOMPARE(r.entries[0].customer, QString("Kunde C"));
        QCOMPARE(r.entries[0].customFields.size(), 1);
        QCOMPARE(r.entries[0].customFields[0].name, QString("Avtalenr"));
        QCOMPARE(r.entries[0].customFields[0].value, QString("A-77"));
    }

    void rejectsUnknown()
    {
        QVERIFY(!importData("foo,bar\n1,2\n").ok);
        QVERIFY(!importData("<html></html>").ok);
        QVERIFY(!importData("x", "db.kdbx").ok);
        QVERIFY(!importData(QByteArray("HVELVDB1") + QByteArray(200, '\0')).ok);
    }

    void mergeSkipsDuplicatesAndAssignsNewIds()
    {
        VaultData vault;
        Entry existing = Entry::createNew();
        existing.title = "Visma";
        existing.username = "rudi";
        existing.password = "pw1";
        vault.entries << existing;

        ImportResult r;
        r.ok = true;
        Entry dup = existing; // samme id og innhold
        Entry fresh = Entry::createNew();
        fresh.title = "Ny";
        Contact c = Contact::createNew();
        c.name = "Kari";
        fresh.contactIds = {c.id};
        r.entries << dup << fresh;
        r.contacts << c;

        MergeOptions opt;
        opt.customer = "Kunde X";
        opt.tag = "importert";
        const auto stats = merge(vault, r, opt);
        // Kunde overstyres før duplikatsjekk, men duplikatnøkkelen bruker ikke kunde.
        QCOMPARE(stats.skipped, 1);
        QCOMPARE(stats.added, 1);
        QCOMPARE(stats.contactsAdded, 1);
        const Entry &added = vault.entries.last();
        QVERIFY(added.id != fresh.id);
        QCOMPARE(added.customer, QString("Kunde X"));
        QVERIFY(added.tags.contains("importert"));
        QCOMPARE(added.contactIds.size(), 1);
        QCOMPARE(added.contactIds[0], vault.contacts.last().id);
        QVERIFY(vault.contacts.last().id != c.id);
    }

    void exportCsvRoundTrip()
    {
        VaultData d;
        Entry e = Entry::createNew();
        e.title = "Komma, \"sitat\" og\nlinjeskift";
        e.username = "rudi";
        e.password = "p,w\"1";
        e.customer = "Kunde";
        e.expiresAt = QDate(2027, 1, 2);
        e.tags = {"a", "b"};
        d.entries << e;
        const auto r = importData(exportCsv(d), "eksport.csv");
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.entries.size(), 1);
        QCOMPARE(r.entries[0].title, e.title);
        QCOMPARE(r.entries[0].password, e.password);
        QCOMPARE(r.entries[0].customer, e.customer);
        QCOMPARE(r.entries[0].expiresAt, e.expiresAt);
        QCOMPARE(r.entries[0].tags, e.tags);
    }
};

QTEST_GUILESS_MAIN(TestImport)
#include "tst_import.moc"
