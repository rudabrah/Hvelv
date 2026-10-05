#pragma once

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace hvelv {

enum class ReminderChannel { App, Email, Both };

enum class ExpiryStatus { None, Ok, ExpiringSoon, Expired };

struct CustomField
{
    QString name;
    QString value;
    bool secret = false;

    QJsonObject toJson() const;
    static CustomField fromJson(const QJsonObject &o);
};

struct PasswordHistoryItem
{
    QString password;
    QDateTime changedAt; // da passordet ble erstattet

    QJsonObject toJson() const;
    static PasswordHistoryItem fromJson(const QJsonObject &o);
};

struct Attachment
{
    QString id;
    QString name;
    QString mimeType;
    QByteArray data;
    QDateTime addedAt;

    QJsonObject toJson(bool includeData = true) const;
    static Attachment fromJson(const QJsonObject &o);
};

// Kontaktperson hos kunde/leverandør. Lagres én gang og kobles til mange oppføringer.
struct Contact
{
    QString id;
    QString name;
    QString company;
    QString role;
    QString email;
    QString phone;
    QString notes;

    QJsonObject toJson() const;
    static Contact fromJson(const QJsonObject &o);
    static Contact createNew();
};

struct Entry
{
    static constexpr int MaxHistory = 20;

    QString id;
    QString type = QStringLiteral("login"); // se entrytypes.h

    // System
    QString title;
    QString url;
    QString customer;    // kunde / tredjepart
    QString system;      // f.eks. "Visma.net", "Azure"
    QString environment; // "prod", "test", "qa", "dev" eller tomt
    QStringList tags;

    // Pålogging / hemmelighet. "password" er hovedhemmeligheten for alle typer
    // (klienthemmelighet, PFX-passord osv.), så Auto-Type og passordhelse virker overalt.
    QString username;
    QString password;
    QDateTime passwordChangedAt;
    QList<PasswordHistoryItem> passwordHistory;

    // Typespesifikke felt (nøkler definert i entrytypes) og egne felt
    QJsonObject fields;
    QList<CustomField> customFields;

    // 2FA
    QString twoFactorPlatform;
    QString totpSecret;

    // Personer
    QStringList contactIds;
    QString owner;

    // Utløp
    QDate expiresAt;
    int reminderDays = 14;
    ReminderChannel reminderChannel = ReminderChannel::App;
    QString renewalProcedure; // «Slik fornyer du»

    QString notes;
    QList<Attachment> attachments;

    QDateTime createdAt;
    QDateTime updatedAt;

    ExpiryStatus expiryStatus(const QDate &today = QDate::currentDate()) const;
    int daysUntilExpiry(const QDate &today = QDate::currentDate()) const; // INT_MIN hvis ingen dato

    // Tar vare på det gamle passordet i historikken hvis det er endret.
    void recordPasswordChange(const Entry &previous, const QDateTime &now = QDateTime::currentDateTimeUtc());

    QJsonObject toJson(bool includeAttachmentData = true) const;
    static Entry fromJson(const QJsonObject &o);
    static Entry createNew();
};

QString reminderChannelToString(ReminderChannel c);
ReminderChannel reminderChannelFromString(const QString &s);

} // namespace hvelv
