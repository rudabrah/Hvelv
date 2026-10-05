#pragma once

#include "importexport.h"
#include "vaultstore.h"

#include <QFutureWatcher>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class EntryModel;
class EntryFilterModel;
class QSystemTrayIcon;
class NetworkChecks;
class GlobalHotkeys;
class AutoTyper;

class VaultController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Vault)
    QML_SINGLETON

    Q_PROPERTY(QString vaultPath READ vaultPath NOTIFY vaultPathChanged)
    Q_PROPERTY(bool vaultExists READ vaultExists NOTIFY vaultPathChanged)
    Q_PROPERTY(bool vaultIsSql READ vaultIsSql NOTIFY vaultPathChanged)
    Q_PROPERTY(QString vaultDisplayName READ vaultDisplayName NOTIFY vaultPathChanged)
    Q_PROPERTY(bool unlocked READ isUnlocked NOTIFY unlockedChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(EntryFilterModel *entries READ entries CONSTANT)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int expiringCount READ expiringCount NOTIFY countsChanged)
    Q_PROPERTY(int expiredCount READ expiredCount NOTIFY countsChanged)
    Q_PROPERTY(int autoLockMinutes READ autoLockMinutes WRITE setAutoLockMinutes NOTIFY settingsChanged)
    Q_PROPERTY(int clipboardSeconds READ clipboardSeconds WRITE setClipboardSeconds NOTIFY settingsChanged)

    // Data til skjemaer og filtre
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY dataChanged)
    Q_PROPERTY(QStringList customers READ customers NOTIFY dataChanged)
    Q_PROPERTY(QStringList systems READ systems NOTIFY dataChanged)
    Q_PROPERTY(QStringList owners READ owners NOTIFY dataChanged)
    Q_PROPERTY(QVariantList entryTypes READ entryTypes CONSTANT)
    Q_PROPERTY(QVariantList templates READ templates CONSTANT)
    Q_PROPERTY(bool networkBusy READ isNetworkBusy NOTIFY networkBusyChanged)

    // Auto-Type og hurtigsøk
    Q_PROPERTY(EntryFilterModel *quickEntries READ quickEntries CONSTANT)
    Q_PROPERTY(QString hotkeySearch READ hotkeySearch WRITE setHotkeySearch NOTIFY settingsChanged)
    Q_PROPERTY(QString hotkeyAutoType READ hotkeyAutoType WRITE setHotkeyAutoType NOTIFY settingsChanged)
    Q_PROPERTY(QString hotkeyStatus READ hotkeyStatus NOTIFY settingsChanged)
    Q_PROPERTY(QString autoTypeSequence READ autoTypeSequence WRITE setAutoTypeSequence NOTIFY settingsChanged)

