#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

// Nettverkssjekker som brukeren selv starter:
//  - Have I Been Pwned (k-anonymitet: bare 5 første tegn av SHA-1 sendes, med padding)
//  - TLS: hent sertifikatet til en server og les utløpsdato m.m.
class NetworkChecks : public QObject
{
    Q_OBJECT

public:
    explicit NetworkChecks(QObject *parent = nullptr);

    bool isBusy() const { return m_pendingRanges > 0 || !m_tlsPending.isEmpty(); }

    // sha1s: store heksadesimale SHA-1. Resultat: sha1 -> antall treff (bare treff > 0).
    void checkBreaches(const QStringList &sha1s);
    void checkTls(const QString &key, const QString &host, quint16 port);

signals:
    void breachesChecked(const QHash<QString, int> &counts, const QString &error);
    void tlsChecked(const QString &key, const QVariantMap &result);
    void busyChanged();

private:
    QNetworkAccessManager m_nam;
    int m_pendingRanges = 0;
    QHash<QString, int> m_counts;
    QString m_breachError;
    QSet<QString> m_tlsPending;
};
