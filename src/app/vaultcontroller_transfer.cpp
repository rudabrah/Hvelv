// VaultController: import og eksport.

#include "vaultcontroller.h"

#include "calendar.h"
#include "entrymodel.h"
#include "vaultfile.h"

#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QDesktopServices>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrent>

using namespace hvelv;

namespace {

constexpr qint64 MaxImportBytes = 200 * 1024 * 1024;

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QVariantMap previewMap(const importexport::ImportResult &r)
{
    if (!r.ok)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), r.error}};
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("format"), r.format},
        {QStringLiteral("count"), int(r.entries.size())},
        {QStringLiteral("contactCount"), int(r.contacts.size())},
        {QStringLiteral("warnings"), r.warnings},
    };
}

} // namespace

QVariantMap VaultController::previewImport(const QUrl &file)
{
    m_pendingImport = {};
    const QString path = localPath(file);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Kunne ikke lese filen.")}};
    if (f.size() > MaxImportBytes)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Filen er for stor.")}};
    const QByteArray head = f.peek(8);
    if (head == "HVELVDB1")
        return {{QStringLiteral("ok"), false}, {QStringLiteral("needsPassword"), true},
                {QStringLiteral("format"), QStringLiteral("Hvelv")}};
    QByteArray data = f.readAll();
    m_pendingImport = importexport::importData(data, QFileInfo(path).fileName());
    data.fill('\0'); // klartekst-passord fra eksportfilen
    return previewMap(m_pendingImport);
}

void VaultController::previewVaultImport(const QUrl &file, const QString &password)
{
    if (m_busy)
        return;
    const QString path = localPath(file);
    if (QFileInfo(path).canonicalFilePath() == QFileInfo(m_path).canonicalFilePath()) {
        emit importPreviewReady({{QStringLiteral("ok"), false},
                                 {QStringLiteral("error"), QStringLiteral("Dette er hvelvet som allerede er åpent.")}});
        return;
    }
    setBusy(true);
    const QByteArray pw = password.toUtf8();
    auto *watcher = new QFutureWatcher<VaultFile::OpenResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        VaultFile::OpenResult r = watcher->result();
        watcher->deleteLater();
        r.file.close();
        setBusy(false);
        m_pendingImport = {};
        if (r.status != VaultFile::Status::Ok) {
            emit importPreviewReady({{QStringLiteral("ok"), false},
                                     {QStringLiteral("needsPassword"), r.status == VaultFile::Status::WrongPassword},
                                     {QStringLiteral("error"), VaultFile::statusText(r.status)}});
            return;
        }
        m_pendingImport.ok = true;
        m_pendingImport.format = QStringLiteral("Hvelv");
        m_pendingImport.entries = r.data.entries;
        m_pendingImport.contacts = r.data.contacts;
        emit importPreviewReady(previewMap(m_pendingImport));
    });
    watcher->setFuture(QtConcurrent::run([path, pw] { return VaultFile::open(path, pw); }));
}

QVariantMap VaultController::commitImport(bool skipDuplicates, const QString &customer, const QString &tag)
{
    if (!isUnlocked() || !m_pendingImport.ok)
        return {{QStringLiteral("ok"), false}};
    importexport::MergeOptions options;
    options.skipDuplicates = skipDuplicates;
    options.customer = customer;
    options.tag = tag;
    // merge() legger bare til, så det nye er det som kommer etter det som fantes fra før.
    VaultData merged = data();
    const auto stats = importexport::merge(merged, m_pendingImport, options);
    ChangeSet changes;
    changes.upsertEntries = merged.entries.mid(data().entries.size());
    changes.upsertContacts = merged.contacts.mid(data().contacts.size());
    if (!commit(changes))
        return {{QStringLiteral("ok"), false}};
    m_pendingImport = {};
    m_model->setContacts(data().contacts);
    m_model->setEntries(data().entries);
    afterDataChange();
    emit toast(stats.skipped > 0
                   ? QStringLiteral("Importerte %1 oppføringer (%2 duplikater hoppet over).").arg(stats.added).arg(stats.skipped)
                   : QStringLiteral("Importerte %1 oppføringer.").arg(stats.added));
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("added"), stats.added},
            {QStringLiteral("skipped"), stats.skipped},
            {QStringLiteral("contactsAdded"), stats.contactsAdded}};
}

void VaultController::cancelImport()
{
    m_pendingImport = {};
}

bool VaultController::exportCsv(const QUrl &target)
{
    if (!isUnlocked())
        return false;
    QSaveFile out(localPath(target));
    QByteArray csv = importexport::exportCsv(data());
    const bool ok = out.open(QIODevice::WriteOnly) && out.write(csv) == csv.size() && out.commit();
    csv.fill('\0');
    if (!ok) {
        setError(QStringLiteral("Kunne ikke skrive filen: %1").arg(out.errorString()));
        return false;
    }
    emit toast(QStringLiteral("Eksporterte %1 oppføringer til CSV (ukryptert!).").arg(data().entries.size()));
    return true;
}

void VaultController::exportVault(const QUrl &target, const QString &password)
{
    if (!isUnlocked() || m_busy)
        return;
    QString path = localPath(target);
    if (!path.endsWith(QLatin1String(".hvelv"), Qt::CaseInsensitive))
        path += QStringLiteral(".hvelv");
    if (QFileInfo(path).canonicalFilePath() == QFileInfo(m_path).canonicalFilePath()) {
        emit exportFinished(false, QStringLiteral("Velg en annen fil enn hvelvet som er åpent."));
        return;
    }
    setBusy(true);
    const QByteArray pw = password.toUtf8();
    const VaultData snapshot = data();
    auto *watcher = new QFutureWatcher<VaultFile::Status>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, path] {
        const auto status = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (status == VaultFile::Status::Ok)
            emit exportFinished(true, QStringLiteral("Kryptert kopi lagret: %1").arg(QFileInfo(path).fileName()));
        else
            emit exportFinished(false, VaultFile::statusText(status));
    });
    watcher->setFuture(QtConcurrent::run([path, pw, snapshot] {
        // Fjern en eventuell gammel fil først, så .bak-logikken ikke tar vare på et annet hvelv.
        QFile::remove(path);
        VaultFile f = VaultFile::create(path, pw);
        const auto status = f.save(snapshot);
        f.close();
        QFile::remove(path + QStringLiteral(".bak"));
        return status;
    }));
}

bool VaultController::exportCalendar(const QUrl &target, bool includeExpired)
{
    if (!isUnlocked())
        return false;
    const QByteArray ics = calendar::buildIcs(data(), {}, includeExpired);
    QSaveFile out(localPath(target));
    if (!out.open(QIODevice::WriteOnly) || out.write(ics) != ics.size() || !out.commit()) {
        setError(QStringLiteral("Kunne ikke skrive filen: %1").arg(out.errorString()));
        return false;
    }
    emit toast(QStringLiteral("Kalenderfil lagret. Åpne den i Outlook for å legge inn avtalene."));
    return true;
}

bool VaultController::openInCalendar(const QString &entryId)
{
    const Entry *e = data().entry(entryId);
    if (!e || !e->expiresAt.isValid())
        return false;
    // Midlertidig fil uten hemmeligheter; Outlook åpner den som en avtale som kan lagres.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QDir(dir).filePath(QStringLiteral("hvelv-%1.ics").arg(e->id.left(8)));
    QSaveFile out(path);
    const QByteArray ics = calendar::buildIcs(data(), {entryId}, true);
    if (!out.open(QIODevice::WriteOnly) || out.write(ics) != ics.size() || !out.commit())
        return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
