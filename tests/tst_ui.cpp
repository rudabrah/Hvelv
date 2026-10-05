// Røyktest av QML-grensesnittet. Kjører med QT_QPA_PLATFORM=offscreen og software-rendering,
// så ingen ekte vinduer, tastatur eller mus brukes. Sett HVELV_UI_SNAPSHOTS=<mappe> for å lagre
// skjermbilder av hvert steg.

#include "certificateinfo.h"
#include "entryfiltermodel.h"
#include "qrscanner.h"
#include "sqlstore.h"
#include "vaultcontroller.h"
#include "vaultfile.h"

#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <wincrypt.h>
#endif

Q_IMPORT_QML_PLUGIN(HvelvPlugin)

using namespace hvelv;

namespace {

#ifdef Q_OS_WIN
// Lager et selvsignert sertifikat (gyldig 2 år) og returnerer {PFX med passord, DER}.
QPair<QByteArray, QByteArray> makeTestCertificate(const QString &cn, const QString &pfxPassword)
{
    QPair<QByteArray, QByteArray> out;
    const std::wstring x500 = (QStringLiteral("CN=") + cn + QStringLiteral(", O=Hvelv Test")).toStdWString();
    DWORD nameLen = 0;
    CertStrToNameW(X509_ASN_ENCODING, x500.c_str(), CERT_X500_NAME_STR, nullptr, nullptr, &nameLen, nullptr);
    QByteArray name(int(nameLen), Qt::Uninitialized);
    CertStrToNameW(X509_ASN_ENCODING, x500.c_str(), CERT_X500_NAME_STR, nullptr,
                   reinterpret_cast<BYTE *>(name.data()), &nameLen, nullptr);
    CERT_NAME_BLOB subject{nameLen, reinterpret_cast<BYTE *>(name.data())};

    SYSTEMTIME start, end;
    GetSystemTime(&start);
    end = start;
    end.wYear += 2;

    // Nøkkel i en midlertidig nøkkelcontainer som slettes etterpå.
    const std::wstring container = QUuid::createUuid().toString().toStdWString();
    CRYPT_KEY_PROV_INFO prov{};
    prov.pwszContainerName = const_cast<wchar_t *>(container.c_str());
    prov.dwProvType = PROV_RSA_AES;
    prov.dwKeySpec = AT_KEYEXCHANGE;
    HCRYPTPROV hprov = 0;
    CryptAcquireContextW(&hprov, container.c_str(), nullptr, PROV_RSA_AES, CRYPT_NEWKEYSET);
    HCRYPTKEY key = 0;
    CryptGenKey(hprov, AT_KEYEXCHANGE, (2048 << 16) | CRYPT_EXPORTABLE, &key);

    PCCERT_CONTEXT cert = CertCreateSelfSignCertificate(hprov, &subject, 0, &prov, nullptr, &start, &end, nullptr);
    if (cert) {
        out.second = QByteArray(reinterpret_cast<const char *>(cert->pbCertEncoded), int(cert->cbCertEncoded));
        HCERTSTORE mem = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
        CertAddCertificateContextToStore(mem, cert, CERT_STORE_ADD_ALWAYS, nullptr);
        CRYPT_DATA_BLOB pfx{};
        const std::wstring pw = pfxPassword.toStdWString();
        PFXExportCertStoreEx(mem, &pfx, pw.c_str(), nullptr, EXPORT_PRIVATE_KEYS);
        QByteArray buf(int(pfx.cbData), Qt::Uninitialized);
        pfx.pbData = reinterpret_cast<BYTE *>(buf.data());
        if (PFXExportCertStoreEx(mem, &pfx, pw.c_str(), nullptr, EXPORT_PRIVATE_KEYS))
            out.first = buf;
        CertCloseStore(mem, 0);
        CertFreeCertificateContext(cert);
    }
    if (key)
        CryptDestroyKey(key);
    if (hprov)
        CryptReleaseContext(hprov, 0);
    HCRYPTPROV del = 0;
    CryptAcquireContextW(&del, container.c_str(), nullptr, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    return out;
}
#endif

QStringList g_qmlWarnings;
QtMessageHandler g_previousHandler = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const bool fromQml = msg.contains(QLatin1String(".qml"))
                         || (ctx.file && QByteArray(ctx.file).endsWith(".qml"));
    const bool problem = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
    if (problem && fromQml)
        g_qmlWarnings << msg;
    if (g_previousHandler)
        g_previousHandler(type, ctx, msg);
}

QObject *findByName(QObject *root, const char *name)
{
    return root->findChild<QObject *>(QString::fromLatin1(name));
}

// Sletter testdatabaser (bare HvelvTest_*) etter SQL-testen.
void dropTestDatabases(const QString &server, const QStringList &names)
{
    const QString connection = QStringLiteral("ui-cleanup");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QODBC"), connection);
        db.setDatabaseName(SqlLocation{server, QStringLiteral("master")}.odbcConnectionString());
        if (db.open()) {
            QSqlQuery q(db);
            for (const auto &name : names)
                if (name.startsWith(QLatin1String("HvelvTest_")))
                    q.exec(QStringLiteral("IF DB_ID('%1') IS NOT NULL BEGIN ALTER DATABASE [%1] SET SINGLE_USER "
                                          "WITH ROLLBACK IMMEDIATE; DROP DATABASE [%1]; END").arg(name));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

} // namespace

class TestUi : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QQmlApplicationEngine *m_engine = nullptr;
    VaultController *m_vault = nullptr;
    QQuickWindow *m_window = nullptr;
    QString m_soonId;
    QString m_expiredId;
    QString m_contactId;

    void snap(const QString &name)
    {
        const QString dir = qEnvironmentVariable("HVELV_UI_SNAPSHOTS");
        if (dir.isEmpty())
            return;
        QTest::qWait(300); // la bindinger og animasjoner falle til ro
        QVERIFY(m_window->grabWindow().save(QDir(dir).filePath(name + QStringLiteral(".png"))));
    }

private slots:
    void initTestCase()
    {
        g_previousHandler = qInstallMessageHandler(messageHandler);
        QCoreApplication::setOrganizationName(QStringLiteral("HvelvTest"));
        QCoreApplication::setApplicationName(QStringLiteral("HvelvUiTest"));
        // Software-rendering gir stabile tester uten GPU, men kan ikke tegne shader-effekter
        // (f.eks. Fluent-dialogenes bakgrunn). HVELV_UI_RHI=d3d11 gir skjermbilder som i appen.
        if (qEnvironmentVariable("HVELV_UI_RHI") == QLatin1String("d3d11"))
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
        else
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));

