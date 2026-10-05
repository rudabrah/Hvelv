#pragma once

#include "vaultdata.h"

#include <QByteArray>
#include <QDate>
#include <QStringList>

namespace hvelv::calendar {

// iCalendar (RFC 5545) med én heldagsavtale per utløpsdato og påminnelse
// «reminderDays» dager før. UID er fast per oppføring, så en ny import
// oppdaterer avtalen i stedet for å lage duplikat. Inneholder aldri hemmeligheter.
//
// entryIds tom = alle oppføringer med utløpsdato. Utløpte tas med bare hvis includeExpired.
QByteArray buildIcs(const VaultData &data, const QStringList &entryIds = {}, bool includeExpired = false,
                    const QDate &today = QDate::currentDate());

} // namespace hvelv::calendar
