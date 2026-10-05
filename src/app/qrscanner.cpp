#include "qrscanner.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

#include <ReadBarcode.h> // ZXing (third_party/zxing-cpp/core/src)

namespace qrscanner {

QStringList decode(const QImage &image)
{
    if (image.isNull())
        return {};
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const ZXing::ImageView view(gray.constBits(), gray.width(), gray.height(), ZXing::ImageFormat::Lum,
                                int(gray.bytesPerLine()));
    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTryHarder(true);

    QStringList out;
    for (const auto &barcode : ZXing::ReadBarcodes(view, options))
        if (barcode.isValid())
            out << QString::fromStdString(barcode.text());
    return out;
}

QStringList scanScreens()
{
    QStringList out;
    // Hver skjerm dekodes for seg i full oppløsning (ulik skalering per skjerm).
    for (QScreen *s : QGuiApplication::screens())
        out << decode(s->grabWindow(0).toImage());
    out.removeDuplicates();
    return out;
}

} // namespace qrscanner
