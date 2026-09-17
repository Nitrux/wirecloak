// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <QSurfaceFormat>
#include <QQuickWindow>
#include <QIcon>
#include <QDate>

#include <KLocalizedString>
#include <KLocalizedContext>
#include <KAboutData>
#include <MauiKit4/Core/mauiapp.h>
#include "vpnbackend.h"

int main(int argc, char *argv[])
{
    // 1. ENABLE WINDOW TRANSPARENCY
    QSurfaceFormat format;
    format.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);

    QStringList paths = QIcon::themeSearchPaths();
    paths.append(QStringLiteral(":/icons"));
    QIcon::setThemeSearchPaths(paths);

    // 2. SETUP ORGANIZATION
    app.setOrganizationName(QStringLiteral("Nitrux"));
    app.setApplicationName(QStringLiteral("Wirecloak"));
    
    // 3. SETUP WINDOW ICON
    QIcon appIcon = QIcon::fromTheme(QStringLiteral("preferences-system-network-proxy"), QIcon(QStringLiteral(":/assets/wirecloak.svg")));
    app.setWindowIcon(appIcon);

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("wirecloak"));

    // 5. SETUP ABOUT DATA
    KAboutData about(QStringLiteral("wirecloak"),
                     i18n("Wirecloak"),
                     QStringLiteral("0.0.1"),
                     i18n("Manage WireGuard VPN configurations."),
                     KAboutLicense::BSD_3_Clause,
                      // "Maui" must be present for the footer logo to appear
                     i18n("© %1 Made by Nitrux | Built with MauiKit", QString::number(QDate::currentDate().year())),
                     QString::fromUtf8(GIT_BRANCH) + QStringLiteral("/") + QString::fromUtf8(GIT_COMMIT_HASH));

    about.addAuthor(QStringLiteral("Uri Herrera"), i18n("Developer"), QStringLiteral("uri_herrera@nxos.org"));
    about.setHomepage(QStringLiteral("https://nxos.org"));
    about.setProductName(QByteArrayLiteral("nitrux/wirecloak"));
    about.setOrganizationDomain(QByteArrayLiteral("nxos.org"));
    about.setDesktopFileName(QStringLiteral("org.nxos.wirecloak"));
    
    // Set the logo for the About Dialog header
    about.setProgramLogo(app.windowIcon());

    KAboutData::setApplicationData(about);

    // 6. INITIALIZE MAUIKIT
    // Initializes the singleton and theming
    MauiApp::instance()->setIconName(QStringLiteral("qrc:/assets/wirecloak.svg")); 

    qmlRegisterType<VpnBackend>("org.nitrux.vpn", 1, 0, "VpnBackend");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));

    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
