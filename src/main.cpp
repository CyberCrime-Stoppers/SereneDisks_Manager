/*
 * SereneDisk Manager — main.cpp
 * Bootstrap: register the backend type, load the shell.
 */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "backend/UDisks2Backend.h"
#include "backend/Operations.h"
#include "JournalStore.h"

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

    // Append-only operation audit trail. Lives for the whole session;
    // persists across runs under AppDataLocation.
    sdm::JournalStore journalStore;

    // Write-side operations (format / resize / delete / repartition /
    // defragment). Mirrors every attempt + outcome into the journal.
    sdm::StorageOps storageOps;
    storageOps.setJournal(&journalStore);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("devicesBackend"), devicesBackend);
    engine.rootContext()->setContextProperty(
        QStringLiteral("journalStore"), &journalStore);
    engine.rootContext()->setContextProperty(
        QStringLiteral("storageOps"), &storageOps);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("SDM", "Main");

    return app.exec();
}
