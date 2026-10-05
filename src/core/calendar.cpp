#include "calendar.h"

#include "entrytypes.h"

#include <QDateTime>

namespace hvelv::calendar {

namespace {

QString escapeText(QString v)
{
    v.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    v.replace(QLatin1String(";"), QLatin1String("\\;"));
    v.replace(QLatin1String(","), QLatin1String("\\,"));
    v.replace(QLatin1String("\r\n"), QLatin1String("\\n"));
    v.replace(QLatin1String("\n"), QLatin1String("\\n"));
    return v;
}

// Linjer over 75 oktetter brytes med CRLF + mellomrom (uten å dele UTF-8-tegn).
QByteArray fold(const QString &line)
{
    const QByteArray utf8 = line.toUtf8();
    QByteArray out;
    int count = 0;
    for (int i = 0; i < utf8.size(); ++i) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        const bool continuation = (c & 0xC0) == 0x80;
        if (count >= 74 && !continuation) {
            out += "\r\n ";
            count = 1;
        }
        out += char(c);
        ++count;
    }
    return out + "\r\n";
}

} // namespace

QByteArray buildIcs(const VaultData &data, const QStringList &entryIds, bool includeExpired, const QDate &today)
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    QByteArray out;
    auto line = [&](const QString &l) { out += fold(l); };

    line(QStringLiteral("BEGIN:VCALENDAR"));
    line(QStringLiteral("VERSION:2.0"));
    line(QStringLiteral("PRODID:-//Hvelv//Utløpsdatoer//NO"));
    line(QStringLiteral("CALSCALE:GREGORIAN"));
    line(QStringLiteral("METHOD:PUBLISH"));
    line(QStringLiteral("X-WR-CALNAME:Hvelv – utløp"));

    for (const auto &e : data.entries) {
        if (!e.expiresAt.isValid())
            continue;
        if (!entryIds.isEmpty() && !entryIds.contains(e.id))
            continue;
        if (!includeExpired && e.expiresAt < today)
            continue;

        const QString typeLabel = entrytypes::type(e.type)[QStringLiteral("label")].toString();
        const QString expiryLabel = entrytypes::type(e.type)[QStringLiteral("expiryLabel")].toString();
        QStringList desc;
        desc << QStringLiteral("%1: %2").arg(expiryLabel, e.expiresAt.toString(QStringLiteral("dd.MM.yyyy")));
        desc << QStringLiteral("Type: %1").arg(typeLabel);
        if (!e.customer.isEmpty())
            desc << QStringLiteral("Kunde: %1").arg(e.customer);
        if (!e.system.isEmpty())
            desc << QStringLiteral("System: %1%2").arg(e.system, e.environment.isEmpty() ? QString()
                                                                    : QStringLiteral(" (%1)").arg(e.environment));
        if (!e.username.isEmpty())
            desc << QStringLiteral("Bruker: %1").arg(e.username);
        if (!e.owner.isEmpty())
            desc << QStringLiteral("Ansvarlig: %1").arg(e.owner);
        for (const auto &cid : e.contactIds)
            if (const Contact *c = data.contact(cid))
                desc << QStringLiteral("Kontakt: %1%2").arg(c->name, c->email.isEmpty() ? QString()
                                                                    : QStringLiteral(" <%1>").arg(c->email));
        if (!e.renewalProcedure.isEmpty())
            desc << QString() << QStringLiteral("Slik fornyer du:") << e.renewalProcedure;
        desc << QString() << QStringLiteral("Detaljer og pålogging ligger i Hvelv.");

        const QString title = e.customer.isEmpty() ? e.title : QStringLiteral("%1 (%2)").arg(e.title, e.customer);
        const qint64 sequence = e.updatedAt.isValid() ? e.updatedAt.toSecsSinceEpoch() / 60 : 0;

        line(QStringLiteral("BEGIN:VEVENT"));
        line(QStringLiteral("UID:hvelv-%1@hvelv").arg(e.id));
        line(QStringLiteral("SEQUENCE:%1").arg(sequence));
        line(QStringLiteral("DTSTAMP:%1").arg(stamp));
        line(QStringLiteral("DTSTART;VALUE=DATE:%1").arg(e.expiresAt.toString(QStringLiteral("yyyyMMdd"))));
        line(QStringLiteral("DTEND;VALUE=DATE:%1").arg(e.expiresAt.addDays(1).toString(QStringLiteral("yyyyMMdd"))));
        line(QStringLiteral("SUMMARY:%1").arg(escapeText(QStringLiteral("Utløper: ") + title)));
        line(QStringLiteral("DESCRIPTION:%1").arg(escapeText(desc.join(QLatin1Char('\n')))));
        if (!e.url.isEmpty())
            line(QStringLiteral("URL:%1").arg(e.url));
        line(QStringLiteral("CATEGORIES:Hvelv"));
        line(QStringLiteral("TRANSP:TRANSPARENT"));
        line(QStringLiteral("BEGIN:VALARM"));
        line(QStringLiteral("ACTION:DISPLAY"));
        line(QStringLiteral("DESCRIPTION:%1").arg(escapeText(QStringLiteral("Utløper: ") + title)));
        line(QStringLiteral("TRIGGER:-P%1D").arg(qMax(0, e.reminderDays)));
        line(QStringLiteral("END:VALARM"));
        line(QStringLiteral("END:VEVENT"));
    }
    line(QStringLiteral("END:VCALENDAR"));
    return out;
}

} // namespace hvelv::calendar
