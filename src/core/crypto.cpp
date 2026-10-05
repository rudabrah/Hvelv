#include "crypto.h"

#include <QRandomGenerator>
#include <monocypher.h>

#include <memory>
#include <stdexcept>

namespace hvelv::crypto {

namespace {
const uint8_t *u8(const QByteArray &b) { return reinterpret_cast<const uint8_t *>(b.constData()); }
uint8_t *u8(QByteArray &b) { return reinterpret_cast<uint8_t *>(b.data()); }
} // namespace

QByteArray randomBytes(int size)
{
    QByteArray out(size, Qt::Uninitialized);
    // QRandomGenerator::system() bruker OS-ets CSPRNG (BCryptGenRandom på Windows).
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(out.data()), size / 4);
    for (int i = size - size % 4; i < size; ++i)
        out[i] = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    return out;
}

QByteArray deriveKey(const QByteArray &password, const QByteArray &salt, const KdfParams &params)
{
    if (salt.size() != SaltSize)
        throw std::invalid_argument("ugyldig saltlengde");
    if (params.memoryKiB < 8 * params.lanes || params.passes < 1 || params.lanes < 1)
        throw std::invalid_argument("ugyldige KDF-parametre");

    const size_t workSize = size_t(params.memoryKiB) * 1024;
    std::unique_ptr<uint8_t[]> work(new uint8_t[workSize]);

    crypto_argon2_config config{CRYPTO_ARGON2_ID, params.memoryKiB, params.passes, params.lanes};
    crypto_argon2_inputs inputs{u8(password), u8(salt), uint32_t(password.size()), uint32_t(salt.size())};

    QByteArray key(KeySize, Qt::Uninitialized);
    crypto_argon2(u8(key), KeySize, work.get(), config, inputs, crypto_argon2_no_extras);
    crypto_wipe(work.get(), workSize);
    return key;
}

Sealed seal(const QByteArray &key, const QByteArray &associatedData, const QByteArray &plainText)
{
    if (key.size() != KeySize)
        throw std::invalid_argument("ugyldig nøkkellengde");

    Sealed s;
    s.nonce = randomBytes(NonceSize);
    s.mac = QByteArray(MacSize, Qt::Uninitialized);
    s.cipherText = QByteArray(plainText.size(), Qt::Uninitialized);
    crypto_aead_lock(u8(s.cipherText), u8(s.mac), u8(key), u8(s.nonce),
                     u8(associatedData), size_t(associatedData.size()),
                     u8(plainText), size_t(plainText.size()));
    return s;
}

std::optional<QByteArray> open(const QByteArray &key, const QByteArray &associatedData,
                               const QByteArray &nonce, const QByteArray &mac,
                               const QByteArray &cipherText)
{
    if (key.size() != KeySize || nonce.size() != NonceSize || mac.size() != MacSize)
        return std::nullopt;

    QByteArray plain(cipherText.size(), Qt::Uninitialized);
    const int rc = crypto_aead_unlock(u8(plain), u8(mac), u8(key), u8(nonce),
                                      u8(associatedData), size_t(associatedData.size()),
                                      u8(cipherText), size_t(cipherText.size()));
    if (rc != 0) {
        wipe(plain);
        return std::nullopt;
    }
    return plain;
}

void wipe(QByteArray &data)
{
    if (!data.isEmpty())
        crypto_wipe(data.data(), size_t(data.size()));
    data.clear();
}

} // namespace hvelv::crypto
