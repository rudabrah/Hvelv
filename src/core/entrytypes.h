#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Oppføringstyper og maler. Ren data, så QML kan bygge skjemaet dynamisk.
//
// Type:  { key, label, description, usernameLabel, passwordLabel, expiryLabel,
//          showUsername, fields: [ { key, label, secret, multiline, placeholder } ] }
// Mal:   { name, type, description, preset: { system, url, twoFactorPlatform, tags,
//          reminderDays, renewalProcedure, fields: { key: verdi } } }
namespace hvelv::entrytypes {

QJsonArray types();
QJsonObject type(const QString &key); // ukjent nøkkel gir "login"
QJsonArray templates();

// Feltnøkler som ikke er hemmelige, og derfor kan søkes i og vises i lister.
QStringList searchableFieldKeys(const QString &typeKey);

} // namespace hvelv::entrytypes
