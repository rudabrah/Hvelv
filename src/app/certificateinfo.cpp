#include "certificateinfo.h"

#include <QList>
#include <QTimeZone>

#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <wincrypt.h>
#endif

#ifdef Q_OS_WIN
namespace {

QDateTime fromFileTime(const FILETIME &ft)
{
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    const qint64 msecs = qint64((v.QuadPart - 116444736000000000ULL) / 10000ULL);
    return QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC);
}

QString nameString(PCCERT_CONTEXT ctx, DWORD flags)
{
    DWORD strType = CERT_X500_NAME_STR;
    const DWORD len = CertGetNameStringW(ctx, CERT_NAME_RDN_TYPE, flags, &strType, nullptr, 0);
    if (len <= 1)
        return {};
    std::wstring buf(len, L'\0');
    CertGetNameStringW(ctx, CERT_NAME_RDN_TYPE, flags, &strType, buf.data(), len);
    return QString::fromWCharArray(buf.c_str());
}

bool hasPrivateKey(PCCERT_CONTEXT ctx)
{
    DWORD size = 0;
    for (DWORD prop : {DWORD(CERT_KEY_PROV_INFO_PROP_ID), DWORD(CERT_KEY_CONTEXT_PROP_ID),
                       DWORD(CERT_NCRYPT_KEY_HANDLE_PROP_ID)})
        if (CertGetCertificateContextProperty(ctx, prop, nullptr, &size))
            return true;
    return false;
}

CertificateInfo describe(PCCERT_CONTEXT ctx)
{
    CertificateInfo info;
    info.ok = true;
    info.subject = nameString(ctx, 0);
    info.issuer = nameString(ctx, CERT_NAME_ISSUER_FLAG);
    info.validFrom = fromFileTime(ctx->pCertInfo->NotBefore);
    info.validTo = fromFileTime(ctx->pCertInfo->NotAfter);

    BYTE hash[20];
    DWORD hashLen = sizeof(hash);
    if (CertGetCertificateContextProperty(ctx, CERT_SHA1_HASH_PROP_ID, hash, &hashLen))
        info.thumbprint = QByteArray(reinterpret_cast<const char *>(hash), int(hashLen)).toHex().toUpper();

    // Serienummeret lagres little endian i CERT_INFO.
    const CRYPT_INTEGER_BLOB &serial = ctx->pCertInfo->SerialNumber;
    QByteArray s(reinterpret_cast<const char *>(serial.pbData), int(serial.cbData));
    std::reverse(s.begin(), s.end());
    info.serialNumber = s.toHex().toUpper();
    return info;
}

CertificateInfo readPfx(const QByteArray &data, const QString &password)
{
    CRYPT_DATA_BLOB blob{DWORD(data.size()), reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()))};
    const std::wstring pw = password.toStdWString();
    // PKCS12_NO_PERSIST_KEY: ikke legg den private nøkkelen i Windows' nøkkellager.
    HCERTSTORE store = PFXImportCertStore(&blob, pw.c_str(), PKCS12_NO_PERSIST_KEY);
    if (!store && password.isEmpty())
        store = PFXImportCertStore(&blob, nullptr, PKCS12_NO_PERSIST_KEY);
    if (!store) {
        CertificateInfo err;
        err.error = GetLastError() == ERROR_INVALID_PASSWORD
                        ? QStringLiteral("Feil PFX-passord.")
                        : QStringLiteral("Kunne ikke lese PFX-filen.");
        return err;
    }

    QList<PCCERT_CONTEXT> certs;
    for (PCCERT_CONTEXT c = nullptr; (c = CertEnumCertificatesInStore(store, c));)
        certs << CertDuplicateCertificateContext(c);

    PCCERT_CONTEXT chosen = nullptr;
    for (auto c : certs)
        if (hasPrivateKey(c))
            chosen = c;
    if (!chosen) {
        for (auto c : certs) {
            bool issuesOthers = false;
            for (auto other : certs)
                if (other != c && CertCompareCertificateName(X509_ASN_ENCODING, &other->pCertInfo->Issuer,
                                                             &c->pCertInfo->Subject))
                    issuesOthers = true;
            if (!issuesOthers) {
                chosen = c;
                break;
            }
        }
    }

    CertificateInfo info;
    if (chosen)
        info = describe(chosen);
    else
        info.error = QStringLiteral("PFX-filen inneholder ingen sertifikater.");
    for (auto c : certs)
        CertFreeCertificateContext(c);
    CertCloseStore(store, 0);
    return info;
}

CertificateInfo readX509(const QByteArray &data)
{
    QByteArray der = data;
    if (data.contains("-----BEGIN")) {
        DWORD len = 0;
        if (!CryptStringToBinaryA(data.constData(), DWORD(data.size()), CRYPT_STRING_BASE64HEADER,
                                  nullptr, &len, nullptr, nullptr)) {
            CertificateInfo err;
            err.error = QStringLiteral("Ugyldig PEM-fil.");
            return err;
        }
        der.resize(int(len));
        CryptStringToBinaryA(data.constData(), DWORD(data.size()), CRYPT_STRING_BASE64HEADER,
                             reinterpret_cast<BYTE *>(der.data()), &len, nullptr, nullptr);
    }
    PCCERT_CONTEXT ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                      reinterpret_cast<const BYTE *>(der.constData()),
                                                      DWORD(der.size()));
    if (!ctx) {
        CertificateInfo err;
        err.error = QStringLiteral("Filen er ikke et gjenkjennelig sertifikat.");
        return err;
    }
    CertificateInfo info = describe(ctx);
    CertFreeCertificateContext(ctx);
    return info;
}

} // namespace
#endif

CertificateInfo readCertificate(const QByteArray &data, const QString &pfxPassword)
{
#ifdef Q_OS_WIN
    CRYPT_DATA_BLOB blob{DWORD(data.size()), reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()))};
    if (PFXIsPFXBlob(&blob))
        return readPfx(data, pfxPassword);
    return readX509(data);
#else
    Q_UNUSED(data);
    Q_UNUSED(pfxPassword);
    CertificateInfo err;
    err.error = QStringLiteral("Sertifikatlesing er bare støttet på Windows.");
    return err;
#endif
}
