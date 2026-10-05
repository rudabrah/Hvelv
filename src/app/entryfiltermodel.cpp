#include "entryfiltermodel.h"
#include "entrymodel.h"

EntryFilterModel::EntryFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    sort(0);
    connect(this, &QAbstractItemModel::rowsInserted, this, &EntryFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &EntryFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &EntryFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &EntryFilterModel::countChanged);
}

void EntryFilterModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    invalidateRowsFilter();
    emit searchTextChanged();
    emit countChanged();
}

void EntryFilterModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    invalidateRowsFilter();
    emit filterChanged();
    emit countChanged();
}

void EntryFilterModel::setOwnerFilter(const QString &owner)
{
    if (m_owner == owner)
        return;
    m_owner = owner;
    invalidateRowsFilter();
    emit ownerFilterChanged();
    emit countChanged();
}

void EntryFilterModel::setCustomerFilter(const QString &customer)
{
    if (m_customer == customer)
        return;
    m_customer = customer;
    invalidateRowsFilter();
    emit customerFilterChanged();
    emit countChanged();
}

void EntryFilterModel::setSourceFilter(const QString &source)
{
    if (m_source == source)
        return;
    m_source = source;
    invalidateRowsFilter();
    emit sourceFilterChanged();
    emit countChanged();
}

void EntryFilterModel::setOnlyIds(const QStringList &ids)
{
    if (m_onlyIds == ids)
        return;
    m_onlyIds = ids;
    invalidateRowsFilter();
    emit onlyIdsChanged();
    emit countChanged();
}

QString EntryFilterModel::idAt(int row) const
{
    return data(index(row, 0), EntryModel::IdRole).toString();
}

int EntryFilterModel::rowOfId(const QString &id) const
{
    for (int r = 0; r < rowCount(); ++r)
        if (idAt(r) == id)
            return r;
    return -1;
}

bool EntryFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    const QString status = idx.data(EntryModel::ExpiryStatusRole).toString();

    if (m_filter == QLatin1String("soon") && status != QLatin1String("soon"))
        return false;
    if (m_filter == QLatin1String("expired") && status != QLatin1String("expired"))
        return false;
    if (m_filter == QLatin1String("attention") && status != QLatin1String("soon")
        && status != QLatin1String("expired"))
        return false;

    if (!m_onlyIds.isEmpty() && !m_onlyIds.contains(idx.data(EntryModel::IdRole).toString()))
        return false;
    if (!m_owner.isEmpty() && idx.data(EntryModel::OwnerRole).toString().compare(m_owner, Qt::CaseInsensitive) != 0)
        return false;
    if (!m_source.isEmpty() && idx.data(EntryModel::SourceRole).toString() != m_source)
        return false;
    if (!m_customer.isEmpty() && idx.data(EntryModel::CustomerRole).toString().compare(m_customer, Qt::CaseInsensitive) != 0)
        return false;

    if (m_searchText.trimmed().isEmpty())
        return true;
    const QString haystack = idx.data(EntryModel::SearchTextRole).toString();
    // Alle ord må finnes et sted i oppføringen.
    const auto words = m_searchText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const auto &w : words)
        if (!haystack.contains(w, Qt::CaseInsensitive))
            return false;
    return true;
}

bool EntryFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // Gruppér på kunde (oppføringer uten kunde sist), deretter navn.
    const QString lc = left.data(EntryModel::CustomerRole).toString();
    const QString rc = right.data(EntryModel::CustomerRole).toString();
    if (lc.isEmpty() != rc.isEmpty())
        return !lc.isEmpty();
    const int byCustomer = QString::localeAwareCompare(lc.toLower(), rc.toLower());
    if (byCustomer != 0)
        return byCustomer < 0;
    return QString::localeAwareCompare(left.data(EntryModel::TitleRole).toString(),
                                       right.data(EntryModel::TitleRole).toString())
           < 0;
}
