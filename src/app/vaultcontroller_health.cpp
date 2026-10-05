// VaultController: passordhelse, lekkasjesjekk (HIBP) og TLS-sjekk.

#include "vaultcontroller.h"

#include "health.h"
#include "netchecks.h"

#include <QSet>
#include <QUrl>

using namespace hvelv;

NetworkChecks *VaultController::net()
{
    if (!m_net) {
        m_net = new NetworkChecks(this);
        connect(m_net, &NetworkChecks::busyChanged, this, &VaultController::networkBusyChanged);
        connect(m_net, &NetworkChecks::breachesChecked, this, [this](const QHash<QString, int> &counts, const QString &error) {
            if (!isUnlocked())
                return; // låst mens vi ventet
            if (!error.isEmpty()) {
                emit breachCheckFinished(false, error);
                return;
            }
            m_breachCounts = counts;
            m_breachesChecked = true;
            emit healthChanged();
            const int n = health::analyze(m_data, QDate::currentDate(), 365, m_breachCounts).breached;
            emit breachCheckFinished(true, n == 0 ? QStringLiteral("Ingen av passordene er funnet i kjente datalekkasjer.")
                                                  : QStringLiteral("%1 oppføring(er) har passord som finnes i kjente datalekkasjer.").arg(n));
        });
        connect(m_net, &NetworkChecks::tlsChecked, this, &VaultController::tlsChecked);
    }
    return m_net;
}

bool VaultController::isNetworkBusy() const
{
    return m_net && m_net->isBusy();
}

QVariantMap VaultController::healthReport() const
{
    const auto r = health::analyze(m_data, QDate::currentDate(), 365, m_breachCounts);
    QVariantList items;
    for (const auto &i : r.issues) {
        const Entry *e = m_data.entry(i.entryId);
        if (!e)
            continue;
        items << QVariantMap{
            {QStringLiteral("id"), e->id},
            {QStringLiteral("title"), e->title},
            {QStringLiteral("customer"), e->customer},
            {QStringLiteral("kinds"), i.kinds},
            {QStringLiteral("bits"), i.bits},
            {QStringLiteral("reuseCount"), i.reuseCount},
            {QStringLiteral("breachCount"), i.breachCount},
        };
    }
    return {
        {QStringLiteral("checked"), r.checked},
        {QStringLiteral("weak"), r.weak},
        {QStringLiteral("reused"), r.reused},
        {QStringLiteral("old"), r.old},
        {QStringLiteral("breached"), r.breached},
        {QStringLiteral("expired"), r.expired},
        {QStringLiteral("breachesChecked"), m_breachesChecked},
        {QStringLiteral("items"), items},
    };
}

int VaultController::passwordBits(const QString &password) const
{
    return health::estimateBits(password);
}

void VaultController::checkBreaches()
{
    if (!isUnlocked())
        return;
    QSet<QString> hashes;
    for (const auto &e : std::as_const(m_data.entries))
        if (!e.password.isEmpty())
            hashes.insert(health::sha1Hex(e.password));
    net()->checkBreaches(hashes.values());
}

QStringList VaultController::tlsTargets() const
{
    QSet<QString> seen;
    QStringList out;
    for (const auto &e : m_data.entries) {
        QString target = e.url.trimmed();
        if (target.isEmpty() && e.type == QLatin1String("certificate"))
            target = e.fields.value(QStringLiteral("host")).toString();
        if (target.isEmpty())
            continue;
        if (!target.contains(QLatin1String("://")))
            target.prepend(QStringLiteral("https://")); // bare vertsnavn: anta https
        const QUrl u(target);
        if (u.scheme() != QLatin1String("https") || u.host().isEmpty())
            continue; // http, sftp osv. har ikke TLS-sertifikat å sjekke her
        const QString key = u.host().toLower() + (u.port() > 0 && u.port() != 443 ? QStringLiteral(":%1").arg(u.port()) : QString());
        if (!seen.contains(key)) {
            seen.insert(key);
            out << key;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void VaultController::checkTls(const QString &target)
{
    QString t = target.trimmed();
    if (t.isEmpty())
        return;
    if (!t.contains(QLatin1String("://")))
        t.prepend(QStringLiteral("https://"));
    const QUrl u(t);
    if (u.host().isEmpty()) {
        emit tlsChecked(target, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Ugyldig adresse.")}});
        return;
    }
    net()->checkTls(target, u.host(), quint16(u.port(443)));
}
