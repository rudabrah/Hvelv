#include "vaultcontroller.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(HvelvPlugin)

namespace {

// Ikonfilene ligger i assets/ og lages av tools/make_icon.cpp.
QIcon makeAppIcon()
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/assets/icon-%1.png").arg(size), QSize(size, size));
    return icon;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Hvelv"));
    QApplication::setOrganizationName(QStringLiteral("Hvelv"));
    QApplication::setApplicationVersion(QStringLiteral(HVELV_VERSION));
    const QIcon appIcon = makeAppIcon();
    QApplication::setWindowIcon(appIcon);
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Hvelv – passordhvelv for tredjepartssystemer"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("fil"), QStringLiteral("Hvelvfil som skal åpnes (valgfritt)."));
    parser.process(app);

    QSystemTrayIcon tray(appIcon);
    tray.setToolTip(QStringLiteral("Hvelv"));
    QMenu trayMenu;
    QAction *showAction = trayMenu.addAction(QStringLiteral("Vis Hvelv"));
    QAction *lockAction = trayMenu.addAction(QStringLiteral("Lås"));
    trayMenu.addSeparator();
    QAction *quitAction = trayMenu.addAction(QStringLiteral("Avslutt"));
    tray.setContextMenu(&trayMenu);
    tray.show();

    QQmlApplicationEngine engine;
    // QML eier singletonen; vi henter den ut og kobler den til før Main lastes.
    auto *vault = engine.singletonInstance<VaultController *>("Hvelv", "Vault");
    if (!vault)
        return -1;
    vault->setTrayIcon(&tray);
    if (!parser.positionalArguments().isEmpty())
        vault->useVaultPathForSession(parser.positionalArguments().first());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("Hvelv", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto raise = [window] {
        if (!window)
            return;
        window->show();
        window->raise();
        window->requestActivate();
    };
    QObject::connect(showAction, &QAction::triggered, raise);
    QObject::connect(&tray, &QSystemTrayIcon::activated, [raise](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
            raise();
    });
    QObject::connect(vault, &VaultController::showAttentionRequested, raise);
    QObject::connect(lockAction, &QAction::triggered, vault, &VaultController::lock);
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    return app.exec();
}
