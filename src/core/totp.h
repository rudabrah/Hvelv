#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <optional>

namespace hvelv::totp {

struct Config
{
    QByteArray secret;
    int digits = 6;
    int period = 30;
    QCryptographicHash::Algorithm algorithm = QCryptographicHash::Sha1;
    QString issuer;
    QString account;
};

// Godtar ren base32 (mellomrom og bindestreker ignoreres) eller en otpauth://totp/-URI.
std::optional<Config> parse(const QString &input);

std::optional<QByteArray> base32Decode(const QString &input);

QString code(const Config &config, qint64 unixTime);
int secondsRemaining(const Config &config, qint64 unixTime);

} // namespace hvelv::totp
