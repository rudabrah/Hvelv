#include "entrytypes.h"

namespace hvelv::entrytypes {

namespace {

QJsonObject field(const char *key, const char *label, bool secret = false, bool multiline = false,
                  const char *placeholder = "")
{
    return {{"key", QString::fromUtf8(key)},
            {"label", QString::fromUtf8(label)},
            {"secret", secret},
            {"multiline", multiline},
            {"placeholder", QString::fromUtf8(placeholder)}};
}

QJsonObject makeType(const char *key, const char *label, const char *description,
                     const char *usernameLabel, const char *passwordLabel, const char *expiryLabel,
                     bool showUsername, const QJsonArray &fields)
{
    return {{"key", QString::fromUtf8(key)},
            {"label", QString::fromUtf8(label)},
            {"description", QString::fromUtf8(description)},
            {"usernameLabel", QString::fromUtf8(usernameLabel)},
            {"passwordLabel", QString::fromUtf8(passwordLabel)},
            {"expiryLabel", QString::fromUtf8(expiryLabel)},
            {"showUsername", showUsername},
            {"fields", fields}};
}

const QJsonArray &allTypes()
{
    static const QJsonArray list = {
        makeType("login", "Pålogging", "Brukerkonto i en webportal, et fagsystem eller en tjeneste.",
                 "Brukernavn", "Passord", "Bruker utløper", true, {}),
        makeType("api", "API / app-registrering",
                 "Klienthemmelighet, API-nøkkel eller tjenestekonto for en integrasjon.",
                 "Brukernavn (valgfritt)", "Klienthemmelighet / API-nøkkel", "Hemmeligheten utløper", true,
                 {field("tenantId", "Tenant-ID"),
                  field("clientId", "Klient-ID (Application ID)"),
                  field("objectId", "Objekt-ID"),
                  field("scope", "Scope", false, false, "f.eks. https://graph.microsoft.com/.default"),
                  field("tokenUrl", "Token-URL"),
                  field("apiBaseUrl", "API-adresse")}),
        makeType("certificate", "Sertifikat",
                 "TLS-, klient- eller signeringssertifikat. Utløp leses fra filen.",
                 "Brukernavn (valgfritt)", "PFX-passord", "Sertifikatet utløper", false,
                 {field("subject", "Emne (Subject)"),
                  field("issuer", "Utsteder"),
                  field("thumbprint", "Fingeravtrykk (SHA-1)"),
                  field("serialNumber", "Serienummer"),
                  field("validFrom", "Gyldig fra"),
                  field("usedBy", "Brukes av", false, false, "f.eks. integrasjon, server, app-registrering")}),
        makeType("database", "Database", "Databasepålogging eller tilkoblingsstreng.",
                 "Brukernavn", "Passord", "Bruker utløper", true,
                 {field("engine", "Databasetype", false, false, "SQL Server, PostgreSQL, MySQL, Oracle …"),
                  field("host", "Server"),
                  field("port", "Port"),
                  field("database", "Databasenavn"),
                  field("connectionString", "Tilkoblingsstreng", true, true)}),
        makeType("server", "Server / SFTP / SSH",
                 "Filoverføring, fjernskrivebord eller skall mot en server.",
                 "Brukernavn", "Passord / nøkkelpassord", "Bruker utløper", true,
                 {field("protocol", "Protokoll", false, false, "SFTP, FTPS, SSH, RDP"),
                  field("host", "Server"),
                  field("port", "Port"),
                  field("remotePath", "Katalog"),
                  field("hostKeyFingerprint", "Vertsnøkkel (fingeravtrykk)"),
                  field("privateKey", "Privat nøkkel", true, true, "-----BEGIN OPENSSH PRIVATE KEY-----")}),
        makeType("note", "Sikkert notat", "Fritekst som skal holdes kryptert, f.eks. gjenopprettingskoder.",
                 "Brukernavn (valgfritt)", "Hemmelighet (valgfritt)", "Utløper", false,
                 {field("content", "Innhold", true, true)}),
    };
    return list;
}

QJsonObject preset(std::initializer_list<std::pair<const char *, QJsonValue>> values)
{
    QJsonObject o;
    for (const auto &[k, v] : values)
        o[QString::fromUtf8(k)] = v;
    return o;
}

QJsonObject makeTemplate(const char *name, const char *type, const char *description, const QJsonObject &p)
{
    return {{"name", QString::fromUtf8(name)},
            {"type", QString::fromUtf8(type)},
            {"description", QString::fromUtf8(description)},
            {"preset", p}};
}

} // namespace

QJsonArray types()
{
    return allTypes();
}

QJsonObject type(const QString &key)
{
    for (const auto &t : allTypes())
        if (t.toObject()["key"].toString() == key)
            return t.toObject();
    return allTypes().first().toObject();
}

QJsonArray templates()
{
    const QString entraRenewal = QStringLiteral(
        "1. Entra ID → App-registreringer → appen → Sertifikater og hemmeligheter\n"
        "2. Lag en ny klienthemmelighet (eller last opp nytt sertifikat)\n"
        "3. Oppdater integrasjonen / Key Vault med den nye verdien og test\n"
        "4. Slett den gamle hemmeligheten\n"
        "5. Oppdater utløpsdatoen her");

    return {
        makeTemplate("Pålogging (generell)", "login", "Webportal, fagsystem eller SaaS-tjeneste.", {}),
        makeTemplate("Microsoft 365 / Entra-bruker", "login", "Brukerkonto i kundens Microsoft-tenant.",
                     preset({{"system", "Microsoft 365"},
                             {"url", "https://portal.office.com"},
                             {"twoFactorPlatform", "Microsoft Authenticator"}})),
        makeTemplate("Entra ID app-registrering", "api",
                     "Klienthemmelighet for en integrasjon mot Microsoft Graph eller andre Microsoft-API.",
                     preset({{"system", "Entra ID"},
                             {"url", "https://entra.microsoft.com"},
                             {"reminderDays", 30},
                             {"renewalProcedure", entraRenewal},
                             {"fields", QJsonObject{
                                  {"tokenUrl", "https://login.microsoftonline.com/<tenant-id>/oauth2/v2.0/token"},
                                  {"scope", "https://graph.microsoft.com/.default"}}}})),
        makeTemplate("API-nøkkel", "api", "Statisk API-nøkkel eller token mot et tredjeparts-API.",
                     preset({{"reminderDays", 30}})),
        makeTemplate("Sertifikat", "certificate", "TLS-, klient- eller signeringssertifikat.",
                     preset({{"reminderDays", 30}})),
        makeTemplate("SQL Server-database", "database", "Pålogging mot Microsoft SQL Server.",
                     preset({{"fields", QJsonObject{{"engine", "SQL Server"}, {"port", "1433"}}}})),
        makeTemplate("PostgreSQL-database", "database", "Pålogging mot PostgreSQL.",
                     preset({{"fields", QJsonObject{{"engine", "PostgreSQL"}, {"port", "5432"}}}})),
        makeTemplate("SFTP-server", "server", "Filoverføring med SFTP.",
                     preset({{"fields", QJsonObject{{"protocol", "SFTP"}, {"port", "22"}}}})),
        makeTemplate("Windows-server (RDP)", "server", "Fjernskrivebord mot en Windows-server.",
                     preset({{"fields", QJsonObject{{"protocol", "RDP"}, {"port", "3389"}}}})),
        makeTemplate("Sikkert notat", "note", "Gjenopprettingskoder, avtaleinfo o.l.", {}),
    };
}

QStringList searchableFieldKeys(const QString &typeKey)
{
    QStringList keys;
    for (const auto &f : type(typeKey)["fields"].toArray()) {
        const auto o = f.toObject();
        if (!o["secret"].toBool())
            keys << o["key"].toString();
    }
    return keys;
}

} // namespace hvelv::entrytypes
