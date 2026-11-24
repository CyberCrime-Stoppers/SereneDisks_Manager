/*
 * SereneDisk Manager — main.cpp
 * Bootstrap: register the backend type, load the shell.
 */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "backend/UDisks2Backend.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("sereneOS"));
    app.setApplicationName(QStringLiteral("SereneDisk Manager"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    QQuickStyle::setStyle(QStringLiteral("Basic")); // fully themeable base

    // One shared backend instance, exposed to QML as a context property
    // so every page talks to the same device list and journal stream.
    sdm::UDisks2Backend backend;
    sdm::UDisks2Backend *devicesBackend = &backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("devicesBackend"), devicesBackend);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("SDM", "Main");

    return app.exec();
}