public:
    explicit VaultController(QObject *parent = nullptr);
    ~VaultController() override;

    void setTrayIcon(QSystemTrayIcon *tray);
    // Fra kommandolinjen: brukes for denne økten, lagres ikke som standard.
    void useVaultPathForSession(const QString &path);

    QString vaultPath() const { return m_path; }
    bool vaultExists() const;
    bool vaultIsSql() const;
    QString vaultDisplayName() const;
    bool isUnlocked() const { return m_store != nullptr; }
    bool isBusy() const { return m_busy; }
    QString errorMessage() const { return m_error; }
    EntryFilterModel *entries() const { return m_filter; }
    int totalCount() const { return int(data().entries.size()); }
    int expiringCount() const { return m_expiring; }
    int expiredCount() const { return m_expired; }
    int autoLockMinutes() const;
    void setAutoLockMinutes(int minutes);
    int clipboardSeconds() const;
    void setClipboardSeconds(int seconds);

    QVariantList contacts() const;
    QStringList customers() const;
    QStringList systems() const;
    QStringList owners() const;
    QVariantList entryTypes() const;
    QVariantList templates() const;
    bool isNetworkBusy() const;
    EntryFilterModel *quickEntries() const { return m_quickFilter; }
    QString hotkeySearch() const;
    void setHotkeySearch(const QString &keys);
    QString hotkeyAutoType() const;
    void setHotkeyAutoType(const QString &keys);
    QString hotkeyStatus() const { return m_hotkeyStatus; }
    QString autoTypeSequence() const;
    void setAutoTypeSequence(const QString &sequence);

    // Hvelvfil
    Q_INVOKABLE void selectVaultFile(const QUrl &url);
    Q_INVOKABLE void createVault(const QString &masterPassword); // på vaultPath
    // SQL Server: velger databasen (create = opprettes ved createVault i stedet for å åpnes).
    Q_INVOKABLE void selectSqlDatabase(const QString &server, const QString &database, bool create);
    Q_INVOKABLE QVariantMap sqlLocation() const; // {server, database} for gjeldende eller forslag
    Q_INVOKABLE bool isValidDatabaseName(const QString &name) const;
    Q_INVOKABLE void unlock(const QString &masterPassword);
    Q_INVOKABLE void lock();
    Q_INVOKABLE void changeMasterPassword(const QString &current, const QString &next);
    Q_INVOKABLE QUrl defaultVaultUrl() const;

    // Oppføringer
    Q_INVOKABLE QVariantMap newEntry(int templateIndex = -1) const;
    Q_INVOKABLE QVariantMap entryType(const QString &key) const;
    Q_INVOKABLE QVariantMap entry(const QString &id) const;
    Q_INVOKABLE QString saveEntry(const QVariantMap &map); // returnerer id, eller tom streng ved feil
    Q_INVOKABLE bool deleteEntry(const QString &id);

    // Kontakter
    Q_INVOKABLE QVariantMap contact(const QString &id) const;
    Q_INVOKABLE QString saveContact(const QVariantMap &map); // returnerer id
    Q_INVOKABLE bool deleteContact(const QString &id);       // fjernes også fra oppføringene

    // Vedlegg (kun på lagrede oppføringer). Data sendes aldri til QML.
    Q_INVOKABLE bool addAttachment(const QString &entryId, const QUrl &file);
    Q_INVOKABLE bool saveAttachment(const QString &entryId, const QString &attachmentId, const QUrl &target);
    Q_INVOKABLE bool removeAttachment(const QString &entryId, const QString &attachmentId);

    // Import: forhåndsvis først, så commitImport(). Hvelv-filer krever passord (asynkront).
    Q_INVOKABLE QVariantMap previewImport(const QUrl &file);
    Q_INVOKABLE void previewVaultImport(const QUrl &file, const QString &password); // -> importPreviewReady
    Q_INVOKABLE QVariantMap commitImport(bool skipDuplicates, const QString &customer, const QString &tag);
    Q_INVOKABLE void cancelImport();

    // Kalender (.ics). Uten hemmeligheter.
    Q_INVOKABLE bool exportCalendar(const QUrl &target, bool includeExpired = false);
    Q_INVOKABLE bool openInCalendar(const QString &entryId); // åpner .ics i standard kalenderapp

    // Eksport
    Q_INVOKABLE bool exportCsv(const QUrl &target);
    Q_INVOKABLE void exportVault(const QUrl &target, const QString &password); // -> exportFinished
    // Ny SQL Server-database med alt innhold, låst med password. -> exportFinished
    Q_INVOKABLE void exportToSql(const QString &server, const QString &database, const QString &password);

    // Auto-Type: målvinduet huskes når hurtigtasten trykkes.
    Q_INVOKABLE void requestQuickSearch();
    Q_INVOKABLE void requestAutoType();
    Q_INVOKABLE bool autoType(const QString &entryId, bool rememberWindow = false);
    Q_INVOKABLE QString validateSequence(const QString &sequence) const; // tom = gyldig

    // QR-kode for TOTP: fra skjermen (asynkront, Hvelv skjules et øyeblikk), utklippstavlen eller fil.
    // Resultat: { ok, otpauth, error }
    Q_INVOKABLE void scanQrFromScreen();                 // -> qrScanned
    Q_INVOKABLE QVariantMap scanQrFromClipboard() const;
    Q_INVOKABLE QVariantMap scanQrFromFile(const QUrl &file) const;

    // Passordhelse
    Q_INVOKABLE QVariantMap healthReport() const;
    Q_INVOKABLE int passwordBits(const QString &password) const;
    Q_INVOKABLE void checkBreaches();                 // HIBP, k-anonymitet -> breachCheckFinished
    Q_INVOKABLE QStringList tlsTargets() const;       // unike https-verter fra oppføringene
    Q_INVOKABLE void checkTls(const QString &target); // URL eller vert[:port] -> tlsChecked

    // Leser .cer/.crt/.pem/.der/.pfx/.p12 og returnerer felt for sertifikat-typen.
    Q_INVOKABLE QVariantMap readCertificate(const QUrl &file, const QString &pfxPassword = {}) const;

    // Verktøy
    Q_INVOKABLE void copyToClipboard(const QString &text, bool sensitive = true);
    Q_INVOKABLE void copyField(const QString &id, const QString &field);
    // field: "fields.<nøkkel>", "custom.<indeks>" eller "history.<indeks>" i tillegg til de vanlige

    Q_INVOKABLE QVariantMap totp(const QString &id) const;
    Q_INVOKABLE bool isValidTotp(const QString &secret) const;
    Q_INVOKABLE QString generatePassword(int length, bool lower, bool upper, bool digits,
                                         bool symbols, bool avoidAmbiguous) const;
    Q_INVOKABLE void openUrl(const QString &url) const;
    Q_INVOKABLE void clearError();

