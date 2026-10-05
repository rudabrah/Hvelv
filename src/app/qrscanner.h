#pragma once

#include <QImage>
#include <QStringList>

// QR-dekoding med ZXing-cpp. Bilder holdes bare i minnet og lagres aldri.
namespace qrscanner {

QStringList decode(const QImage &image);
QStringList scanScreens(); // alle skjermer

} // namespace qrscanner
