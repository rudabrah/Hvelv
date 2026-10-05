#include "entry.h"

#include <QJsonArray>
#include <QUuid>

#include <climits>

namespace hvelv {

namespace {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString isoDateTime(const QDateTime &dt)
{
    return dt.isValid() ? dt.toString(Qt::ISODateWithMs) : QString();
}

QDateTime parseDateTime(const QJsonValue &v)
{
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}

} // namespace

QString reminderChannelToString(ReminderChannel c)
{
    switch (c) {
    case ReminderChannel::Email: return QStringLiteral("email");
    case ReminderChannel::Both: return QStringLiteral("both");
    case ReminderChannel::App: break;
    }
    return QStringLiteral("app");
}

ReminderChannel reminderChannelFromString(const QString &s)
{
    if (s == QLatin1String("email")) return ReminderChannel::Email;
    if (s == QLatin1String("both")) return ReminderChannel::Both;
    return ReminderChannel::App;
}

// --- CustomField ---

QJsonObject CustomField::toJson() const
{
    return {{"name", name}, {"value", value}, {"secret", secret}};
}

CustomField CustomField::fromJson(const QJsonObject &o)
{
    return {o["name"].toString(), o["value"].toString(), o["secret"].toBool()};
}

// --- PasswordHistoryItem ---

QJsonObject PasswordHistoryItem::toJson() const
{
    return {{"password", password}, {"changedAt", isoDateTime(changedAt)}};
}

PasswordHistoryItem PasswordHistoryItem::fromJson(const QJsonObject &o)
{
    return {o["password"].toString(), parseDateTime(o["changedAt"])};
}

// --- Attachment ---

QJsonObject Attachment::toJson(bool includeData) const
{
    QJsonObject o{{"id", id}, {"name", name}, {"mimeType", mimeType},
                  {"size", qint64(data.size())}, {"addedAt", isoDateTime(addedAt)}};
    if (includeData)
        o["data"] = QString::fromLatin1(data.toBase64());
    return o;
}

Attachment Attachment::fromJson(const QJsonObject &o)
{
    Attachment a;
    a.id = o["id"].toString();
    a.name = o["name"].toString();
    a.mimeType = o["mimeType"].toString();
    a.data = QByteArray::fromBase64(o["data"].toString().toLatin1());
    a.addedAt = parseDateTime(o["addedAt"]);
    if (a.id.isEmpty())
        a.id = newId();
    return a;
}

// --- Contact ---

QJsonObject Contact::toJson() const
{
    return {{"id", id}, {"name", name}, {"company", company}, {"role", role},
            {"email", email}, {"phone", phone}, {"notes", notes}};
}

Contact Contact::fromJson(const QJsonObject &o)
{
    Contact c;
    c.id = o["id"].toString();
    c.name = o["name"].toString();
    c.company = o["company"].toString();
    c.role = o["role"].toString();
    c.email = o["email"].toString();
    c.phone = o["phone"].toString();
    c.notes = o["notes"].toString();
    if (c.id.isEmpty())
        c.id = newId();
    return c;
}

Contact Contact::createNew()
{
    Contact c;
    c.id = newId();
    return c;
}

// --- Entry ---

int Entry::daysUntilExpiry(const QDate &today) const
{
    if (!expiresAt.isValid())
        return INT_MIN;
    return int(today.daysTo(expiresAt));
}

ExpiryStatus Entry::expiryStatus(const QDate &today) const
{
    if (!expiresAt.isValid())
        return ExpiryStatus::None;
    const int days = daysUntilExpiry(today);
    if (days < 0)
        return ExpiryStatus::Expired;
    if (days <= reminderDays)
        return ExpiryStatus::ExpiringSoon;
    return ExpiryStatus::Ok;
}

void Entry::recordPasswordChange(const Entry &previous, const QDateTime &now)
{
    passwordHistory = previous.passwordHistory;
    passwordChangedAt = previous.passwordChangedAt;
    if (password == previous.password)
        return;
    if (!previous.password.isEmpty()) {
        passwordHistory.prepend({previous.password, now});
        while (passwordHistory.size() > MaxHistory)
            passwordHistory.removeLast();
    }
    passwordChangedAt = now;
}

QJsonObject Entry::toJson(bool includeAttachmentData) const
{
    QJsonArray history;
    for (const auto &h : passwordHistory)
        history.append(h.toJson());
    QJsonArray custom;
    for (const auto &f : customFields)
        custom.append(f.toJson());
    QJsonArray files;
    for (const auto &a : attachments)
        files.append(a.toJson(includeAttachmentData));

    QJsonObject o;
    o["id"] = id;
    o["type"] = type;
    o["title"] = title;
    o["url"] = url;
    o["customer"] = customer;
    o["system"] = system;
    o["environment"] = environment;
    o["tags"] = QJsonArray::fromStringList(tags);
    o["username"] = username;
    o["password"] = password;
    o["passwordChangedAt"] = isoDateTime(passwordChangedAt);
    o["passwordHistory"] = history;
    o["fields"] = fields;
    o["customFields"] = custom;
    o["twoFactorPlatform"] = twoFactorPlatform;
    o["totpSecret"] = totpSecret;
    o["contactIds"] = QJsonArray::fromStringList(contactIds);
    o["owner"] = owner;
    o["expiresAt"] = expiresAt.isValid() ? expiresAt.toString(Qt::ISODate) : QString();
    o["reminderDays"] = reminderDays;
    o["reminderChannel"] = reminderChannelToString(reminderChannel);
    o["renewalProcedure"] = renewalProcedure;
    o["notes"] = notes;
    o["attachments"] = files;
    o["createdAt"] = isoDateTime(createdAt);
    o["updatedAt"] = isoDateTime(updatedAt);
    return o;
}

Entry Entry::fromJson(const QJsonObject &o)
{
    Entry e;
    e.id = o["id"].toString();
    e.type = o["type"].toString();
    if (e.type.isEmpty())
        e.type = QStringLiteral("login");
    e.title = o["title"].toString();
    e.url = o["url"].toString();
    e.customer = o["customer"].toString();
    e.system = o["system"].toString();
    e.environment = o["environment"].toString();
    for (const auto &t : o["tags"].toArray())
        e.tags << t.toString();
    e.username = o["username"].toString();
    e.password = o["password"].toString();
    e.passwordChangedAt = parseDateTime(o["passwordChangedAt"]);
    for (const auto &h : o["passwordHistory"].toArray())
        e.passwordHistory << PasswordHistoryItem::fromJson(h.toObject());
    e.fields = o["fields"].toObject();
    for (const auto &f : o["customFields"].toArray())
        e.customFields << CustomField::fromJson(f.toObject());
    e.twoFactorPlatform = o["twoFactorPlatform"].toString();
    e.totpSecret = o["totpSecret"].toString();
    for (const auto &c : o["contactIds"].toArray())
        e.contactIds << c.toString();
    e.owner = o["owner"].toString();
    e.expiresAt = QDate::fromString(o["expiresAt"].toString(), Qt::ISODate);
    e.reminderDays = o["reminderDays"].toInt(14);
    e.reminderChannel = reminderChannelFromString(o["reminderChannel"].toString());
    e.renewalProcedure = o["renewalProcedure"].toString();
    e.notes = o["notes"].toString();
    for (const auto &a : o["attachments"].toArray())
        e.attachments << Attachment::fromJson(a.toObject());
    e.createdAt = parseDateTime(o["createdAt"]);
    e.updatedAt = parseDateTime(o["updatedAt"]);
    if (e.id.isEmpty())
        e.id = newId();
    return e;
}

Entry Entry::createNew()
{
    Entry e;
    e.id = newId();
    e.createdAt = QDateTime::currentDateTimeUtc();
    e.updatedAt = e.createdAt;
    return e;
}

} // namespace hvelv