signals:
    void vaultPathChanged();
    void unlockedChanged();
    void busyChanged();
    void errorMessageChanged();
    void countsChanged();
    void settingsChanged();
    void masterPasswordChanged();
    void dataChanged();                     // oppføringer eller kontakter er endret
    void entryChanged(const QString &id);   // f.eks. nytt vedlegg
    void importPreviewReady(const QVariantMap &preview);
    void exportFinished(bool ok, const QString &message);
    void healthChanged();
    void breachCheckFinished(bool ok, const QString &message);
    void tlsChecked(const QString &target, const QVariantMap &result);
    void networkBusyChanged();
    void quickSearchRequested(const QString &windowTitle, bool forAutoType);
    void qrScanned(const QVariantMap &result);
    void toast(const QString &message);
    void showAttentionRequested(); // fra varsel i systemkurven

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setBusy(bool busy);
    void setError(const QString &message);
    void setPath(const QString &path, bool remember = true);
    // Lagrer endringene i hvelvet som er åpent. Setter feilmelding og returnerer false ved feil.
    bool commit(const hvelv::ChangeSet &changes);
    const hvelv::VaultData &data() const; // tomt når hvelvet er låst
    void afterDataChange();            // oppdaterer modell-kontakter, tellere og signaler
    NetworkChecks *net();
    void initAutoType();
    void applyHotkeys();
    void recount();
    void checkReminders();
    void dailyRefresh();

    QSettings m_settings;
    QString m_path;
    bool m_sessionOnlyPath = false;
    bool m_sqlCreate = false; // valgt SQL-database skal opprettes, ikke åpnes
    std::shared_ptr<hvelv::VaultStore> m_store; // null = låst
    EntryModel *m_model;
    EntryFilterModel *m_filter;
    bool m_busy = false;
    QString m_error;
    int m_expiring = 0;
    int m_expired = 0;

    QTimer m_idleTimer;
    QTimer m_clipboardTimer;
    QTimer m_refreshTimer;
    QString m_lastClipboard;
    QDate m_lastRefreshDate;
    QSet<QString> m_notified; // "id|yyyy-MM-dd"
    QSystemTrayIcon *m_tray = nullptr;
    hvelv::importexport::ImportResult m_pendingImport;
    NetworkChecks *m_net = nullptr;
    QHash<QString, int> m_breachCounts; // SHA-1 -> treff i HIBP; tømmes ved låsing
    bool m_breachesChecked = false;
    EntryFilterModel *m_quickFilter = nullptr;
    GlobalHotkeys *m_hotkeys = nullptr;
    AutoTyper *m_typer = nullptr;
    quintptr m_autoTypeTarget = 0;
    QString m_autoTypeTitle;
    QString m_hotkeyStatus;
};
