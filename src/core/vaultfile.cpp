#include "vaultfile.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>

namespace hvelv {

namespace {

constexpr quint8 KdfArgon2id = 1;
constexpr int ParamsBlockSize = 8 + 2 + 1 + 1 + 4 + 4 + 4 + crypto::SaltSize;
constexpr int KeyBlockSize = crypto::NonceSize + crypto::MacSize + crypto::KeySize;
constexpr int HeaderSize = ParamsBlockSize + KeyBlockSize + crypto::NonceSize + crypto::MacSize;

// Grenser som hindrer at en manipulert fil får oss til å allokere gigabytes.
constexpr quint32 MaxMemoryKiB = 1024 * 1024;
constexpr quint32 MaxPasses = 64;
constexpr quint32 MaxLanes = 16;

} // namespace

QByteArray VaultFile::paramsBlock() const
{
    QByteArray block;
    QDataStream s(&block, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData(Magic, 8);
    s << FormatVersion << KdfArgon2id << quint8(0)
      << quint32(m_params.memoryKiB) << quint32(m_params.passes) << quint32(m_params.lanes);
    s.writeRawData(m_salt.constData(), m_salt.size());
    return block;
}

QByteArray VaultFile::keyBlock() const
{
    return m_wrapNonce + m_wrapMac + m_wrappedKey;
}

void VaultFile::wrapKey(const QByteArray &kek)
{
    const auto sealed = crypto::seal(kek, paramsBlock(), m_vaultKey);
    m_wrapNonce = sealed.nonce;
    m_wrapMac = sealed.mac;
    m_wrappedKey = sealed.cipherText;
}

VaultFile VaultFile::create(const QString &path, const QByteArray &masterPassword,
                            const crypto::KdfParams &params)
{
    VaultFile f;
    f.m_path = path;
    f.m_params = params;
    f.m_salt = crypto::randomBytes(crypto::SaltSize);
    f.m_vaultKey = crypto::randomBytes(crypto::KeySize);
    QByteArray kek = crypto::deriveKey(masterPassword, f.m_salt, params);
    f.wrapKey(kek);
    crypto::wipe(kek);
    return f;
}

VaultFile::OpenResult VaultFile::open(const QString &path, const QByteArray &masterPassword)
{
    OpenResult r;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        r.status = Status::IoError;
        return r;
    }
    const QByteArray raw = file.readAll();
    if (raw.size() < HeaderSize || !raw.startsWith(QByteArray(Magic, 8))) {
        r.status = Status::BadFormat;
        return r;
    }

    QDataStream s(raw);
    s.setByteOrder(QDataStream::LittleEndian);
    s.skipRawData(8);
    quint16 version;
    quint8 kdf, reserved;
    quint32 mem, passes, lanes;
    s >> version >> kdf >> reserved >> mem >> passes >> lanes;
    if (version != FormatVersion || kdf != KdfArgon2id) {
        r.status = Status::UnsupportedVersion;
        return r;
    }
    if (mem > MaxMemoryKiB || passes > MaxPasses || lanes == 0 || lanes > MaxLanes
        || passes == 0 || mem < 8 * lanes) {
        r.status = Status::BadFormat;
        return r;
    }

    VaultFile &f = r.file;
    f.m_path = path;
    f.m_params = {mem, passes, lanes};
    int pos = ParamsBlockSize - crypto::SaltSize;
    f.m_salt = raw.mid(pos, crypto::SaltSize);           pos += crypto::SaltSize;
    f.m_wrapNonce = raw.mid(pos, crypto::NonceSize);     pos += crypto::NonceSize;
    f.m_wrapMac = raw.mid(pos, crypto::MacSize);         pos += crypto::MacSize;
    f.m_wrappedKey = raw.mid(pos, crypto::KeySize);      pos += crypto::KeySize;
    const QByteArray dataAd = raw.left(pos);
    const QByteArray dataNonce = raw.mid(pos, crypto::NonceSize); pos += crypto::NonceSize;
    const QByteArray dataMac = raw.mid(pos, crypto::MacSize);     pos += crypto::MacSize;
    const QByteArray cipher = raw.mid(pos);

    QByteArray kek = crypto::deriveKey(masterPassword, f.m_salt, f.m_params);
    auto vaultKey = crypto::open(kek, f.paramsBlock(), f.m_wrapNonce, f.m_wrapMac, f.m_wrappedKey);
    crypto::wipe(kek);
    if (!vaultKey) {
        // Feil passord og manipulert nøkkelblokk kan ikke skilles fra hverandre.
        r.status = Status::WrongPassword;
        return r;
    }
    f.m_vaultKey = *vaultKey;

    auto plain = crypto::open(f.m_vaultKey, dataAd, dataNonce, dataMac, cipher);
    if (!plain) {
        f.close();
        r.status = Status::Corrupt;
        return r;
    }
    const bool ok = VaultData::fromJson(*plain, r.data);
    crypto::wipe(*plain);
    if (!ok) {
        f.close();
        r.status = Status::Corrupt;
        return r;
    }
    r.status = Status::Ok;
    return r;
}

VaultFile::Status VaultFile::save(const VaultData &data, QString *errorString) const
{
    if (!isOpen())
        return Status::IoError;

    QByteArray plain = data.toJson();
    const QByteArray header = paramsBlock() + keyBlock();
    const auto sealed = crypto::seal(m_vaultKey, header, plain);
    crypto::wipe(plain);

    QDir().mkpath(QFileInfo(m_path).absolutePath());

    // Behold forrige versjon som .bak i tilfelle noe går galt.
    if (QFile::exists(m_path)) {
        const QString bak = m_path + QStringLiteral(".bak");
        QFile::remove(bak);
        QFile::copy(m_path, bak);
    }

    QSaveFile out(m_path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (errorString)
            *errorString = out.errorString();
        return Status::IoError;
    }
    out.write(header);
    out.write(sealed.nonce);
    out.write(sealed.mac);
    out.write(sealed.cipherText);
    if (!out.commit()) {
        if (errorString)
            *errorString = out.errorString();
        return Status::IoError;
    }
    return Status::Ok;
}

VaultFile::Status VaultFile::changePassword(const QByteArray &newPassword, const VaultData &data,
                                            const crypto::KdfParams &params)
{
    if (!isOpen())
        return Status::IoError;
    const VaultFile previous = *this;
    m_params = params;
    m_salt = crypto::randomBytes(crypto::SaltSize);
    QByteArray kek = crypto::deriveKey(newPassword, m_salt, m_params);
    wrapKey(kek);
    crypto::wipe(kek);
    const Status s = save(data);
    if (s != Status::Ok)
        *this = previous; // filen er urørt, så den gamle nøkkelinnpakningen gjelder fortsatt
    return s;
}

void VaultFile::close()
{
    crypto::wipe(m_vaultKey);
}

QString VaultFile::statusText(Status s)
{
    switch (s) {
    case Status::Ok: return QStringLiteral("OK");
    case Status::IoError: return QStringLiteral("Kunne ikke lese eller skrive filen.");
    case Status::BadFormat: return QStringLiteral("Filen er ikke et Hvelv-hvelv.");
    case Status::UnsupportedVersion: return QStringLiteral("Hvelvet er laget med en nyere versjon av Hvelv.");
    case Status::WrongPassword: return QStringLiteral("Feil hovedpassord.");
    case Status::Corrupt: return QStringLiteral("Hvelvet er skadet eller har blitt endret.");
    }
    return {};
}

} // namespace hvelv
