#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

struct CertificateInfo
{
    bool ok = false;
    QString error;
    QString subject;
    QString issuer;
    QString thumbprint;   // SHA-1, heksadesimalt
    QString serialNumber; // heksadesimalt, mest signifikante byte først
    QDateTime validFrom;
    QDateTime validTo;
};

// Leser sertifikat fra PFX/P12 (med passord), PEM eller DER. For PFX med kjede
// velges sertifikatet som ikke har utstedt noen av de andre (bladsertifikatet).
CertificateInfo readCertificate(const QByteArray &data, const QString &pfxPassword = {});
