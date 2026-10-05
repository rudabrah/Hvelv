#include "autotype.h"

#include <QHash>
#include <QRegularExpression>
#include <QUrl>

namespace hvelv::autotype {

namespace {

QString customField(const Entry &e, const char *name)
{
    for (const auto &f : e.customFields)
        if (f.name.compare(QLatin1String(name), Qt::CaseInsensitive) == 0)
            return f.value;
    return {};
}

} // namespace

ParseResult parseSequence(const QString &sequence, const Entry &entry, const QString &totpCode)
{
    static const QHash<QString, Action::SpecialKey> keys = {
        {"TAB", Action::Tab}, {"ENTER", Action::Enter}, {"SPACE", Action::Space},
        {"BACKSPACE", Action::Backspace}, {"BS", Action::Backspace}, {"ESC", Action::Escape},
        {"UP", Action::Up}, {"DOWN", Action::Down}, {"LEFT", Action::Left}, {"RIGHT", Action::Right},
        {"HOME", Action::Home}, {"END", Action::End}, {"DELETE", Action::Delete}, {"DEL", Action::Delete},
    };

    ParseResult r;
    QString pending;
    auto flush = [&] {
        if (!pending.isEmpty()) {
            r.actions << Action{Action::Text, pending};
            pending.clear();
        }
    };
    auto text = [&](const QString &t) { pending += t; };

    for (int i = 0; i < sequence.size(); ++i) {
        const QChar c = sequence[i];
        if (c != QLatin1Char('{')) {
            pending += c;
            continue;
        }
        // {{} og {}} er bokstavelige klammer
        if (sequence.mid(i, 3) == QLatin1String("{{}") || sequence.mid(i, 3) == QLatin1String("{}}")) {
            pending += sequence[i + 1];
            i += 2;
            continue;
        }
        const int end = sequence.indexOf(QLatin1Char('}'), i);
        if (end < 0) {
            r.error = QStringLiteral("Mangler } i sekvensen.");
            return r;
        }
        const QString token = sequence.mid(i + 1, end - i - 1).trimmed();
        const QString upper = token.toUpper();
        i = end;

        if (upper == QLatin1String("USERNAME"))
            text(entry.username);
        else if (upper == QLatin1String("PASSWORD"))
            text(entry.password);
        else if (upper == QLatin1String("TOTP"))
            text(totpCode);
        else if (upper == QLatin1String("URL"))
            text(entry.url);
        else if (upper == QLatin1String("TITLE"))
            text(entry.title);
        else if (keys.contains(upper)) {
            flush();
            r.actions << Action{Action::Key, {}, keys.value(upper)};
        } else if (upper.startsWith(QLatin1String("DELAY "))) {
            bool ok = false;
            const int ms = upper.mid(6).trimmed().toInt(&ok);
            if (!ok || ms < 0 || ms > 10000) {
                r.error = QStringLiteral("Ugyldig {DELAY}: %1").arg(token);
                return r;
            }
            flush();
            r.actions << Action{Action::Delay, {}, Action::Tab, ms};
        } else {
            r.error = QStringLiteral("Ukjent felt i sekvensen: {%1}").arg(token);
            return r;
        }
    }
    flush();
    return r;
}

QString sequenceFor(const Entry &entry, const QString &defaultSequence)
{
    const QString custom = customField(entry, SequenceFieldName).trimmed();
    return custom.isEmpty() ? defaultSequence : custom;
}

int matchScore(const Entry &entry, const QString &windowTitle)
{
    const QString title = windowTitle.trimmed();
    if (title.isEmpty())
        return 0;

    // 1) Eksplisitt mønster, f.eks. «*Visma*» eller «Logg inn - Altinn*»
    const QString pattern = customField(entry, WindowFieldName).trimmed();
    if (!pattern.isEmpty()) {
        const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(
                                        pattern, QRegularExpression::UnanchoredWildcardConversion),
                                    QRegularExpression::CaseInsensitiveOption);
        if (re.match(title).hasMatch())
            return 100;
    }

    int score = 0;
    // 2) Vertsnavnet fra URL-en (uten www.) står i tittelen, typisk i nettlesere
    QString host = QUrl::fromUserInput(entry.url).host().toLower();
    if (host.startsWith(QLatin1String("www.")))
        host = host.mid(4);
    if (host.size() >= 4 && title.contains(host, Qt::CaseInsensitive))
        score = qMax(score, 60);
    // Hoveddelen av domenet (f.eks. «visma» fra login.visma.net)
    const QStringList parts = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        const QString main = parts.at(parts.size() - 2);
        if (main.size() >= 4 && title.contains(main, Qt::CaseInsensitive))
            score = qMax(score, 30);
    }
    // 3) Oppføringens navn eller system står i tittelen
    if (entry.title.size() >= 3 && title.contains(entry.title, Qt::CaseInsensitive))
        score = qMax(score, 50);
    if (entry.system.size() >= 3 && title.contains(entry.system, Qt::CaseInsensitive))
        score = qMax(score, 25);
    return score;
}

} // namespace hvelv::autotype
