#include "importexport.h"

#include <QDataStream>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QUuid>
#include <QXmlStreamReader>

#include <optional>

namespace hvelv::importexport {

namespace {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

Entry blankEntry()
{
    Entry e = Entry::createNew();
    return e;
}

void addTag(Entry &e, const QString &tag)
{
    const QString t = tag.trimmed();
    if (!t.isEmpty() && !e.tags.contains(t, Qt::CaseInsensitive))
        e.tags << t;
}

// TOTP kan komme som otpauth://-URI, ren base32 eller KeePass sitt «key=...&step=..»-format.
QString normalizeTotp(const QString &raw)
{
    const QString v = raw.trimmed();
    if (v.isEmpty() || v.startsWith(QLatin1String("otpauth://"), Qt::CaseInsensitive))
        return v;
    if (v.contains(QLatin1String("key="))) {
        for (const auto &part : v.split(QLatin1Char('&')))
            if (part.startsWith(QLatin1String("key=")))
                return part.mid(4);
    }
    return v;
}

// --- KeePass XML ---------------------------------------------------------

QDate parseKeePassTime(const QString &text)
{
    const QString t = text.trimmed();
    QDateTime dt = QDateTime::fromString(t, Qt::ISODate);
    if (dt.isValid())
        return dt.toUTC().date();
    // KDBX4: base64 av int64 sekunder siden 0001-01-01 (little endian)
    const QByteArray raw = QByteArray::fromBase64(t.toLatin1());
    if (raw.size() == 8) {
        qint64 secs = 0;
        QDataStream s(raw);
        s.setByteOrder(QDataStream::LittleEndian);
        s >> secs;
        const QDateTime epoch(QDate(1, 1, 1), QTime(0, 0), QTimeZone::UTC);
        return epoch.addSecs(secs).date();
    }
    return {};
}

ImportResult importKeePassXml(const QByteArray &data)
{
    ImportResult r;
    r.format = QStringLiteral("KeePass XML");
    QXmlStreamReader xml(data);
    QStringList groupPath;
    std::optional<Entry> current;
    int historyDepth = 0;
    int attachmentsSkipped = 0;

    // Tilstand for <String><Key/><Value/></String> og <Times>
    QString key, value;
    bool protectedValue = false;
    bool expires = false;
    QDate expiryTime;

    while (!xml.atEnd()) {
        xml.readNext();
        const auto name = xml.name();
        if (xml.isStartElement()) {
            if (name == QLatin1String("Group")) {
                groupPath << QString();
            } else if (name == QLatin1String("Name") && !current && !groupPath.isEmpty()) {
                groupPath.last() = xml.readElementText();
            } else if (name == QLatin1String("History")) {
                ++historyDepth; // gamle versjoner av oppføringen tas ikke med
            } else if (name == QLatin1String("Entry") && historyDepth == 0) {
                current = blankEntry();
                expires = false;
                expiryTime = {};
            } else if (current && historyDepth == 0) {
                if (name == QLatin1String("Key")) {
                    key = xml.readElementText();
                } else if (name == QLatin1String("Value")) {
                    protectedValue = xml.attributes().value(QLatin1String("ProtectInMemory")) == QLatin1String("True")
                                     || xml.attributes().value(QLatin1String("Protected")) == QLatin1String("True");
                    value = xml.readElementText();
                } else if (name == QLatin1String("Expires")) {
                    expires = xml.readElementText().compare(QLatin1String("True"), Qt::CaseInsensitive) == 0;
                } else if (name == QLatin1String("ExpiryTime")) {
                    expiryTime = parseKeePassTime(xml.readElementText());
                } else if (name == QLatin1String("Binary")) {
                    ++attachmentsSkipped;
                }
            }
        } else if (xml.isEndElement()) {
            if (name == QLatin1String("Group")) {
                if (!groupPath.isEmpty())
                    groupPath.removeLast();
            } else if (name == QLatin1String("History")) {
                --historyDepth;
            } else if (name == QLatin1String("String") && current && historyDepth == 0) {
                if (key == QLatin1String("Title"))
                    current->title = value;
                else if (key == QLatin1String("UserName"))
                    current->username = value;
                else if (key == QLatin1String("Password"))
                    current->password = value;
                else if (key == QLatin1String("URL"))
                    current->url = value;
                else if (key == QLatin1String("Notes"))
                    current->notes = value;
                else if (key == QLatin1String("otp") || key == QLatin1String("TOTP Seed"))
                    current->totpSecret = normalizeTotp(value);
                else if (key == QLatin1String("TOTP Settings"))
                    ; // standardverdier (30 s / 6 siffer) brukes
                else if (!value.isEmpty())
                    current->customFields << CustomField{key, value, protectedValue};
                key.clear();
                value.clear();
            } else if (name == QLatin1String("Entry") && current && historyDepth == 0) {
                if (expires && expiryTime.isValid())
                    current->expiresAt = expiryTime;
                // Øverste gruppe er databasens rot; resten blir en tagg.
                const QStringList path = groupPath.mid(1);
                if (!path.isEmpty() && path.first() != QLatin1String("Recycle Bin")
                    && path.first() != QLatin1String("Papirkurv"))
                    addTag(*current, path.join(QStringLiteral(" / ")));
                const bool inTrash = !path.isEmpty()
                                     && (path.first() == QLatin1String("Recycle Bin")
                                         || path.first() == QLatin1String("Papirkurv"));
                if (!inTrash && (!current->title.isEmpty() || !current->username.isEmpty()
                                 || !current->password.isEmpty())) {
                    if (current->title.isEmpty())
                        current->title = current->url.isEmpty() ? current->username : current->url;
                    r.entries << *current;
                }
                current.reset();
            }
        }
    }
    if (xml.hasError()) {
        r.error = QStringLiteral("Ugyldig XML: %1").arg(xml.errorString());
        return r;
    }
    if (attachmentsSkipped > 0)
        r.warnings << QStringLiteral("%1 vedlegg ble ikke importert (legg dem til manuelt).").arg(attachmentsSkipped);
    r.ok = true;
    return r;
}

// --- Bitwarden JSON ------------------------------------------------------

ImportResult importBitwardenJson(const QJsonObject &root)
{
    ImportResult r;
    r.format = QStringLiteral("Bitwarden JSON");
    if (root[QStringLiteral("encrypted")].toBool()) {
        r.error = QStringLiteral("Kryptert Bitwarden-eksport støttes ikke. Eksporter som «.json» uten kryptering.");
        return r;
    }
    QHash<QString, QString> folders;
    for (const auto &f : root[QStringLiteral("folders")].toArray())
        folders.insert(f.toObject()[QStringLiteral("id")].toString(), f.toObject()[QStringLiteral("name")].toString());
    for (const auto &c : root[QStringLiteral("collections")].toArray())
        folders.insert(c.toObject()[QStringLiteral("id")].toString(), c.toObject()[QStringLiteral("name")].toString());

    int unsupported = 0;
    for (const auto &v : root[QStringLiteral("items")].toArray()) {
        const QJsonObject item = v.toObject();
        const int type = item[QStringLiteral("type")].toInt(1);
        Entry e = blankEntry();
        e.title = item[QStringLiteral("name")].toString();
        e.notes = item[QStringLiteral("notes")].toString();
        addTag(e, folders.value(item[QStringLiteral("folderId")].toString()));
        for (const auto &cid : item[QStringLiteral("collectionIds")].toArray())
            addTag(e, folders.value(cid.toString()));

        if (type == 1) { // innlogging
            const QJsonObject login = item[QStringLiteral("login")].toObject();
            e.username = login[QStringLiteral("username")].toString();
            e.password = login[QStringLiteral("password")].toString();
            e.totpSecret = normalizeTotp(login[QStringLiteral("totp")].toString());
            const QJsonArray uris = login[QStringLiteral("uris")].toArray();
            if (!uris.isEmpty())
                e.url = uris.first().toObject()[QStringLiteral("uri")].toString();
            for (int i = 1; i < uris.size(); ++i)
                e.customFields << CustomField{QStringLiteral("URL %1").arg(i + 1),
                                              uris[i].toObject()[QStringLiteral("uri")].toString(), false};
        } else if (type == 2) { // sikkert notat
            e.type = QStringLiteral("note");
            e.fields[QStringLiteral("content")] = e.notes;
            e.notes.clear();
        } else {
            ++unsupported; // kort og identiteter
            continue;
        }
        for (const auto &f : item[QStringLiteral("fields")].toArray()) {
            const QJsonObject o = f.toObject();
            e.customFields << CustomField{o[QStringLiteral("name")].toString(),
                                          o[QStringLiteral("value")].toVariant().toString(),
                                          o[QStringLiteral("type")].toInt() == 1};
        }
        r.entries << e;
    }
    if (unsupported > 0)
        r.warnings << QStringLiteral("%1 kort/identiteter ble hoppet over.").arg(unsupported);
    r.ok = true;
    return r;
}

// --- CSV -----------------------------------------------------------------

QString canonicalColumn(const QString &header)
{
    static const QHash<QString, QString> map = {
        {"title", "title"}, {"name", "title"}, {"navn", "title"}, {"tittel", "title"}, {"system", "system"},
        {"username", "username"}, {"user name", "username"}, {"login_username", "username"},
        {"brukernavn", "username"}, {"user", "username"}, {"login", "username"}, {"email", "username"},
        {"password", "password"}, {"login_password", "password"}, {"passord", "password"},
        {"url", "url"}, {"uri", "url"}, {"login_uri", "url"}, {"website", "url"}, {"adresse", "url"},
        {"notes", "notes"}, {"note", "notes"}, {"notater", "notes"}, {"merknad", "notes"},
        {"totp", "totp"}, {"otp", "totp"}, {"otpauth", "totp"}, {"login_totp", "totp"},
        {"group", "group"}, {"folder", "group"}, {"gruppe", "group"}, {"mappe", "group"},
        {"tags", "tags"}, {"tagger", "tags"},
        {"customer", "customer"}, {"kunde", "customer"}, {"tredjepart", "customer"},
        {"environment", "environment"}, {"miljø", "environment"},
        {"owner", "owner"}, {"ansvarlig", "owner"},
        {"expires", "expiresAt"}, {"expiry", "expiresAt"}, {"expiresat", "expiresAt"}, {"utløper", "expiresAt"},
        {"type", "type"},
    };
    return map.value(header.trimmed().toLower());
}

ImportResult importCsv(const QString &text)
{
    ImportResult r;
    const QString firstLine = text.section(QLatin1Char('\n'), 0, 0);
    const QChar delimiter = firstLine.count(QLatin1Char(';')) > firstLine.count(QLatin1Char(','))
                                ? QLatin1Char(';') : QLatin1Char(',');
    const auto rows = parseCsv(text, delimiter);
    if (rows.size() < 1) {
        r.error = QStringLiteral("CSV-filen er tom.");
        return r;
    }
    const QStringList header = rows.first();
    QHash<QString, int> col;
    QList<int> unknownColumns;
    for (int i = 0; i < header.size(); ++i) {
        const QString canon = canonicalColumn(header[i]);
        if (!canon.isEmpty() && !col.contains(canon))
            col.insert(canon, i);
        else if (canon.isEmpty())
            unknownColumns << i;
    }
    if (!col.contains(QStringLiteral("title")) && !col.contains(QStringLiteral("username"))
        && !col.contains(QStringLiteral("password"))) {
        r.error = QStringLiteral("Fant ingen kjente kolonner (navn, brukernavn, passord, URL …) i første linje.");
        return r;
    }

    const QString h = header.join(QLatin1Char(',')).toLower();
    if (h.contains(QLatin1String("login_uri")))
        r.format = QStringLiteral("Bitwarden CSV");
    else if (h.contains(QLatin1String("otpauth")) && h.contains(QLatin1String("archived")))
        r.format = QStringLiteral("1Password CSV");
    else if (h.contains(QLatin1String("group")) && h.contains(QLatin1String("last modified")))
        r.format = QStringLiteral("KeePassXC CSV");
    else if (header.size() >= 4 && header.size() <= 5 && h.startsWith(QLatin1String("name,url,username,password")))
        r.format = QStringLiteral("Chrome/Edge CSV");
    else
        r.format = QStringLiteral("CSV");

    // Kolonner fra eksporter som ikke gir mening å ta med som egne felt
    static const QSet<QString> ignored = {"favorite", "archived", "reprompt", "icon", "last modified",
                                          "created", "fields", "note"};
    auto cell = [&](const QStringList &row, const char *name) {
        const int i = col.value(QString::fromLatin1(name), -1);
        return i >= 0 && i < row.size() ? row[i] : QString();
    };

    for (int r_i = 1; r_i < rows.size(); ++r_i) {
        const QStringList &row = rows[r_i];
        if (row.size() == 1 && row.first().trimmed().isEmpty())
            continue;
        Entry e = blankEntry();
        e.title = cell(row, "title");
        e.username = cell(row, "username");
        e.password = cell(row, "password");
        e.url = cell(row, "url");
        e.notes = cell(row, "notes");
        e.totpSecret = normalizeTotp(cell(row, "totp"));
        e.customer = cell(row, "customer");
        e.system = cell(row, "system");
        e.environment = cell(row, "environment");
        e.owner = cell(row, "owner");
        const QDate expiry = QDate::fromString(cell(row, "expiresAt").left(10), Qt::ISODate);
        if (expiry.isValid())
            e.expiresAt = expiry;
        const QString type = cell(row, "type");
        if (r.format == QLatin1String("Bitwarden CSV") && type == QLatin1String("note")) {
            e.type = QStringLiteral("note");
            e.fields[QStringLiteral("content")] = e.notes;
            e.notes.clear();
        }
        QString group = cell(row, "group");
        if (r.format == QLatin1String("KeePassXC CSV") && group.startsWith(QLatin1String("Root/")))
            group = group.mid(5);
        else if (group == QLatin1String("Root"))
            group.clear();
        addTag(e, group);
        for (const auto &t : cell(row, "tags").split(QRegularExpression(QStringLiteral("[,;]"))))
            addTag(e, t);
        for (int i : unknownColumns) {
            if (i < row.size() && !row[i].trimmed().isEmpty() && !ignored.contains(header[i].trimmed().toLower()))
                e.customFields << CustomField{header[i].trimmed(), row[i], false};
        }
        if (e.title.isEmpty())
            e.title = !e.url.isEmpty() ? e.url : e.username;
        if (e.title.isEmpty() && e.password.isEmpty())
            continue;
        r.entries << e;
    }
    r.ok = true;
    return r;
}

QString csvEscape(const QString &v)
{
    if (v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"')) || v.contains(QLatin1Char('\n'))
        || v.contains(QLatin1Char('\r'))) {
        QString q = v;
        q.replace(QLatin1String("\""), QLatin1String("\"\""));
        return QLatin1Char('"') + q + QLatin1Char('"');
    }
    return v;
}

QString duplicateKey(const Entry &e)
{
    return QStringList{e.title.trimmed().toLower(), e.username.trimmed().toLower(),
                       e.url.trimmed().toLower(), e.password}
        .join(QChar(0x1f));
}

} // namespace

QList<QStringList> parseCsv(const QString &text, QChar delimiter)
{
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool inQuotes = false;
    bool fieldStarted = false;
    const int n = int(text.size());
    for (int i = 0; i < n; ++i) {
        const QChar c = text[i];
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < n && text[i + 1] == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') && !fieldStarted) {
            inQuotes = true;
            fieldStarted = true;
        } else if (c == delimiter) {
            row << field;
            field.clear();
            fieldStarted = false;
        } else if (c == QLatin1Char('\r')) {
            // ignoreres; \n avslutter raden
        } else if (c == QLatin1Char('\n')) {
            row << field;
            rows << row;
            row.clear();
            field.clear();
            fieldStarted = false;
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (fieldStarted || !field.isEmpty() || !row.isEmpty()) {
        row << field;
        rows << row;
    }
    return rows;
}

ImportResult importData(const QByteArray &data, const QString &fileName)
{
    QByteArray bytes = data;
    if (bytes.startsWith("\xEF\xBB\xBF")) // UTF-8 BOM
        bytes.remove(0, 3);
    const QByteArray head = bytes.left(512).trimmed();

    if (head.startsWith("<?xml") || head.startsWith("<KeePassFile")) {
        if (!bytes.contains("<KeePassFile")) {
            ImportResult r;
            r.error = QStringLiteral("XML-filen er ikke en KeePass-eksport.");
            return r;
        }
        return importKeePassXml(bytes);
    }
    if (head.startsWith("{")) {
        const auto doc = QJsonDocument::fromJson(bytes);
        if (doc.isObject() && doc.object().contains(QStringLiteral("items")))
            return importBitwardenJson(doc.object());
        ImportResult r;
        r.error = QStringLiteral("JSON-filen er ikke en kjent eksport (forventet Bitwarden).");
        return r;
    }
    if (bytes.startsWith("HVELVDB1")) {
        ImportResult r;
        r.error = QStringLiteral("Hvelv-filer må importeres med passord.");
        return r;
    }
    if (fileName.endsWith(QLatin1String(".kdbx"), Qt::CaseInsensitive)) {
        ImportResult r;
        r.error = QStringLiteral("KeePass-databaser (.kdbx) kan ikke leses direkte. "
                                 "Bruk «Eksporter → KeePass XML» eller CSV i KeePass/KeePassXC.");
        return r;
    }
    return importCsv(QString::fromUtf8(bytes));
}

MergeStats merge(VaultData &target, const ImportResult &imported, const MergeOptions &options)
{
    MergeStats stats;
    QSet<QString> existing;
    for (const auto &e : std::as_const(target.entries))
        existing.insert(duplicateKey(e));

    QHash<QString, QString> contactIdMap; // gammel id -> ny id
    for (const auto &c : imported.contacts) {
        Contact copy = c;
        copy.id = newId();
        contactIdMap.insert(c.id, copy.id);
        target.contacts << copy;
        ++stats.contactsAdded;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (Entry e : imported.entries) {
        if (!options.customer.trimmed().isEmpty())
            e.customer = options.customer.trimmed();
        if (!options.tag.trimmed().isEmpty())
            addTag(e, options.tag);
        const QString key = duplicateKey(e);
        if (options.skipDuplicates && existing.contains(key)) {
            ++stats.skipped;
            continue;
        }
        existing.insert(key);
        e.id = newId();
        for (auto &cid : e.contactIds)
            cid = contactIdMap.value(cid);
        e.contactIds.removeAll(QString());
        if (!e.createdAt.isValid())
            e.createdAt = now;
        e.updatedAt = now;
        target.entries << e;
        ++stats.added;
    }
    return stats;
}

QByteArray exportCsv(const VaultData &data)
{
    const QStringList header{"title", "type", "customer", "system", "environment", "url", "username",
                             "password", "totp", "twoFactorPlatform", "owner", "contacts", "expiresAt",
                             "tags", "notes"};
    QStringList lines{header.join(QLatin1Char(','))};
    for (const auto &e : data.entries) {
        QStringList contacts;
        for (const auto &cid : e.contactIds)
            if (const Contact *c = data.contact(cid))
                contacts << (c->email.isEmpty() ? c->name : QStringLiteral("%1 <%2>").arg(c->name, c->email));
        const QStringList row{e.title, e.type, e.customer, e.system, e.environment, e.url, e.username,
                              e.password, e.totpSecret, e.twoFactorPlatform, e.owner,
                              contacts.join(QStringLiteral("; ")),
                              e.expiresAt.isValid() ? e.expiresAt.toString(Qt::ISODate) : QString(),
                              e.tags.join(QStringLiteral(", ")), e.notes};
        QStringList escaped;
        for (const auto &v : row)
            escaped << csvEscape(v);
        lines << escaped.join(QLatin1Char(','));
    }
    return lines.join(QStringLiteral("\r\n")).toUtf8() + "\r\n";
}

} // namespace hvelv::importexport