        const QDate today = QDate::currentDate();
        VaultData data;

        Entry soon = Entry::createNew();
        soon.title = QStringLiteral("Visma.net – Kunde AS");
        soon.url = QStringLiteral("https://login.visma.net");
        soon.customer = QStringLiteral("Kunde AS");
        soon.tags = {QStringLiteral("ERP"), QStringLiteral("integrasjon")};
        soon.username = QStringLiteral("rudi@firma.no");
        soon.password = QStringLiteral("S3cret!");
        soon.twoFactorPlatform = QStringLiteral("TOTP i Hvelv");
        soon.totpSecret = QStringLiteral("JBSWY3DPEHPK3PXP");
        Contact kari = Contact::createNew();
        kari.name = QStringLiteral("Kari Nordmann");
        kari.company = QStringLiteral("Leverandør AS");
        kari.role = QStringLiteral("Teknisk kontakt");
        kari.email = QStringLiteral("kari@leverandor.example");
        kari.phone = QStringLiteral("+47 900 00 000");
        m_contactId = kari.id;
        data.contacts << kari;
        soon.contactIds = {kari.id};
        soon.system = QStringLiteral("Visma.net");
        soon.environment = QStringLiteral("prod");
        soon.renewalProcedure = QStringLiteral("Bestill ny bruker via kundeportalen.");
        soon.customFields = {{QStringLiteral("Avtalenr"), QStringLiteral("12345"), false}};
        soon.owner = QStringLiteral("Rudi");
        soon.expiresAt = today.addDays(5);
        soon.reminderChannel = ReminderChannel::Both;
        soon.notes = QStringLiteral("Avtalenr. 12345. Tilgang fornyes via kundeportalen.");
        m_soonId = soon.id;

        Entry expired = Entry::createNew();
        expired.title = QStringLiteral("Azure DevOps – Leverandør B");
        expired.type = QStringLiteral("api");
        expired.customer = QStringLiteral("Leverandør B");
        expired.fields = QJsonObject{{"tenantId", "1111-2222"}, {"clientId", "aaaa-bbbb"}};
        expired.owner = QStringLiteral("Ola");
        expired.username = QStringLiteral("svc-integrasjon");
        expired.twoFactorPlatform = QStringLiteral("Microsoft Authenticator");
        expired.expiresAt = today.addDays(-3);
        m_expiredId = expired.id;

        Entry plain = Entry::createNew();
        plain.title = QStringLiteral("SFTP – Bank");
        plain.username = QStringLiteral("ftp-user");

        data.entries << soon << expired << plain;
        const QString path = m_dir.filePath(QStringLiteral("ui.hvelv"));
        QCOMPARE(VaultFile::create(path, "pw", {64, 1, 1}).save(data), VaultFile::Status::Ok);

