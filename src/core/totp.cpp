#include "totp.h"

#include <QMessageAuthenticationCode>
#include <QUrl>
#include <QUrlQuery>
#include <QtEndian>

namespace hvelv::totp {

std::optional<QByteArray> base32Decode(const QString &input)
{
    static const QByteArray alphabet("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
    QByteArray out;
    quint32 buffer = 0;
    int bits = 0;
    for (QChar ch : input) {
        if (ch.isSpace() || ch == QLatin1Char('-') || ch == QLatin1Char('='))
            continue;
        const int v = alphabet.indexOf(char(ch.toUpper().toLatin1()));
        if (v < 0)
            return std::nullopt;
        buffer = (buffer << 5) | quint32(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(char((buffer >> bits) & 0xff));
        }
    }
    if (out.isEmpty())
        return std::nullopt;
    return out;
}

std::optional<Config> parse(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;

    Config c;
    if (trimmed.startsWith(QLatin1String("otpauth://"), Qt::CaseInsensitive)) {
        const QUrl url(trimmed);
        if (url.host().compare(QLatin1String("totp"), Qt::CaseInsensitive) != 0)
            return std::nullopt;
        const QUrlQuery q(url);
        auto secret = base32Decode(q.queryItemValue(QStringLiteral("secret")));
        if (!secret)
            return std::nullopt;
        c.secret = *secret;
        if (q.hasQueryItem(QStringLiteral("digits")))
            c.digits = q.queryItemValue(QStringLiteral("digits")).toInt();
        if (q.hasQueryItem(QStringLiteral("period")))
            c.period = q.queryItemValue(QStringLiteral("period")).toInt();
        const QString algo = q.queryItemValue(QStringLiteral("algorithm")).toUpper();
        if (algo == QLatin1String("SHA256"))
            c.algorithm = QCryptographicHash::Sha256;
        else if (algo == QLatin1String("SHA512"))
            c.algorithm = QCryptographicHash::Sha512;
        c.issuer = q.queryItemValue(QStringLiteral("issuer"), QUrl::FullyDecoded);
        QString label = url.path(QUrl::FullyDecoded);
        if (label.startsWith(QLatin1Char('/')))
            label.remove(0, 1);
        const int colon = label.indexOf(QLatin1Char(':'));
        c.account = colon >= 0 ? label.mid(colon + 1).trimmed() : label;
        if (c.issuer.isEmpty() && colon >= 0)
            c.issuer = label.left(colon);
    } else {
        auto secret = base32Decode(trimmed);
        if (!secret)
            return std::nullopt;
        c.secret = *secret;
    }
    if (c.digits < 6 || c.digits > 10 || c.period < 1)
        return std::nullopt;
    return c;
}

QString code(const Config &config, qint64 unixTime)
{
    const quint64 counter = quint64(unixTime / config.period);
    QByteArray msg(8, Qt::Uninitialized);
    qToBigEndian(counter, msg.data());

    const QByteArray hmac = QMessageAuthenticationCode::hash(msg, config.secret, config.algorithm);
    const int offset = hmac.at(hmac.size() - 1) & 0x0f;
    const quint32 binary = (quint32(quint8(hmac[offset]) & 0x7f) << 24)
                           | (quint32(quint8(hmac[offset + 1])) << 16)
                           | (quint32(quint8(hmac[offset + 2])) << 8)
                           | quint32(quint8(hmac[offset + 3]));
    quint64 mod = 1;
    for (int i = 0; i < config.digits; ++i)
        mod *= 10;
    return QStringLiteral("%1").arg(quint64(binary) % mod, config.digits, 10, QLatin1Char('0'));
}

int secondsRemaining(const Config &config, qint64 unixTime)
{
    return config.period - int(unixTime % config.period);
}

} // namespace hvelv::totp
