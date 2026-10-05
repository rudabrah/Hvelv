#pragma once

#include "vaultfile.h"
#include "vaultstore.h"

#include <memory>

namespace hvelv {

// Hvelv i en kryptert .hvelv-fil. Hver endring skriver hele filen på nytt.
class FileStore final : public VaultStore
{
public:
    struct OpenResult
    {
        Result result;
        std::shared_ptr<FileStore> store; // satt når result.ok()
    };

    // Lager og skriver et tomt hvelv. Feiler hvis filen finnes.
    static OpenResult create(const QString &path, const QByteArray &masterPassword,
                             const crypto::KdfParams &params = {});
    static OpenResult open(const QString &path, const QByteArray &masterPassword);

    ~FileStore() override;

    Kind kind() const override { return Kind::File; }
    QString location() const override { return m_file.path(); }
    const VaultData &data() const override { return m_data; }

    Result apply(const ChangeSet &changes) override;
    Result changePassword(const QByteArray &current, const QByteArray &next) override;

    // VaultFile-status som Result med norsk feiltekst.
    static Result fromStatus(VaultFile::Status status, const QString &detail = {});

private:
    FileStore(VaultFile file, VaultData data);

    VaultFile m_file;
    VaultData m_data;
};

} // namespace hvelv
