// Lager appikonet: assets/icon-<str>.png og assets/hvelv.ico (PNG-baserte ICO-oppføringer).
// Bygg målet hvelv_make_icon og kjør:  hvelv_make_icon <assets-mappe>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QtEndian>

namespace {

// Tegnes stort og skaleres ned, så kantene blir myke også i små størrelser.
QImage render(int size)
{
    const int S = 1024;
    QImage img(S, S, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    // Bakgrunn: avrundet kvadrat med blå gradient
    const qreal inset = size <= 24 ? 0 : S * 0.04;
    const QRectF bg(inset, inset, S - 2 * inset, S - 2 * inset);
    QLinearGradient grad(bg.topLeft(), bg.bottomLeft());
    grad.setColorAt(0, QColor(0x3b, 0x82, 0xf6));
    grad.setColorAt(1, QColor(0x1e, 0x40, 0xaf));
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRoundedRect(bg, S * 0.22, S * 0.22);

    // Hengelås: bøyle
    const qreal cx = S / 2.0;
    const qreal shackleW = S * 0.36;
    const qreal stroke = size <= 24 ? S * 0.11 : S * 0.085;
    QPainterPath shackle;
    shackle.moveTo(cx - shackleW / 2, S * 0.50);
    shackle.lineTo(cx - shackleW / 2, S * 0.36);
    shackle.arcTo(QRectF(cx - shackleW / 2, S * 0.18, shackleW, shackleW), 180, -180);
    shackle.lineTo(cx + shackleW / 2, S * 0.50);
    p.setPen(QPen(Qt::white, stroke, Qt::SolidLine, Qt::FlatCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shackle);

    // Hengelås: kropp
    const QRectF body(S * 0.24, S * 0.44, S * 0.52, S * 0.40);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRoundedRect(body, S * 0.07, S * 0.07);

    // Nøkkelhull (utelatt i 16 px, der det bare blir støy)
    if (size > 16) {
        p.setBrush(QColor(0x1e, 0x40, 0xaf));
        const qreal r = S * 0.055;
        const QPointF c(cx, body.top() + body.height() * 0.42);
        p.drawEllipse(c, r, r);
        QPainterPath slot;
        slot.moveTo(c.x() - r * 0.45, c.y());
        slot.lineTo(c.x() + r * 0.45, c.y());
        slot.lineTo(c.x() + r * 0.7, c.y() + S * 0.12);
        slot.lineTo(c.x() - r * 0.7, c.y() + S * 0.12);
        slot.closeSubpath();
        p.drawPath(slot);
    }
    p.end();

    return img.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32);
}

QByteArray toPng(const QImage &img)
{
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return data;
}

// ICO-container: 6 B header + 16 B per oppføring + PNG-data (støttet siden Windows Vista).
QByteArray buildIco(const QList<QPair<int, QByteArray>> &pngs)
{
    QByteArray out;
    auto u16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); out.append(b, 2); };
    auto u32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); out.append(b, 4); };

    u16(0); u16(1); u16(quint16(pngs.size()));
    quint32 offset = 6 + 16 * quint32(pngs.size());
    for (const auto &[size, png] : pngs) {
        out.append(char(size >= 256 ? 0 : size)); // 0 betyr 256
        out.append(char(size >= 256 ? 0 : size));
        out.append(char(0)); out.append(char(0)); // ingen palett, reservert
        u16(1); u16(32);                          // fargeplan, bits per piksel
        u32(quint32(png.size()));
        u32(offset);
        offset += quint32(png.size());
    }
    for (const auto &entry : pngs)
        out.append(entry.second);
    return out;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QDir dir(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("assets"));
    QDir().mkpath(dir.path());

    QList<QPair<int, QByteArray>> icoEntries;
    for (int size : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        const QByteArray png = toPng(render(size));
        icoEntries.append({size, png});
        QFile f(dir.filePath(QStringLiteral("icon-%1.png").arg(size)));
        if (!f.open(QIODevice::WriteOnly) || f.write(png) != png.size())
            return 1;
    }
    QFile ico(dir.filePath(QStringLiteral("hvelv.ico")));
    if (!ico.open(QIODevice::WriteOnly))
        return 1;
    ico.write(buildIco(icoEntries));
    return 0;
}
