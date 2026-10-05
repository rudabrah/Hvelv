#pragma once

#include <QByteArray>
#include <cstdint>
#include <optional>

namespace hvelv::crypto {

constexpr int KeySize = 32;
constexpr int SaltSize = 16;
constexpr int NonceSize = 24; // XChaCha20
constexpr int MacSize = 16;   // Poly1305

struct KdfParams
{
    uint32_t memoryKiB = 64 * 1024; // 64 MiB
    uint32_t passes = 3;
    uint32_t lanes = 1;
};

// Kryptografisk sikre tilfeldige bytes fra operativsystemet.
QByteArray randomBytes(int size);

// Argon2id: hovedpassord + salt -> 32-byte nøkkel.
QByteArray deriveKey(const QByteArray &password, const QByteArray &salt, const KdfParams &params);

struct Sealed
{
    QByteArray nonce;
    QByteArray mac;
    QByteArray cipherText;
};

// XChaCha20-Poly1305. Tilfeldig nonce for hvert kall.
Sealed seal(const QByteArray &key, const QByteArray &associatedData, const QByteArray &plainText);

// Returnerer nullopt hvis nøkkel er feil eller data/ad er endret.
std::optional<QByteArray> open(const QByteArray &key, const QByteArray &associatedData,
                               const QByteArray &nonce, const QByteArray &mac,
                               const QByteArray &cipherText);

// Overskriver innholdet før bufferen frigjøres.
void wipe(QByteArray &data);

} // namespace hvelv::crypto
