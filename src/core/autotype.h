#pragma once

#include "entry.h"

#include <QList>
#include <QString>

namespace hvelv::autotype {

inline constexpr char DefaultSequence[] = "{USERNAME}{TAB}{PASSWORD}{ENTER}";
inline constexpr char SequenceFieldName[] = "Auto-Type";        // egendefinert felt som overstyrer
inline constexpr char WindowFieldName[] = "Auto-Type-vindu";    // vindustittel-mønster med *

struct Action
{
    enum Kind { Text, Key, Delay };
    enum SpecialKey { Tab, Enter, Space, Backspace, Escape, Up, Down, Left, Right, Home, End, Delete };

    Kind kind = Text;
    QString text;          // Kind::Text
    SpecialKey key = Tab;  // Kind::Key
    int ms = 0;            // Kind::Delay
};

// Tokens: {USERNAME} {PASSWORD} {TOTP} {URL} {TITLE} {TAB} {ENTER} {SPACE} {BACKSPACE} {ESC}
//         {UP} {DOWN} {LEFT} {RIGHT} {HOME} {END} {DELETE} {DELAY n} og {{} / {}} for klammer.
// Annen tekst skrives som den er. Ukjente tokens gir feilmelding.
struct ParseResult
{
    QList<Action> actions;
    QString error;
};
ParseResult parseSequence(const QString &sequence, const Entry &entry, const QString &totpCode = {});

// Sekvensen som gjelder for oppføringen (egendefinert felt «Auto-Type» eller standard).
QString sequenceFor(const Entry &entry, const QString &defaultSequence = QString::fromLatin1(DefaultSequence));

// Hvor godt oppføringen passer til et vindu (0 = ikke). Høyest vinner.
int matchScore(const Entry &entry, const QString &windowTitle);

} // namespace hvelv::autotype
