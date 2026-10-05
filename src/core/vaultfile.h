#pragma once

#include "crypto.h"
#include "vaultdata.h"

#include <QString>

namespace hvelv {

// Filformat (little endian):
//
//   params   : magic "HVELVDB1" | u16 format | u8 kdf | u8 reservert | u32 minne KiB
//              | u32 passes | u32 lanes | salt[16]                         (40 B)
//   nøkkel   : nonce[24] | mac[16] | innpakket hvelvnøkkel[32]             (72 B)
//   data     : nonce[24] | mac[16] | chiffertekst (JSON)
//
// Hovedpassord --Argon2id--> KEK, som låser opp en tilfeldig hvelvnøkkel.
// Hvelvnøkkelen krypterer JSON-innholdet. AD for nøkkelen er params-blokken,
// AD for dataene er params + nøkkelblokk, så enhver endring i headeren avvises.
// (Noncen er autentisert av AEAD-en selv.)
class VaultFile
{
public:
    enum class Status { Ok, IoError, BadFormat, UnsupportedVersion, WrongPassword, Corrupt };

    static constexpr char Magic[] = "HVELVDB1";
    static constexpr quint16 FormatVersion = 1;

    struct OpenResult;

    // Lager nye nøkler. Ingenting skrives før save().
    static VaultFile create(const QString &path, const QByteArray &masterPassword,
                            const crypto::KdfParams &params = {});
    static OpenResult open(const QString &path, const QByteArray &masterPassword);

    Status save(const VaultData &data, QString *errorString = nullptr) const;

    // Ny KEK og nytt salt. Hvelvnøkkelen beholdes. Skriver filen på nytt.
    Status changePassword(const QByteArray &newPassword, const VaultData &data,
                          const crypto::KdfParams &params = {});

    void close();
    bool isOpen() const { return m_vaultKey.size() == crypto::KeySize; }
    QString path() const { return m_path; }
    crypto::KdfParams kdfParams() const { return m_params; }

    static QString statusText(Status s);

private:
    QByteArray paramsBlock() const;
    QByteArray keyBlock() const;
    void wrapKey(const QByteArray &kek);

    QString m_path;
    crypto::KdfParams m_params;
    QByteArray m_salt;
    QByteArray m_wrapNonce;
    QByteArray m_wrapMac;
    QByteArray m_wrappedKey;
    QByteArray m_vaultKey;
};

struct VaultFile::OpenResult
{
    Status status = Status::IoError;
    VaultFile file;
    VaultData data;
};

} // namespace hvelv
