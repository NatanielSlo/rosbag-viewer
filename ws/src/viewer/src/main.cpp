#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTimer>

#include "appinfo.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(viewer::applicationName());
    QGuiApplication::setApplicationVersion(viewer::applicationVersion());
    QGuiApplication::setOrganizationName(QStringLiteral("HORYZN"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption smoke(QStringLiteral("smoke-test"),
                             QStringLiteral("Load the UI, then exit (0 = success). Used by CI."));
    parser.addOption(smoke);
    parser.process(app);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Viewer", "Main");

    if (parser.isSet(smoke)) {
        QTimer::singleShot(500, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
