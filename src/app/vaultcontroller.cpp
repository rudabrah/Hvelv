#include "vaultcontroller.h"

#include "entryfiltermodel.h"
#include "entrymodel.h"
#include "entrytypes.h"
#include "filestore.h"
#include "passwordgenerator.h"
#include "sqlstore.h"
#include "totp.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeData>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QtConcurrent>

#include <climits>

using namespace hvelv;

namespace {

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

// Ber Windows om å holde kopierte hemmeligheter utenfor utklippshistorikk (Win+V) og skysynk.
QMimeData *secretMimeData(const QString &text)
{
    auto *mime = new QMimeData;
    mime->setText(text);
#ifdef Q_OS_WIN
    const QByteArray zeroDword(4, '\0');
    mime->setData(QStringLiteral("application/x-qt-windows-mime;value=\"ExcludeClipboardContentFromMonitorProcessing\""), zeroDword);
    mime->setData(QStringLiteral("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\""), zeroDword);
    mime->setData(QStringLiteral("application/x-qt-windows-mime;value=\"CanUploadToCloudClipboard\""), zeroDword);
#endif
    return mime;
}

QString describeExpiry(const Entry &e)
{
    const int d = e.daysUntilExpiry();
    if (d < 0)
        return QStringLiteral("%1 – utløpt for %2 dager siden").arg(e.title).arg(-d);
    if (d == 0)
        return QStringLiteral("%1 – utløper i dag").arg(e.title);
    return QStringLiteral("%1 – utløper om %2 dager").arg(e.title).arg(d);
}

// Åpner eller oppretter hvelvet på path: fil eller "sqlserver://server/database".
struct Opened
{
    VaultStore::Result result;
    std::shared_ptr<VaultStore> store;
    QStringList rejected; // endret utenom Hvelv (bare SQL)
};

Opened openOrCreate(const QString &path, const QByteArray &password, bool create)
{
    Opened o;
    if (SqlLocation::isSqlLocation(path)) {
        const auto loc = SqlLocation::parse(path);
        if (!loc) {
            o.result = {VaultStore::Error::BadFormat, QStringLiteral("Ugyldig SQL Server-adresse: %1").arg(path)};
            return o;
        }
        auto r = create ? SqlStore::create(*loc, password) : SqlStore::open(*loc, password);
        o.result = r.result;
        o.store = std::move(r.store);
        o.rejected = r.rejected;
    } else {
        auto r = create ? FileStore::create(path, password) : FileStore::open(path, password);
        o.result = r.result;
        o.store = std::move(r.store);
    }
    return o;
}

} // namespace

VaultController::VaultController(QObject *parent)
    : QObject(parent)
    , m_model(new EntryModel(this))
    , m_filter(new EntryFilterModel(this))
{
    m_filter->setSourceModel(m_model);

    const QString saved = m_settings.value(QStringLiteral("vaultPath")).toString();
    m_path = saved.isEmpty() ? localPath(defaultVaultUrl()) : saved;

    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (isUnlocked()) {
            lock();
            emit toast(QStringLiteral("Hvelvet ble låst etter %1 min uten aktivitet.").arg(autoLockMinutes()));
        }
    });

    m_clipboardTimer.setSingleShot(true);
    connect(&m_clipboardTimer, &QTimer::timeout, this, [this] {
        auto *cb = QGuiApplication::clipboard();
        if (!m_lastClipboard.isEmpty() && cb->text() == m_lastClipboard)
            cb->clear();
        m_lastClipboard.clear();
    });

    // Sjekker hvert kvarter om dagen har skiftet (utløpsstatus) og om noe skal varsles.
    m_refreshTimer.setInterval(15 * 60 * 1000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &VaultController::dailyRefresh);

    qApp->installEventFilter(this);
    initAutoType();
}

VaultController::~VaultController()
{
    if (!m_lastClipboard.isEmpty() && QGuiApplication::clipboard()->text() == m_lastClipboard)
        QGuiApplication::clipboard()->clear();
    m_store.reset();
}

void VaultController::setTrayIcon(QSystemTrayIcon *tray)
{
    m_tray = tray;
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &VaultController::showAttentionRequested);
}

bool VaultController::vaultExists() const
{
    // En SQL-database kan ikke sjekkes uten å koble til; brukeren har sagt om den skal opprettes.
    return vaultIsSql() ? !m_sqlCreate : QFileInfo::exists(m_path);
}

bool VaultController::vaultIsSql() const
{
    return SqlLocation::isSqlLocation(m_path);
}

