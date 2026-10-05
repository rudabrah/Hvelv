#include "health.h"

#include <QCryptographicHash>
#include <QRegularExpression>

#include <cmath>

namespace hvelv::health {

namespace {

// Et lite utvalg av de mest brukte passordene (inkl. norske varianter).
const QSet<QString> &commonPasswords()
{
    static const QSet<QString> list = {
        "123456", "123456789", "12345678", "password", "passord", "qwerty", "qwerty123", "111111",
        "123123", "abc123", "1234567890", "password1", "passord1", "iloveyou", "admin", "admin123",
        "welcome", "welcome1", "velkommen", "velkommen1", "sommer2024", "sommer2025", "sommer2026",
        "vinter2025", "vinter2026", "host2025", "høst2025", "var2026", "vår2026", "letmein", "monkey",
        "dragon", "football", "fotball", "norge", "norge123", "hemmelig", "hemmelig1", "changeme",
        "p@ssw0rd", "p@ssword", "passw0rd", "secret", "test", "test123", "summer2025", "winter2025",
        "Password1!", "Passord1!", "Sommer2025!", "Velkommen1!", "Qwerty123!", "Welcome1!",
    };
    return list;
}

bool isSequence(const QString &s)
{
    if (s.size() < 4)
        return false;
    int up = 0, down = 0, same = 0;
    for (int i = 1; i < s.size(); ++i) {
        const int d = s[i].unicode() - s[i - 1].unicode();
        up += d == 1;
        down += d == -1;
        same += d == 0;
    }
    const int n = int(s.size()) - 1;
    return up == n || down == n || same == n;
}

} // namespace

int estimateBits(const QString &password)
{
    if (password.isEmpty())
        return 0;
    if (commonPasswords().contains(password) || commonPasswords().contains(password.toLower()))
        return 0;

    bool lower = false, upper = false, digit = false, symbol = false, other = false;
    for (QChar c : password) {
        if (c.unicode() < 128) {
            if (c.isLower()) lower = true;
            else if (c.isUpper()) upper = true;
            else if (c.isDigit()) digit = true;
            else symbol = true;
        } else {
            other = true; // æ, ø, å og andre
        }
    }
    int pool = 0;
    if (lower) pool += 26;
    if (upper) pool += 26;
    if (digit) pool += 10;
    if (symbol) pool += 33;
    if (other) pool += 50;

    double bits = password.size() * std::log2(double(qMax(pool, 2)));
    if (isSequence(password))
        bits /= 4;
    // Vanlig mønster: ord + årstall/siffer + ett tegn, f.eks. «Sommer2026!»
    static const QRegularExpression wordYear(QStringLiteral("^[A-Za-zÆØÅæøå]{3,12}(19|20)\\d{2}[!.?#]?$"));
    if (wordYear.match(password).hasMatch())
        bits = qMin(bits, 30.0);
    return int(bits);
}

bool isWeak(const QString &password)
{
    return estimateBits(password) < 50;
}

QString sha1Hex(const QString &password)
{
    return QString::fromLatin1(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha1).toHex().toUpper());
}

int hibpCountInResponse(const QByteArray &response, const QString &sha1)
{
    const QByteArray suffix = sha1.mid(5).toUpper().toLatin1();
    for (const QByteArray &rawLine : response.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        const int colon = line.indexOf(':');
        if (colon == 35 && line.left(colon).toUpper() == suffix)
            return line.mid(colon + 1).toInt(); // Add-Padding gir linjer med 0, som da blir 0
    }
    return 0;
}

Report analyze(const VaultData &data, const QDate &today, int oldAfterDays, const QHash<QString, int> &breachCounts)
{
    Report r;
    QHash<QString, int> usage;
    for (const auto &e : data.entries)
        if (!e.password.isEmpty())
            ++usage[e.password];

    for (const auto &e : data.entries) {
        Issue issue;
        issue.entryId = e.id;
        if (!e.password.isEmpty()) {
            ++r.checked;
            issue.bits = estimateBits(e.password);
            if (issue.bits < 50) {
                issue.kinds << QStringLiteral("weak");
                ++r.weak;
            }
            issue.reuseCount = usage.value(e.password);
            if (issue.reuseCount > 1) {
                issue.kinds << QStringLiteral("reused");
                ++r.reused;
            }
            const QDate changed = e.passwordChangedAt.isValid() ? e.passwordChangedAt.date()
                                                                : e.createdAt.date();
            if (changed.isValid() && changed.daysTo(today) > oldAfterDays) {
                issue.kinds << QStringLiteral("old");
                ++r.old;
            }
            issue.breachCount = breachCounts.value(sha1Hex(e.password));
            if (issue.breachCount > 0) {
                issue.kinds << QStringLiteral("breached");
                ++r.breached;
            }
        }
        if (e.expiryStatus(today) == ExpiryStatus::Expired) {
            issue.kinds << QStringLiteral("expired");
            ++r.expired;
        }
        if (!issue.kinds.isEmpty())
            r.issues << issue;
    }
    return r;
}

} // namespace hvelv::health
