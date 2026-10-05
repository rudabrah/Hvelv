#include "filestore.h"

#include <QFileInfo>

namespace hvelv {

FileStore::FileStore(VaultFile file, VaultData data)
    : m_file(std::move(file)), m_data(std::move(data))
{
}

FileStore::~FileStore()
{
    m_file.close();
}

VaultStore::Result FileStore::fromStatus(VaultFile::Status status, const QString &detail)
{
    Result r;
    switch (status) {
    case VaultFile::Status::Ok:                 return r;
    case VaultFile::Status::IoError:            r.error = Error::Io; break;
    case VaultFile::Status::BadFormat:          r.error = Error::BadFormat; break;
    case VaultFile::Status::UnsupportedVersion: r.error = Error::UnsupportedVersion; break;
    case VaultFile::Status::WrongPassword:      r.error = Error::WrongPassword; break;
    case VaultFile::Status::Corrupt:            r.error = Error::Corrupt; break;
    }
    r.message = detail.isEmpty() ? VaultFile::statusText(status) : detail;
    return r;
}

FileStore::OpenResult FileStore::create(const QString &path, const QByteArray &masterPassword,
                                        const crypto::KdfParams &params)
{
    OpenResult r;
    if (QFileInfo::exists(path)) {
        r.result = {Error::Io, QStringLiteral("Filen finnes allerede.")};
        return r;
    }
    VaultFile file = VaultFile::create(path, masterPassword, params);
    QString err;
    const auto status = file.save({}, &err);
    r.result = fromStatus(status, err);
    if (r.result.ok())
        r.store.reset(new FileStore(std::move(file), {}));
    return r;
}

FileStore::OpenResult FileStore::open(const QString &path, const QByteArray &masterPassword)
{
    OpenResult r;
    VaultFile::OpenResult opened = VaultFile::open(path, masterPassword);
    r.result = fromStatus(opened.status);
    if (r.result.ok())
        r.store.reset(new FileStore(std::move(opened.file), std::move(opened.data)));
    return r;
}

VaultStore::Result FileStore::apply(const ChangeSet &changes)
{
    if (changes.isEmpty())
        return {};
    VaultData next = m_data;
    changes.applyTo(next);
    QString err;
    const auto status = m_file.save(next, &err);
    if (status != VaultFile::Status::Ok)
        return fromStatus(status, err);
    m_data = std::move(next);
    return {};
}

VaultStore::Result FileStore::changePassword(const QByteArray &current, const QByteArray &next)
{
    // Bekreft nåværende passord mot filen på disk før vi bytter.
    const auto check = VaultFile::open(m_file.path(), current);
    if (check.status != VaultFile::Status::Ok)
        return fromStatus(check.status);
    VaultFile updated = m_file;
    const auto status = updated.changePassword(next, m_data);
    if (status != VaultFile::Status::Ok)
        return fromStatus(status);
    m_file = std::move(updated);
    return {};
}

} // namespace hvelv