        m_engine = new QQmlApplicationEngine;
        m_vault = m_engine->singletonInstance<VaultController *>("Hvelv", "Vault");
        QVERIFY(m_vault);
        m_vault->useVaultPathForSession(path);
        m_engine->loadFromModule("Hvelv", "Main");
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        snap(QStringLiteral("ui-01-unlock"));
    }

    void wrongPasswordShowsError()
    {
        m_vault->unlock(QStringLiteral("feil"));
        QTRY_VERIFY(!m_vault->isBusy());
        QVERIFY(!m_vault->isUnlocked());
        QCOMPARE(m_vault->errorMessage(), VaultFile::statusText(VaultFile::Status::WrongPassword));
        snap(QStringLiteral("ui-02-wrong-password"));
    }

    void unlockShowsList()
    {
        m_vault->unlock(QStringLiteral("pw"));
        QTRY_VERIFY(m_vault->isUnlocked());
        QCOMPARE(m_vault->totalCount(), 3);
        QCOMPARE(m_vault->expiringCount(), 1);
        QCOMPARE(m_vault->expiredCount(), 1);
        QTRY_VERIFY(findByName(m_window, "vaultPage"));
        snap(QStringLiteral("ui-03-list"));
    }

    void detailsShowEverything()
    {
        QObject *page = findByName(m_window, "vaultPage");
        page->setProperty("selectedId", m_soonId);
        QObject *details = findByName(m_window, "entryDetails");
        QVERIFY(details);
        QTRY_VERIFY(details->property("visible").toBool());

        const QVariantMap e = details->property("e").toMap();
        QCOMPARE(e.value("title").toString(), QStringLiteral("Visma.net – Kunde AS"));
        QCOMPARE(e.value("daysLeft").toInt(), 5);
        QCOMPARE(e.value("expiryStatus").toString(), QStringLiteral("soon"));
        QTRY_VERIFY(details->property("otp").toMap().value("valid").toBool());
        snap(QStringLiteral("ui-04-details"));
    }

    void detailsUpdateAfterSave()
    {
        QVariantMap m = m_vault->entry(m_soonId);
        m["notes"] = QStringLiteral("Oppdatert notat");
        QCOMPARE(m_vault->saveEntry(m), m_soonId);
        // Samme flyt som når editoren lagrer: siden øker revision og detaljene lastes på nytt.
        QObject *page = findByName(m_window, "vaultPage");
        page->setProperty("revision", page->property("revision").toInt() + 1);
        QObject *details = findByName(m_window, "entryDetails");
        QTRY_COMPARE(details->property("e").toMap().value("notes").toString(),
                     QStringLiteral("Oppdatert notat"));
        // Kontakten er løst opp til et objekt i detaljene.
        const QVariantList contacts = details->property("e").toMap().value("contacts").toList();
        QCOMPARE(contacts.size(), 1);
        QCOMPARE(contacts.first().toMap().value("name").toString(), QStringLiteral("Kari Nordmann"));
    }

    void editorOpens()
    {
        QObject *page = findByName(m_window, "vaultPage");
        QVERIFY(QMetaObject::invokeMethod(page, "startEdit"));
        QTRY_VERIFY(page->property("editing").toBool());
        snap(QStringLiteral("ui-05-editor"));
        page->setProperty("editing", false);
    }

    void filtersAndSearch()
    {
        EntryFilterModel *f = m_vault->entries();
        f->setFilter(QStringLiteral("expired"));
        QCOMPARE(f->count(), 1);
        QCOMPARE(f->idAt(0), m_expiredId);
        snap(QStringLiteral("ui-06-filter-expired"));
        f->setFilter(QStringLiteral("attention"));
        QCOMPARE(f->count(), 2);
        f->setFilter(QStringLiteral("all"));
        QCOMPARE(f->count(), 3);
        f->setSearchText(QStringLiteral("kari"));   // treffer kontaktperson
        QCOMPARE(f->count(), 1);
        f->setSearchText(QStringLiteral("aaaa-bbbb")); // treffer typespesifikt felt (klient-ID)
        QCOMPARE(f->count(), 1);
        f->setSearchText(QStringLiteral("12345"));     // treffer egendefinert felt
        QCOMPARE(f->count(), 1);
        f->setSearchText({});
        f->setOwnerFilter(QStringLiteral("ola"));
        QCOMPARE(f->count(), 1);
        f->setOwnerFilter({});
        f->setCustomerFilter(QStringLiteral("Kunde AS"));
        QCOMPARE(f->count(), 1);
        f->setCustomerFilter({});
        // Sortert på kunde (Kunde AS, Leverandør B), oppføringer uten kunde sist.
        QCOMPARE(f->idAt(0), m_soonId);
        QCOMPARE(f->idAt(1), m_expiredId);
        f->setSearchText(QStringLiteral("integrasjon kunde")); // flere ord, alle må finnes
        QCOMPARE(f->count(), 1);
        f->setSearchText(QStringLiteral("finnes ikke"));
        QCOMPARE(f->count(), 0);
        f->setSearchText({});
    }

    void templatesPrefillNewEntries()
    {
        const QVariantList templates = m_vault->templates();
        int entra = -1;
        for (int i = 0; i < templates.size(); ++i)
            if (templates[i].toMap().value("name").toString().contains("Entra ID app"))
                entra = i;
        QVERIFY(entra >= 0);
        const QVariantMap e = m_vault->newEntry(entra);
        QCOMPARE(e.value("type").toString(), QStringLiteral("api"));
        QCOMPARE(e.value("reminderDays").toInt(), 30);
        QVERIFY(e.value("fields").toMap().value("tokenUrl").toString().startsWith("https://login.microsoftonline.com/"));
        QVERIFY(!e.value("renewalProcedure").toString().isEmpty());
    }

    void contactsLifecycle()
    {
        const QString id = m_vault->saveContact({{"name", "Per Support"}, {"email", "per@x.example"}});
        QVERIFY(!id.isEmpty());
        QVariantMap e = m_vault->entry(m_expiredId);
        e["contactIds"] = QStringList{id, "finnes-ikke"};
        QCOMPARE(m_vault->saveEntry(e), m_expiredId);
        // Ukjente kontakt-id-er filtreres bort ved lagring.
        QCOMPARE(m_vault->entry(m_expiredId).value("contactIds").toStringList(), QStringList{id});
        QCOMPARE(m_vault->contact(id).value("usage").toInt(), 1);

        QVERIFY(m_vault->deleteContact(id));
        QVERIFY(m_vault->entry(m_expiredId).value("contactIds").toStringList().isEmpty());
        QVERIFY(m_vault->saveContact({{"notes", "uten navn"}}).isEmpty()); // krever navn eller e-post
        m_vault->clearError();
    }

    void passwordHistoryViaController()
    {
        QVariantMap e = m_vault->entry(m_soonId);
        e["password"] = QStringLiteral("NyttPassord1");
        e["passwordHistory"] = QVariantList{}; // forsøk på å slette historikk ignoreres
        QCOMPARE(m_vault->saveEntry(e), m_soonId);
        const QVariantList history = m_vault->entry(m_soonId).value("passwordHistory").toList();
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.first().toMap().value("password").toString(), QStringLiteral("S3cret!"));
    }

    void attachments()
    {
        QTemporaryDir tmp;
        const QString src = tmp.filePath("avtale.txt");
        QFile f(src);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hemmelig avtale\n");
        f.close();

        QVERIFY(m_vault->addAttachment(m_soonId, QUrl::fromLocalFile(src)));
        const QVariantList list = m_vault->entry(m_soonId).value("attachments").toList();
        QCOMPARE(list.size(), 1);
        const QVariantMap a = list.first().toMap();
        QCOMPARE(a.value("name").toString(), QStringLiteral("avtale.txt"));
        QVERIFY(!a.contains("data")); // innholdet sendes aldri til QML

        const QString out = tmp.filePath("ut.txt");
        QVERIFY(m_vault->saveAttachment(m_soonId, a.value("id").toString(), QUrl::fromLocalFile(out)));
        QFile check(out);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("hemmelig avtale\n"));

        // Vedlegg overlever at oppføringen lagres fra skjemaet (som ikke sender data).
        QCOMPARE(m_vault->saveEntry(m_vault->entry(m_soonId)), m_soonId);
        QCOMPARE(m_vault->entry(m_soonId).value("attachments").toList().size(), 1);

        QVERIFY(m_vault->removeAttachment(m_soonId, a.value("id").toString()));
        QVERIFY(m_vault->entry(m_soonId).value("attachments").toList().isEmpty());
    }

