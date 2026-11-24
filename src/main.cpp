/*
 * SereneDisk Manager — main.cpp
 * Bootstrap: register the backend type, load the shell.
 */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "backend/UDisks2Backend.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("sereneOS"));
    app.setApplicationName(QStringLiteral("SereneDisk Manager"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    QQuickStyle::setStyle(QStringLiteral("Basic")); // fully themeable base

    qmlRegisterType<sdm::UDisks2Backend>("SDM.Backend", 1, 0, "UDisks2Backend");

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("SDM", "Main");

    return app.exec();
}
