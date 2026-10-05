#pragma once

#include <QString>

namespace hvelv {

struct PasswordOptions
{
    int length = 20;
    bool lower = true;
    bool upper = true;
    bool digits = true;
    bool symbols = true;
    bool avoidAmbiguous = true; // utelat l, I, 1, O, 0 osv.
};

// Minst ett tegn fra hver valgte klasse. Bruker OS-ets CSPRNG.
QString generatePassword(const PasswordOptions &options);

} // namespace hvelv
