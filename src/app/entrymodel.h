#pragma once

#include "entry.h"

#include <QHash>

#include <QAbstractListModel>
#include <QList>

// Listemodell for oppføringene. Passord og TOTP-hemmelighet eksponeres ikke som roller;
// de hentes ved behov via VaultController.
class EntryModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        TypeRole,
        SystemRole,
        EnvironmentRole,
        UrlRole,
        CustomerRole,
        UsernameRole,
        TagsRole,
        OwnerRole,
        TwoFactorPlatformRole,
        HasTotpRole,
        ExpiresAtRole,
        DaysLeftRole,
        ExpiryStatusRole, // "none" | "ok" | "soon" | "expired"
        ReminderChannelRole,
        SearchTextRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(const QList<hvelv::Entry> &entries);
    // Kontaktenes navn/e-post/firma tas med i søketeksten.
    void setContacts(const QList<hvelv::Contact> &contacts);
    void upsert(const hvelv::Entry &entry);
    void remove(const QString &id);
    void refreshAll(); // f.eks. når dagen skifter

private:
    int rowOf(const QString &id) const;
    QList<hvelv::Entry> m_entries;
    QHash<QString, QString> m_contactSearch; // kontakt-id -> søketekst
};

QString expiryStatusString(hvelv::ExpiryStatus s);
