#pragma once

#include <QSortFilterProxyModel>
#include <QtQml/qqmlregistration.h>

class EntryFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Hentes fra Vault.entries")

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    // "all" | "attention" (utløper snart + utløpt) | "soon" | "expired"
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString ownerFilter READ ownerFilter WRITE setOwnerFilter NOTIFY ownerFilterChanged)
    Q_PROPERTY(QString customerFilter READ customerFilter WRITE setCustomerFilter NOTIFY customerFilterChanged)
    // Ikke tom = vis bare disse (brukes av Auto-Type når flere oppføringer passer)
    Q_PROPERTY(QStringList onlyIds READ onlyIds WRITE setOnlyIds NOTIFY onlyIdsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit EntryFilterModel(QObject *parent = nullptr);

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString ownerFilter() const { return m_owner; }
    void setOwnerFilter(const QString &owner);
    QString customerFilter() const { return m_customer; }
    void setCustomerFilter(const QString &customer);
    QStringList onlyIds() const { return m_onlyIds; }
    void setOnlyIds(const QStringList &ids);
    int count() const { return rowCount(); }

    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE int rowOfId(const QString &id) const;

signals:
    void searchTextChanged();
    void filterChanged();
    void ownerFilterChanged();
    void customerFilterChanged();
    void onlyIdsChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_searchText;
    QString m_filter = QStringLiteral("all");
    QString m_owner;    // tom = alle
    QString m_customer; // tom = alle
    QStringList m_onlyIds;
};
