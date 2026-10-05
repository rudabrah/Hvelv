// VaultController: QR-kode for TOTP.

#include "vaultcontroller.h"

#include "qrscanner.h"
#include "totp.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <QPointer>
#include <QTimer>
#include <QWindow>

using namespace hvelv;

namespace {

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QVariantMap pickTotp(const QStringList &codes)
{
    for (const auto &c : codes)
        if (c.startsWith(QLatin1String("otpauth://totp/"), Qt::CaseInsensitive) && totp::parse(c))
            return {{QStringLiteral("ok"), true}, {QStringLiteral("otpauth"), c}};
    if (codes.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Fant ingen QR-kode.")}};
    if (codes.first().startsWith(QLatin1String("otpauth-migration://"), Qt::CaseInsensitive))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Dette er en eksport fra Google Authenticator. Skann QR-koden fra tjenesten i stedet.")}};
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("QR-koden er ikke en TOTP-kode (otpauth://totp/…).")}};
}

} // namespace

void VaultController::scanQrFromScreen()
{
    // Skjul Hvelv-vinduene så de ikke dekker QR-koden, ta bildet, og vis dem igjen.
    QList<QPointer<QWindow>> hidden;
    for (QWindow *w : QGuiApplication::topLevelWindows()) {
        if (w->isVisible()) {
            hidden << w;
            w->hide();
        }
    }
    QTimer::singleShot(350, this, [this, hidden] {
        const QVariantMap result = pickTotp(qrscanner::scanScreens());
        for (const auto &w : hidden)
            if (w)
                w->show();
        if (!hidden.isEmpty() && hidden.first())
            hidden.first()->requestActivate();
        emit qrScanned(result);
    });
}

QVariantMap VaultController::scanQrFromClipboard() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasImage()) {
        // Kanskje brukeren kopierte selve otpauth-lenken
        const QString text = QGuiApplication::clipboard()->text().trimmed();
        if (text.startsWith(QLatin1String("otpauth://"), Qt::CaseInsensitive))
            return pickTotp({text});
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Utklippstavlen inneholder ikke et bilde. Ta et skjermbilde (Win+Shift+S) av QR-koden først.")}};
    }
    return pickTotp(qrscanner::decode(qvariant_cast<QImage>(mime->imageData())));
}

QVariantMap VaultController::scanQrFromFile(const QUrl &file) const
{
    QImageReader reader(localPath(file));
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Kunne ikke lese bildet.")}};
    return pickTotp(qrscanner::decode(image));
}