QString VaultController::vaultDisplayName() const
{
    if (const auto loc = SqlLocation::parse(m_path))
        return QStringLiteral("%1 på %2 (SQL Server)").arg(loc->database, loc->server);
    return m_path;
}

int VaultController::autoLockMinutes() const
{
    return m_settings.value(QStringLiteral("autoLockMinutes"), 10).toInt();
}

void VaultController::setAutoLockMinutes(int minutes)
{
    m_settings.setValue(QStringLiteral("autoLockMinutes"), qBound(1, minutes, 240));
    if (isUnlocked())
        m_idleTimer.start(autoLockMinutes() * 60 * 1000);
    emit settingsChanged();
}

int VaultController::clipboardSeconds() const
{
    return m_settings.value(QStringLiteral("clipboardSeconds"), 30).toInt();
}

void VaultController::setClipboardSeconds(int seconds)
{
    m_settings.setValue(QStringLiteral("clipboardSeconds"), qBound(5, seconds, 600));
    emit settingsChanged();
}

QUrl VaultController::defaultVaultUrl() const
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(QDir(docs).filePath(QStringLiteral("Hvelv/mitt-hvelv.hvelv")));
}

void VaultController::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void VaultController::setError(const QString &message)
{
    if (m_error != message) {
        m_error = message;
        emit errorMessageChanged();
    }
}

void VaultController::clearError()
{
    setError({});
}

void VaultController::setPath(const QString &path, bool remember)
{
    if (SqlLocation::isSqlLocation(path)) {
        const auto loc = SqlLocation::parse(path);
        m_path = loc ? loc->toString() : path;
    } else {
        m_path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        m_sqlCreate = false;
    }
    if (remember)
        m_settings.setValue(QStringLiteral("vaultPath"), m_path);
    emit vaultPathChanged();
}

void VaultController::useVaultPathForSession(const QString &path)
{
    m_sessionOnlyPath = true;
    setPath(path, false);
}

void VaultController::selectVaultFile(const QUrl &url)
{
    if (isUnlocked())
        lock();
    clearError();
    m_sessionOnlyPath = false;
    QString path = localPath(url);
    if (!QFileInfo::exists(path) && !path.endsWith(QLatin1String(".hvelv"), Qt::CaseInsensitive))
        path += QStringLiteral(".hvelv");
    setPath(path);
}

void VaultController::selectSqlDatabase(const QString &server, const QString &database, bool create)
{
    const SqlLocation loc{server.trimmed(), database.trimmed()};
    if (loc.server.isEmpty() || !SqlLocation::isValidDatabaseName(loc.database)) {
        setError(QStringLiteral("Oppgi server og et gyldig databasenavn (bokstaver, tall, _ og -)."));
        return;
    }
    if (isUnlocked())
        lock();
    clearError();
    m_sessionOnlyPath = false;
    m_sqlCreate = create;
    setPath(loc.toString());
}

QVariantMap VaultController::sqlLocation() const
{
    const auto loc = SqlLocation::parse(m_path);
    return {{QStringLiteral("server"), loc ? loc->server : QStringLiteral("localhost")},
            {QStringLiteral("database"), loc ? loc->database : QStringLiteral("HvelvPersonlig")}};
}

bool VaultController::isValidDatabaseName(const QString &name) const
{
    return SqlLocation::isValidDatabaseName(name);
}

void VaultController::createVault(const QString &masterPassword)
{
    if (m_busy)
        return;
    const QString path = m_path;
    if (!vaultIsSql() && QFileInfo::exists(path)) {
        setError(QStringLiteral("Filen finnes allerede. Velg «Åpne» for å låse den opp."));
        return;
    }
    if (isUnlocked())
        lock();
    clearError();
    setBusy(true);

    const QByteArray pw = masterPassword.toUtf8();
    auto *watcher = new QFutureWatcher<Opened>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, path] {
        Opened r = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!r.result.ok()) {
            setError(r.result.message);
            return;
        }
        m_sqlCreate = false;
        setPath(path, !m_sessionOnlyPath);
        m_store = std::move(r.store);
        m_model->setContacts({});
        m_model->setEntries({});
        recount();
        m_idleTimer.start(autoLockMinutes() * 60 * 1000);
        m_refreshTimer.start();
        emit unlockedChanged();
        emit dataChanged();
        emit toast(QStringLiteral("Nytt hvelv opprettet."));
    });
    watcher->setFuture(QtConcurrent::run([path, pw] { return openOrCreate(path, pw, true); }));
}

