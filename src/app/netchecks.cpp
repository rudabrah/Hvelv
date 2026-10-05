#include "netchecks.h"

#include "health.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslSocket>
#include <QTimer>

NetworkChecks::NetworkChecks(QObject *parent)
    : QObject(parent)
{
    m_nam.setTransferTimeout(15000);
}

void NetworkChecks::checkBreaches(const QStringList &sha1s)
{
    if (m_pendingRanges > 0)
        return;
    m_counts.clear();
    m_breachError.clear();

    QHash<QString, QStringList> byPrefix;
    for (const auto &sha : sha1s)
        byPrefix[sha.left(5)] << sha;
    if (byPrefix.isEmpty()) {
        emit breachesChecked({}, {});
        return;
    }
    m_pendingRanges = int(byPrefix.size());
    emit busyChanged();

    for (auto it = byPrefix.cbegin(); it != byPrefix.cend(); ++it) {
        QNetworkRequest req(QUrl(QStringLiteral("https://api.pwnedpasswords.com/range/") + it.key()));
        req.setRawHeader("Add-Padding", "true"); // skjuler hvor mange treff prefikset har
        req.setRawHeader("User-Agent", "Hvelv-passordhelse");
        QNetworkReply *reply = m_nam.get(req);
        const QStringList hashes = it.value();
        connect(reply, &QNetworkReply::finished, this, [this, reply, hashes] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                m_breachError = QStringLiteral("Kunne ikke nå Have I Been Pwned: %1").arg(reply->errorString());
            } else {
                const QByteArray body = reply->readAll();
                for (const auto &sha : hashes) {
                    const int n = hvelv::health::hibpCountInResponse(body, sha);
                    if (n > 0)
                        m_counts.insert(sha, n);
                }
            }
            if (--m_pendingRanges == 0) {
                emit breachesChecked(m_counts, m_breachError);
                emit busyChanged();
            }
        });
    }
}

void NetworkChecks::checkTls(const QString &key, const QString &host, quint16 port)
{
    if (m_tlsPending.contains(key))
        return;
    m_tlsPending.insert(key);
    emit busyChanged();

    auto *socket = new QSslSocket(this);
    socket->setPeerVerifyName(host);
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);

    auto finish = [this, key, socket](const QVariantMap &result) {
        if (!m_tlsPending.remove(key))
            return; // allerede rapportert
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
        emit tlsChecked(key, result);
        emit busyChanged();
    };

    // Vi vil lese sertifikatet også når det er utløpt eller selvsignert.
    connect(socket, &QSslSocket::sslErrors, socket, [socket](const QList<QSslError> &) {
        socket->ignoreSslErrors();
    });
    connect(socket, &QSslSocket::encrypted, this, [socket, host, port, finish] {
        const QSslCertificate cert = socket->peerCertificate();
        const QDateTime expiry = cert.expiryDate();
        QString serial = QString::fromLatin1(cert.serialNumber()).remove(QLatin1Char(':')).toUpper();
        finish({
            {QStringLiteral("ok"), !cert.isNull()},
            {QStringLiteral("host"), host},
            {QStringLiteral("port"), port},
            {QStringLiteral("subject"), cert.subjectDisplayName()},
            {QStringLiteral("issuer"), cert.issuerDisplayName()},
            {QStringLiteral("thumbprint"), QString::fromLatin1(cert.digest(QCryptographicHash::Sha1).toHex().toUpper())},
            {QStringLiteral("serialNumber"), serial},
            {QStringLiteral("validFrom"), cert.effectiveDate().toLocalTime().date().toString(Qt::ISODate)},
            {QStringLiteral("expiresAt"), expiry.toLocalTime().date().toString(Qt::ISODate)},
            {QStringLiteral("daysLeft"), int(QDate::currentDate().daysTo(expiry.toLocalTime().date()))},
            {QStringLiteral("selfSigned"), cert.isSelfSigned()},
        });
    });
    connect(socket, &QAbstractSocket::errorOccurred, this, [socket, host, finish](QAbstractSocket::SocketError) {
        finish({{QStringLiteral("ok"), false}, {QStringLiteral("host"), host},
                {QStringLiteral("error"), socket->errorString()}});
    });
    connect(timer, &QTimer::timeout, this, [host, finish] {
        finish({{QStringLiteral("ok"), false}, {QStringLiteral("host"), host},
                {QStringLiteral("error"), QStringLiteral("Tidsavbrudd")}});
    });
    timer->start(10000);
    socket->connectToHostEncrypted(host, port);
}
