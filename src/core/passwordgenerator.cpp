#include "passwordgenerator.h"

#include <QRandomGenerator>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace hvelv {

namespace {

QString strip(QString set, bool avoidAmbiguous)
{
    if (avoidAmbiguous) {
        for (QChar c : QStringLiteral("lI1O0o|`'\""))
            set.remove(c);
    }
    return set;
}

QChar pick(const QString &set)
{
    return set.at(int(QRandomGenerator::system()->bounded(quint32(set.size()))));
}

} // namespace

QString generatePassword(const PasswordOptions &options)
{
    QStringList classes;
    if (options.lower)
        classes << strip(QStringLiteral("abcdefghijklmnopqrstuvwxyz"), options.avoidAmbiguous);
    if (options.upper)
        classes << strip(QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), options.avoidAmbiguous);
    if (options.digits)
        classes << strip(QStringLiteral("0123456789"), options.avoidAmbiguous);
    if (options.symbols)
        classes << strip(QStringLiteral("!#$%&()*+,-./:;<=>?@[]^_{}~"), options.avoidAmbiguous);
    if (classes.isEmpty())
        classes << strip(QStringLiteral("abcdefghijklmnopqrstuvwxyz"), options.avoidAmbiguous);

    const int length = std::max<int>(options.length, int(classes.size()));
    const QString all = classes.join(QString());

    QString out;
    out.reserve(length);
    for (const auto &set : std::as_const(classes))
        out.append(pick(set));
    while (out.size() < length)
        out.append(pick(all));

    // Fisher–Yates, så de påtvungne tegnene ikke alltid står først.
    for (int i = int(out.size()) - 1; i > 0; --i) {
        const int j = int(QRandomGenerator::system()->bounded(quint32(i + 1)));
        std::swap(out[i], out[j]);
    }
    return out;
}

} // namespace hvelv