void VaultController::unlock(const QString &masterPassword)
{
    if (m_busy || isUnlocked())
        return;
    clearError();
    setBusy(true);

    const QByteArray pw = masterPassword.toUtf8();
    const QString path = m_path;
    auto *watcher = new QFutureWatcher<Opened>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        Opened r = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!r.result.ok()) {
            setError(r.result.message);
            return;
        }
        m_store = std::move(r.store);
        m_model->setContacts(data().contacts);
        m_model->setEntries(data().entries);
        recount();
        emit dataChanged();
        m_lastRefreshDate = QDate::currentDate();
        m_idleTimer.start(autoLockMinutes() * 60 * 1000);
        m_refreshTimer.start();
        emit unlockedChanged();
        checkReminders();
        if (!r.rejected.isEmpty())
            setError(QStringLiteral("%1 ble ikke lastet fordi de er endret utenom Hvelv: %2")
                         .arg(r.rejected.size() == 1 ? QStringLiteral("1 oppføring")
                                                     : QStringLiteral("%1 oppføringer").arg(r.rejected.size()),
                              r.rejected.join(QStringLiteral(", "))));
    });
    watcher->setFuture(QtConcurrent::run([path, pw] { return openOrCreate(path, pw, false); }));
}

void VaultController::lock()
{
    if (!isUnlocked())
        return;
    m_store.reset();
    m_model->setEntries({});
    m_model->setContacts({});
    m_filter->setSearchText({});
    m_filter->setOwnerFilter({});
    m_filter->setCustomerFilter({});
    m_quickFilter->setSearchText({});
    m_quickFilter->setOnlyIds({});
    m_autoTypeTarget = 0;
    m_filter->setFilter(QStringLiteral("all"));
    m_breachCounts.clear();
    m_breachesChecked = false;
    m_idleTimer.stop();
    m_refreshTimer.stop();
    if (!m_lastClipboard.isEmpty() && QGuiApplication::clipboard()->text() == m_lastClipboard)
        QGuiApplication::clipboard()->clear();
    m_lastClipboard.clear();
    recount();
    emit unlockedChanged();
    emit dataChanged();
}

void VaultController::changeMasterPassword(const QString &current, const QString &next)
{
    if (!isUnlocked() || m_busy)
        return;
    clearError();
    setBusy(true);
    const QByteArray cur = current.toUtf8();
    const QByteArray nxt = next.toUtf8();
    // Kopien av pekeren holder hvelvet i live selv om det låses mens Argon2 kjører.
    // commit() avviser endringer mens vi er opptatt, så ingen andre skriver samtidig.
    const std::shared_ptr<VaultStore> store = m_store;

    auto *watcher = new QFutureWatcher<VaultStore::Result>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const VaultStore::Result r = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!r.ok()) {
            setError(r.error == VaultStore::Error::WrongPassword
                         ? QStringLiteral("Nåværende hovedpassord er feil.")
                         : r.message);
            return;
        }
        emit masterPasswordChanged();
        emit toast(QStringLiteral("Hovedpassordet er byttet."));
    });
    watcher->setFuture(QtConcurrent::run([store, cur, nxt] { return store->changePassword(cur, nxt); }));
}

bool VaultController::commit(const ChangeSet &changes)
{
    if (!m_store)
        return false;
    if (m_busy) {
        setError(QStringLiteral("Vent til forrige operasjon er ferdig."));
        return false;
    }
    const VaultStore::Result r = m_store->apply(changes);
    if (!r.ok()) {
        setError(QStringLiteral("Kunne ikke lagre hvelvet: %1").arg(r.message));
        return false;
    }
    return true;
}

const VaultData &VaultController::data() const
{
    static const VaultData empty;
    return m_store ? m_store->data() : empty;
}

void VaultController::recount()
{
    int soon = 0, expired = 0;
    for (const auto &e : std::as_const(data().entries)) {
        switch (e.expiryStatus()) {
        case ExpiryStatus::ExpiringSoon: ++soon; break;
        case ExpiryStatus::Expired: ++expired; break;
        default: break;
        }
    }
    m_expiring = soon;
    m_expired = expired;
    emit countsChanged();
}

void VaultController::dailyRefresh()
{
    if (!isUnlocked())
        return;
    const QDate today = QDate::currentDate();
    if (today != m_lastRefreshDate) {
        m_lastRefreshDate = today;
        m_model->refreshAll();
        m_filter->invalidate();
        recount();
    }
    checkReminders();
}

