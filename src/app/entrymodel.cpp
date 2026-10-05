#include "entrymodel.h"
#include "entrytypes.h"

#include <climits>

using hvelv::Entry;
using hvelv::ExpiryStatus;

QString expiryStatusString(ExpiryStatus s)
{
    switch (s) {
    case ExpiryStatus::Ok: return QStringLiteral("ok");
    case ExpiryStatus::ExpiringSoon: return QStringLiteral("soon");
    case ExpiryStatus::Expired: return QStringLiteral("expired");
    case ExpiryStatus::None: break;
    }
    return QStringLiteral("none");
}

int EntryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant EntryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return e.title;
    case IdRole: return e.id;
    case TypeRole: return e.type;
    case SystemRole: return e.system;
    case EnvironmentRole: return e.environment;
    case UrlRole: return e.url;
    case CustomerRole: return e.customer;
    case UsernameRole: return e.username;
    case TagsRole: return e.tags;
    case OwnerRole: return e.owner;
    case TwoFactorPlatformRole: return e.twoFactorPlatform;
    case HasTotpRole: return !e.totpSecret.trimmed().isEmpty();
    case ExpiresAtRole: return e.expiresAt.isValid() ? e.expiresAt.toString(Qt::ISODate) : QString();
    case DaysLeftRole: {
        const int d = e.daysUntilExpiry();
        return d == INT_MIN ? QVariant() : QVariant(d);
    }
    case ExpiryStatusRole: return expiryStatusString(e.expiryStatus());
    case ReminderChannelRole: return hvelv::reminderChannelToString(e.reminderChannel);
    case SearchTextRole: {
        QStringList parts{e.title, e.url, e.customer, e.system, e.environment, e.username,
                          e.owner, e.twoFactorPlatform, e.tags.join(' '), e.notes, e.renewalProcedure};
        for (const auto &key : hvelv::entrytypes::searchableFieldKeys(e.type))
            parts << e.fields.value(key).toString();
        for (const auto &f : e.customFields) {
            parts << f.name;
            if (!f.secret)
                parts << f.value;
        }
        for (const auto &cid : e.contactIds)
            parts << m_contactSearch.value(cid);
        return parts.join('\n');
    }
    }
    return {};
}

QHash<int, QByteArray> EntryModel::roleNames() const
{
    return {
        {IdRole, "entryId"},
        {TitleRole, "title"},
        {TypeRole, "entryType"},
        {SystemRole, "system"},
        {EnvironmentRole, "environment"},
        {UrlRole, "url"},
        {CustomerRole, "customer"},
        {UsernameRole, "username"},
        {TagsRole, "tags"},
        {OwnerRole, "owner"},
        {TwoFactorPlatformRole, "twoFactorPlatform"},
        {HasTotpRole, "hasTotp"},
        {ExpiresAtRole, "expiresAt"},
        {DaysLeftRole, "daysLeft"},
        {ExpiryStatusRole, "expiryStatus"},
        {ReminderChannelRole, "reminderChannel"},
    };
}

void EntryModel::setEntries(const QList<Entry> &entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

void EntryModel::setContacts(const QList<hvelv::Contact> &contacts)
{
    m_contactSearch.clear();
    for (const auto &c : contacts)
        m_contactSearch.insert(c.id, QStringList{c.name, c.email, c.company, c.role, c.phone}.join(' '));
    refreshAll();
}

void EntryModel::upsert(const Entry &entry)
{
    const int row = rowOf(entry.id);
    if (row >= 0) {
        m_entries[row] = entry;
        emit dataChanged(index(row), index(row));
    } else {
        beginInsertRows({}, int(m_entries.size()), int(m_entries.size()));
        m_entries.append(entry);
        endInsertRows();
    }
}

void EntryModel::remove(const QString &id)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
}

void EntryModel::refreshAll()
{
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(int(m_entries.size()) - 1));
}

int EntryModel::rowOf(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == id)
            return i;
    return -1;
}