#ifdef Q_OS_WIN
    void certificateReading()
    {
        const auto [pfx, der] = makeTestCertificate(QStringLiteral("api.kunde.example"), QStringLiteral("pfx-pw"));
        QVERIFY(!pfx.isEmpty());
        QVERIFY(!der.isEmpty());

        const CertificateInfo fromDer = readCertificate(der);
        QVERIFY2(fromDer.ok, qPrintable(fromDer.error));
        QVERIFY(fromDer.subject.contains("CN=api.kunde.example"));
        QCOMPARE(fromDer.thumbprint.size(), 40);
        QCOMPARE(fromDer.validTo.date(), fromDer.validFrom.date().addYears(2));

        const CertificateInfo fromPfx = readCertificate(pfx, QStringLiteral("pfx-pw"));
        QVERIFY2(fromPfx.ok, qPrintable(fromPfx.error));
        QCOMPARE(fromPfx.thumbprint, fromDer.thumbprint);

        const CertificateInfo wrong = readCertificate(pfx, QStringLiteral("feil"));
        QVERIFY(!wrong.ok);
        QCOMPARE(wrong.error, QStringLiteral("Feil PFX-passord."));

        // PEM = base64 av DER med header
        const QByteArray pem = "-----BEGIN CERTIFICATE-----\n" + der.toBase64() + "\n-----END CERTIFICATE-----\n";
        QCOMPARE(readCertificate(pem).thumbprint, fromDer.thumbprint);
        QVERIFY(!readCertificate("tull").ok);
    }