void VaultController::checkReminders()
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    QStringList lines;
    for (const auto &e : std::as_const(data().entries)) {
        const auto status = e.expiryStatus();
        if (status != ExpiryStatus::ExpiringSoon && status != ExpiryStatus::Expired)
            continue;
        if (e.reminderChannel == ReminderChannel::Email)
            continue; // e-post håndteres av serveren (fase 2)
        const QString key = e.id + QLatin1Char('|') + today;
        if (m_notified.contains(key))
            continue;
        m_notified.insert(key);
        lines << describeExpiry(e);
    }
    if (lines.isEmpty())
        return;

    const QString title = lines.size() == 1 ? QStringLiteral("1 konto krever oppmerksomhet")
                                            : QStringLiteral("%1 kontoer krever oppmerksomhet").arg(lines.size());
    QStringList shown = lines.mid(0, 4);
    if (lines.size() > 4)
        shown << QStringLiteral("… og %1 til").arg(lines.size() - 4);
    if (m_tray && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(title, shown.join(QLatin1Char('\n')), QSystemTrayIcon::Warning, 15000);
    emit toast(title);
}

void VaultController::copyToClipboard(const QString &text, bool sensitive)
{
    if (text.isEmpty())
        return;
    auto *cb = QGuiApplication::clipboard();
    if (sensitive) {
        cb->setMimeData(secretMimeData(text));
        m_lastClipboard = text;
        m_clipboardTimer.start(clipboardSeconds() * 1000);
        emit toast(QStringLiteral("Kopiert – tømmes om %1 sekunder.").arg(clipboardSeconds()));
    } else {
        cb->setText(text);
        emit toast(QStringLiteral("Kopiert."));
    }
}

void VaultController::copyField(const QString &id, const QString &field)
{
    const Entry *e = data().entry(id);
    if (!e)
        return;
    if (field == QLatin1String("password"))
        copyToClipboard(e->password, true);
    else if (field == QLatin1String("username"))
        copyToClipboard(e->username, false);
    else if (field == QLatin1String("totp")) {
        const auto cfg = totp::parse(e->totpSecret);
        if (cfg)
            copyToClipboard(totp::code(*cfg, QDateTime::currentSecsSinceEpoch()), true);
    } else if (field.startsWith(QLatin1String("fields."))) {
        const QString key = field.mid(7);
        bool secret = true;
        for (const auto &f : entrytypes::type(e->type)[QStringLiteral("fields")].toArray())
            if (f.toObject()[QStringLiteral("key")].toString() == key)
                secret = f.toObject()[QStringLiteral("secret")].toBool();
        copyToClipboard(e->fields.value(key).toString(), secret);
    } else if (field.startsWith(QLatin1String("custom."))) {
        const int i = field.mid(7).toInt();
        if (i >= 0 && i < e->customFields.size())
            copyToClipboard(e->customFields.at(i).value, e->customFields.at(i).secret);
    } else if (field.startsWith(QLatin1String("history."))) {
        const int i = field.mid(8).toInt();
        if (i >= 0 && i < e->passwordHistory.size())
            copyToClipboard(e->passwordHistory.at(i).password, true);
    }
}

QVariantMap VaultController::totp(const QString &id) const
{
    const Entry *e = data().entry(id);
    if (!e)
        return {{QStringLiteral("valid"), false}};
    const auto cfg = totp::parse(e->totpSecret);
    if (!cfg)
        return {{QStringLiteral("valid"), false}};
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return {
        {QStringLiteral("valid"), true},
        {QStringLiteral("code"), totp::code(*cfg, now)},
        {QStringLiteral("remaining"), totp::secondsRemaining(*cfg, now)},
        {QStringLiteral("period"), cfg->period},
    };
}

bool VaultController::isValidTotp(const QString &secret) const
{
    return secret.trimmed().isEmpty() || totp::parse(secret).has_value();
}

QString VaultController::generatePassword(int length, bool lower, bool upper, bool digits,
                                          bool symbols, bool avoidAmbiguous) const
{
    PasswordOptions o;
    o.length = qBound(4, length, 128);
    o.lower = lower;
    o.upper = upper;
    o.digits = digits;
    o.symbols = symbols;
    o.avoidAmbiguous = avoidAmbiguous;
    return hvelv::generatePassword(o);
}

void VaultController::openUrl(const QString &url) const
{
    QUrl u = QUrl::fromUserInput(url.trimmed());
    if (u.isValid() && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https")))
        QDesktopServices::openUrl(u);
}

bool VaultController::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        if (isUnlocked())
            m_idleTimer.start(autoLockMinutes() * 60 * 1000);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}
