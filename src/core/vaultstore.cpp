#include "vaultstore.h"

#include <algorithm>

namespace hvelv {

void ChangeSet::applyTo(VaultData &data) const
{
    for (const auto &c : upsertContacts) {
        auto it = std::find_if(data.contacts.begin(), data.contacts.end(), [&](const Contact &x) { return x.id == c.id; });
        if (it != data.contacts.end())
            *it = c;
        else
            data.contacts << c;
    }
    for (const auto &e : upsertEntries) {
        auto it = std::find_if(data.entries.begin(), data.entries.end(), [&](const Entry &x) { return x.id == e.id; });
        if (it != data.entries.end())
            *it = e;
        else
            data.entries << e;
    }
    if (!deleteEntryIds.isEmpty())
        data.entries.removeIf([&](const Entry &e) { return deleteEntryIds.contains(e.id); });
    if (!deleteContactIds.isEmpty()) {
        data.contacts.removeIf([&](const Contact &c) { return deleteContactIds.contains(c.id); });
        for (auto &e : data.entries)
            e.contactIds.removeIf([&](const QString &id) { return deleteContactIds.contains(id); });
    }
    if (settings)
        data.settings = *settings;
}

} // namespace hvelv
