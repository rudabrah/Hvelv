#pragma once

#include "vaultdata.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace hvelv::health {

// Grovt anslag på entropi i bit (tegnsett × lengde, med straff for vanlige mønstre).
int estimateBits(const QString &password);
bool isWeak(const QString &password); // < 50 bit eller på listen over vanlige passord

struct Issue
{
    QString entryId;
    QStringList kinds; // "weak", "reused", "old", "breached", "expired"
    int bits = 0;
    int reuseCount = 0;   // antall oppføringer som deler passordet
    int breachCount = 0;  // ganger passordet er sett i lekkasjer (HIBP)
};

struct Report
{
    int checked = 0; // oppføringer med passord
    int weak = 0;
    int reused = 0;
    int old = 0;
    int breached = 0;
    int expired = 0;
    QList<Issue> issues;
};

// breachCounts: SHA-1 (store bokstaver, hex) -> antall treff fra HIBP. Tom = ikke sjekket.
Report analyze(const VaultData &data, const QDate &today = QDate::currentDate(), int oldAfterDays = 365,
               const QHash<QString, int> &breachCounts = {});

// Have I Been Pwned «range»-API med k-anonymitet: bare de 5 første tegnene sendes.
QString sha1Hex(const QString &password);
int hibpCountInResponse(const QByteArray &response, const QString &sha1);

} // namespace hvelv::health