#endif

    void importAndExport()
    {
        const int before = m_vault->totalCount();

        // KeePass XML: 2 oppføringer
        const QVariantMap preview = m_vault->previewImport(QUrl::fromLocalFile(QFINDTESTDATA("data/keepass.xml")));
        QVERIFY(preview.value("ok").toBool());
        QCOMPARE(preview.value("count").toInt(), 2);
        const QVariantMap stats = m_vault->commitImport(true, QStringLiteral("Importkunde"), QStringLiteral("importtest"));
        QVERIFY(stats.value("ok").toBool());
        QCOMPARE(stats.value("added").toInt(), 2);
        QCOMPARE(m_vault->totalCount(), before + 2);
        QVERIFY(m_vault->customers().contains(QStringLiteral("Importkunde")));

        // Samme fil igjen: alt er duplikater
        m_vault->previewImport(QUrl::fromLocalFile(QFINDTESTDATA("data/keepass.xml")));
        QCOMPARE(m_vault->commitImport(true, {}, {}).value("skipped").toInt(), 2);
        QCOMPARE(m_vault->totalCount(), before + 2);

        // CSV-eksport
        QTemporaryDir tmp;
        const QString csv = tmp.filePath("ut.csv");
        QVERIFY(m_vault->exportCsv(QUrl::fromLocalFile(csv)));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("Importkunde"));

        // Kryptert kopi og import av den med passord
        QSignalSpy exported(m_vault, &VaultController::exportFinished);
        const QString copy = tmp.filePath("kopi.hvelv");
        m_vault->exportVault(QUrl::fromLocalFile(copy), QStringLiteral("kopipassord1"));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 20000);
        QVERIFY(exported.first().at(0).toBool());
        QVERIFY(QFile::exists(copy));
        QVERIFY(!QFile::exists(copy + ".bak"));

        QVERIFY(m_vault->previewImport(QUrl::fromLocalFile(copy)).value("needsPassword").toBool());
        QSignalSpy previewed(m_vault, &VaultController::importPreviewReady);
        m_vault->previewVaultImport(QUrl::fromLocalFile(copy), QStringLiteral("feil"));
        QTRY_COMPARE_WITH_TIMEOUT(previewed.size(), 1, 20000);
        QVERIFY(!previewed.takeFirst().at(0).toMap().value("ok").toBool());
        m_vault->previewVaultImport(QUrl::fromLocalFile(copy), QStringLiteral("kopipassord1"));
        QTRY_COMPARE_WITH_TIMEOUT(previewed.size(), 1, 20000);
        const QVariantMap vp = previewed.first().at(0).toMap();
        QVERIFY(vp.value("ok").toBool());
        QCOMPARE(vp.value("count").toInt(), m_vault->totalCount());
        // Alt i kopien finnes allerede.
        QCOMPARE(m_vault->commitImport(true, {}, {}).value("added").toInt(), 0);
    }

    void healthReportAndDialog()
    {
        const QVariantMap r = m_vault->healthReport();
        QVERIFY(r.value("checked").toInt() >= 3);
        QVERIFY(!r.value("breachesChecked").toBool());
        QVERIFY(r.value("expired").toInt() >= 1); // «Azure DevOps» er utløpt
        QVERIFY(m_vault->passwordBits("password") == 0);
        QVERIFY(m_vault->passwordBits("Hk3$uP9!qL2@vX7#rT5%") > 80);

        // Åpne dialogen for å fange QML-advarsler og ta skjermbilde.
        QObject *page = findByName(m_window, "vaultPage");
        QVERIFY(QMetaObject::invokeMethod(page, "openDialog", Q_ARG(QVariant, QStringLiteral("health"))));
        snap(QStringLiteral("ui-07-health"));
        QVERIFY(QMetaObject::invokeMethod(page, "openDialog", Q_ARG(QVariant, QStringLiteral("none"))));
    }

    void tlsErrorPath()
    {
        QSignalSpy spy(m_vault, &VaultController::tlsChecked);
        m_vault->checkTls(QStringLiteral("127.0.0.1:9")); // lukket port
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 15000);
        const QVariantMap r = spy.first().at(1).toMap();
        QVERIFY(!r.value("ok").toBool());
        QVERIFY(!r.value("error").toString().isEmpty());
        QVERIFY(m_vault->tlsTargets().contains(QStringLiteral("login.visma.net")));
    }

    void liveNetwork()
    {
        if (qEnvironmentVariable("HVELV_NETWORK_TESTS") != QLatin1String("1"))
            QSKIP("Sett HVELV_NETWORK_TESTS=1 for å teste mot internett.");
        QSignalSpy tls(m_vault, &VaultController::tlsChecked);
        m_vault->checkTls(QStringLiteral("https://example.com"));
        QTRY_COMPARE_WITH_TIMEOUT(tls.size(), 1, 20000);
        const QVariantMap r = tls.first().at(1).toMap();
        QVERIFY2(r.value("ok").toBool(), qPrintable(r.value("error").toString()));
        QVERIFY(r.value("daysLeft").toInt() > 0);
        QCOMPARE(r.value("thumbprint").toString().size(), 40);
        qInfo() << "TLS example.com:" << r.value("expiresAt") << r.value("issuer");

        // Hvelvet i testen har «S3cret!» i historikken og svake testpassord.
        QVariantMap weak = m_vault->entry(m_expiredId);
        weak["password"] = QStringLiteral("password");
        QCOMPARE(m_vault->saveEntry(weak), m_expiredId);
        QSignalSpy hibp(m_vault, &VaultController::breachCheckFinished);
        m_vault->checkBreaches();
        QTRY_COMPARE_WITH_TIMEOUT(hibp.size(), 1, 30000);
        QVERIFY2(hibp.first().at(0).toBool(), qPrintable(hibp.first().at(1).toString()));
        const QVariantMap report = m_vault->healthReport();
        QVERIFY(report.value("breachesChecked").toBool());
        QVERIFY(report.value("breached").toInt() >= 1);
        qInfo() << "HIBP:" << hibp.first().at(1).toString();
    }

    void quickSearchAndSettings()
    {
        // Vis hurtigsøket slik Auto-Type gjør når flere oppføringer passer. Vi kaller ikke
        // requestAutoType() her: den ville lest og skrevet til det ekte forgrunnsvinduet.
        m_vault->quickEntries()->setOnlyIds({m_soonId, m_expiredId});
        QCOMPARE(m_vault->quickEntries()->count(), 2);
        QVERIFY(QMetaObject::invokeMethod(m_vault, "quickSearchRequested",
                                          Q_ARG(QString, QStringLiteral("Logg inn – Testvindu")), Q_ARG(bool, true)));
        QQuickWindow *quick = nullptr;
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (w->title() == QStringLiteral("Hvelv hurtigsøk"))
                quick = qobject_cast<QQuickWindow *>(w);
        QVERIFY(quick);
        QTRY_VERIFY(quick->isVisible());
        if (!qEnvironmentVariable("HVELV_UI_SNAPSHOTS").isEmpty()) {
            QTest::qWait(300);
            QVERIFY(quick->grabWindow().save(QDir(qEnvironmentVariable("HVELV_UI_SNAPSHOTS")).filePath("ui-08-quicksearch.png")));
        }
        // Hovedlisten påvirkes ikke av hurtigsøket.
        QVERIFY(m_vault->entries()->onlyIds().isEmpty());
        quick->hide();
        m_vault->quickEntries()->setOnlyIds({});

        QCOMPARE(m_vault->validateSequence("{USERNAME}{TAB}{PASSWORD}"), QString());
        QVERIFY(!m_vault->validateSequence("{NOE}").isEmpty());

        QObject *page = findByName(m_window, "vaultPage");
        QVERIFY(QMetaObject::invokeMethod(page, "openDialog", Q_ARG(QVariant, QStringLiteral("settings"))));
        snap(QStringLiteral("ui-09-settings"));
        QVERIFY(QMetaObject::invokeMethod(page, "openDialog", Q_ARG(QVariant, QStringLiteral("none"))));
    }

    void qrCodes()
    {
        const QString png = QFINDTESTDATA("data/totp-qr.png");
        const QString expected = QStringLiteral("otpauth://totp/Hvelv:test@example.com?secret=JBSWY3DPEHPK3PXP&issuer=Hvelv");

        const QVariantMap fromFile = m_vault->scanQrFromFile(QUrl::fromLocalFile(png));
        QVERIFY2(fromFile.value("ok").toBool(), qPrintable(fromFile.value("error").toString()));
        QCOMPARE(fromFile.value("otpauth").toString(), expected);
        QVERIFY(m_vault->isValidTotp(fromFile.value("otpauth").toString()));

        // Utklippstavlen er intern i testprosessen på offscreen-plattformen.
        QGuiApplication::clipboard()->setImage(QImage(png));
        QCOMPARE(m_vault->scanQrFromClipboard().value("otpauth").toString(), expected);
        QGuiApplication::clipboard()->setText(expected);
        QCOMPARE(m_vault->scanQrFromClipboard().value("otpauth").toString(), expected);
        QGuiApplication::clipboard()->setText(QStringLiteral("bare tekst"));
        QVERIFY(!m_vault->scanQrFromClipboard().value("ok").toBool());
        QGuiApplication::clipboard()->clear();

        QImage blank(400, 300, QImage::Format_RGB32);
        blank.fill(Qt::white);
        QVERIFY(qrscanner::decode(blank).isEmpty());
        QVERIFY(!m_vault->scanQrFromFile(QUrl::fromLocalFile(QFINDTESTDATA("data/keepass.xml"))).value("ok").toBool());
    }

    // En tilleggsdatabase ved siden av hoveddatabasen: lagring, kopiering, flytting og
    // automatisk opplåsing med passordet som er lagret i hoveddatabasen.
    void multipleDatabases()
    {
        QVERIFY(m_vault->isUnlocked());
        const int mainCount = m_vault->totalCount();
        const QString path = m_dir.filePath(QStringLiteral("felles.hvelv"));
        QSignalSpy finished(m_vault, &VaultController::connectionFinished);
        m_vault->addFileConnection(QStringLiteral("Felles"), QUrl::fromLocalFile(path), QStringLiteral("fellespassord1"), true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QCOMPARE(m_vault->openSourceCount(), 2);
        const QVariantMap felles = m_vault->sources().at(1).toMap();
        const QString key = felles.value("key").toString();
        QCOMPARE(felles.value("name").toString(), QStringLiteral("Felles"));
        QVERIFY(felles.value("open").toBool());

        // Samme fil to ganger avvises.
        finished.clear();
        m_vault->addFileConnection(QStringLiteral("Igjen"), QUrl::fromLocalFile(path), QStringLiteral("fellespassord1"), false);
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.first().at(0).toBool());
        QCOMPARE(m_vault->sources().size(), 2);

        // Kontakt og oppføring i tilleggsdatabasen.
        const QString contactId = m_vault->saveContact({{"name", QStringLiteral("Per Felles")},
                                                        {"email", QStringLiteral("per@felles.no")},
                                                        {"source", key}});
        QVERIFY2(contactId.startsWith(key + ':'), qPrintable(contactId));
        QVariantMap n = m_vault->newEntry(-1, key);
        QCOMPARE(n.value("source").toString(), key);
        n["title"] = QStringLiteral("Felles-konto");
        n["password"] = QStringLiteral("delt-hemmelighet");
        n["contactIds"] = QStringList{contactId, m_contactId}; // kontakten fra hoveddatabasen kan ikke kobles
        const QString sharedId = m_vault->saveEntry(n);
        QVERIFY2(sharedId.startsWith(key + ':'), qPrintable(m_vault->errorMessage()));
        QCOMPARE(m_vault->totalCount(), mainCount + 1);
        QCOMPARE(m_vault->entry(sharedId).value("sourceName").toString(), QStringLiteral("Felles"));
        QCOMPARE(m_vault->entry(sharedId).value("contactIds").toStringList(), QStringList{contactId});

        m_vault->entries()->setSourceFilter(key);
        QCOMPARE(m_vault->entries()->count(), 1);
        m_vault->entries()->setSourceFilter({});
        QObject *page = findByName(m_window, "vaultPage");
        page->setProperty("selectedId", sharedId);
        snap(QStringLiteral("ui-30-two-databases"));

        // Kopi fra hoveddatabasen: kontakten blir med, originalen blir liggende.
        const QString copied = m_vault->copyEntryTo(m_soonId, key, false);
        QVERIFY2(copied.startsWith(key + ':'), qPrintable(m_vault->errorMessage()));
        QCOMPARE(m_vault->totalCount(), mainCount + 2);
        QVERIFY(!m_vault->entry(m_soonId).isEmpty());
        QCOMPARE(m_vault->entry(copied).value("password").toString(), m_vault->entry(m_soonId).value("password").toString());
        const QStringList copiedContacts = m_vault->entry(copied).value("contactIds").toStringList();
        QCOMPARE(copiedContacts.size(), 1);
        QVERIFY(copiedContacts.first().startsWith(key + ':'));

        // Flytt fra tilleggsdatabasen til hoveddatabasen.
        const QString moved = m_vault->copyEntryTo(sharedId, QStringLiteral("main"), true);
        QVERIFY2(!moved.isEmpty() && !moved.contains(':'), qPrintable(m_vault->errorMessage()));
        QVERIFY(m_vault->entry(sharedId).isEmpty());
        QCOMPARE(m_vault->entry(moved).value("title").toString(), QStringLiteral("Felles-konto"));
        QCOMPARE(m_vault->totalCount(), mainCount + 2);

        // Etter låsing åpnes tilleggsdatabasen automatisk sammen med hoveddatabasen.
        m_vault->lock();
        QCOMPARE(m_vault->openSourceCount(), 0);
        m_vault->unlock(QStringLiteral("pw"));
        QTRY_VERIFY_WITH_TIMEOUT(m_vault->isUnlocked(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(m_vault->openSourceCount(), 2, 20000);
        QCOMPARE(m_vault->totalCount(), mainCount + 2);
        QVERIFY(!m_vault->entry(copied).isEmpty());

        // En kryptert kopi tar ikke med passordene til tilleggsdatabasene.
        QSignalSpy exported(m_vault, &VaultController::exportFinished);
        const QString copyPath = m_dir.filePath(QStringLiteral("kopi-uten-tilkoblinger.hvelv"));
        m_vault->exportVault(QUrl::fromLocalFile(copyPath), QStringLiteral("kopipassord1"));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 20000);
        QVERIFY(exported.first().at(0).toBool());
        const auto copy = VaultFile::open(copyPath, "kopipassord1");
        QCOMPARE(copy.status, VaultFile::Status::Ok);
        QVERIFY(!copy.data.settings.contains(QStringLiteral("connections")));
        QVERIFY(!copy.data.toJson().contains("fellespassord1"));

        QTRY_VERIFY(findByName(m_window, "vaultPage"));
        page = findByName(m_window, "vaultPage");
        QMetaObject::invokeMethod(page, "openDialog", Q_ARG(QVariant, QVariant(QStringLiteral("databases"))));
        QTRY_VERIFY(findByName(m_window, "databasesDialog")->property("opened").toBool());
        snap(QStringLiteral("ui-31-databases-dialog"));
        QMetaObject::invokeMethod(page, "openDialog", Q_ARG(QVariant, QVariant(QStringLiteral("none"))));

        // Fjernet fra listen: tilbake til én database, filen ligger igjen.
        QVERIFY(m_vault->removeConnection(key));
        QCOMPARE(m_vault->openSourceCount(), 1);
        QCOMPARE(m_vault->totalCount(), mainCount + 1); // den flyttede oppføringen ligger i hoveddatabasen
        QVERIFY(QFile::exists(path));
        m_vault->lock();
        m_vault->unlock(QStringLiteral("pw"));
        QTRY_VERIFY_WITH_TIMEOUT(m_vault->isUnlocked(), 20000);
        QCOMPARE(m_vault->sources().size(), 1);
    }

    // Kopier filhvelvet til SQL Server, åpne det derfra, lagre, og opprett en ny tom database.
    void sqlServerFlow()
    {
        const QString server = qEnvironmentVariable("HVELV_SQL_SERVER");
        if (server.isEmpty())
            QSKIP("Sett HVELV_SQL_SERVER (f.eks. localhost) for å teste SQL Server.");
        const QString db = QStringLiteral("HvelvTest_ui_") + QUuid::createUuid().toString(QUuid::Id128).left(8);
        const QString db2 = db + QStringLiteral("_ny");
        const auto cleanup = qScopeGuard([&] { dropTestDatabases(server, {db, db2}); });

        QVERIFY(m_vault->isUnlocked());
        QVERIFY(!m_vault->vaultIsSql());
        const int count = m_vault->totalCount();
        QSignalSpy exported(m_vault, &VaultController::exportFinished);
        m_vault->exportToSql(server, db, QStringLiteral("sqlpassord12"));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 30000);
        QVERIFY2(exported.first().at(0).toBool(), qPrintable(exported.first().at(1).toString()));

        // Når databasen velges, låses hvelvet og opplåsingssiden viser databasen.
        m_vault->selectSqlDatabase(server, db, false);
        QVERIFY(!m_vault->isUnlocked());
        QVERIFY(m_vault->vaultIsSql());
        QVERIFY(m_vault->vaultExists());
        QVERIFY(m_vault->vaultDisplayName().contains(db));
        QCOMPARE(m_vault->sqlLocation().value("database").toString(), db);
        QTRY_VERIFY(!findByName(m_window, "vaultPage"));
        snap(QStringLiteral("ui-20-sql-unlock"));

        // Dialogen fylles ut med databasen som er valgt.
        QObject *dialog = findByName(m_window, "sqlDatabaseDialog");
        QVERIFY(dialog);
        QMetaObject::invokeMethod(dialog, "open");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(findByName(m_window, "sqlDatabaseField")->property("text").toString(), db);
        QCOMPARE(findByName(m_window, "sqlServerField")->property("text").toString(), server);
        QVERIFY(dialog->property("valid").toBool());
        snap(QStringLiteral("ui-22-sql-dialog"));
        QMetaObject::invokeMethod(dialog, "close");
        QTRY_VERIFY(!dialog->property("visible").toBool());

        m_vault->unlock(QStringLiteral("feil"));
        QTRY_VERIFY_WITH_TIMEOUT(!m_vault->isBusy(), 20000);
        QVERIFY(!m_vault->isUnlocked());
        QVERIFY(!m_vault->errorMessage().isEmpty());

        m_vault->unlock(QStringLiteral("sqlpassord12"));
        QTRY_VERIFY_WITH_TIMEOUT(m_vault->isUnlocked(), 20000);
        QVERIFY2(m_vault->errorMessage().isEmpty(), qPrintable(m_vault->errorMessage()));
        QCOMPARE(m_vault->totalCount(), count);
        QCOMPARE(m_vault->entry(m_soonId).value("contacts").toList().size(), 1);

        QVariantMap m = m_vault->entry(m_soonId);
        m["notes"] = QStringLiteral("Lagret i SQL Server");
        QCOMPARE(m_vault->saveEntry(m), m_soonId);
        QVERIFY(m_vault->deleteEntry(m_expiredId));
        m_vault->lock();
        m_vault->unlock(QStringLiteral("sqlpassord12"));
        QTRY_VERIFY_WITH_TIMEOUT(m_vault->isUnlocked(), 20000);
        QCOMPARE(m_vault->totalCount(), count - 1);
        QCOMPARE(m_vault->entry(m_soonId).value("notes").toString(), QStringLiteral("Lagret i SQL Server"));
        snap(QStringLiteral("ui-21-sql-list"));

        // Ny, tom database
        m_vault->selectSqlDatabase(server, db2, true);
        QVERIFY(!m_vault->vaultExists());
        m_vault->createVault(QStringLiteral("nytt sqlpassord"));
        QTRY_VERIFY_WITH_TIMEOUT(m_vault->isUnlocked(), 20000);
        QVERIFY(m_vault->vaultExists());
        QCOMPARE(m_vault->totalCount(), 0);

        // Ugyldig databasenavn avvises uten å røre det som er åpent.
        m_vault->selectSqlDatabase(server, QStringLiteral("x];DROP"), false);
        QVERIFY(m_vault->isUnlocked());
        QVERIFY(!m_vault->errorMessage().isEmpty());
        m_vault->clearError();
    }

    void lockClearsEverything()
    {
        m_vault->lock();
        QVERIFY(!m_vault->isUnlocked());
        QCOMPARE(m_vault->entries()->count(), 0);
        QCOMPARE(m_vault->totalCount(), 0);
        QTRY_VERIFY(!findByName(m_window, "vaultPage"));
    }

    void cleanupTestCase()
    {
        delete m_engine;
        m_engine = nullptr;
        qInstallMessageHandler(g_previousHandler);
        QVERIFY2(g_qmlWarnings.isEmpty(), qPrintable(g_qmlWarnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
