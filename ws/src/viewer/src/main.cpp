#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMimeDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWebEngineProfile>
#include <QTimer>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QtWebEngineQuick>

#include "app/replaycontroller.h"
#include "appinfo.h"

// Serves the map page and CesiumJS over app:// so web workers work without a web server.
//   app://demo/<file>   -> <web root>/web/<file>
//   app://cesium/<file> -> <web root>/third_party/cesium/<file>
class AppSchemeHandler : public QWebEngineUrlSchemeHandler {
public:
    explicit AppSchemeHandler(QString root, QObject* parent = nullptr)
        : QWebEngineUrlSchemeHandler(parent), root_(std::move(root)) {}

    void requestStarted(QWebEngineUrlRequestJob* job) override {
        const QUrl url = job->requestUrl();
        const QString rel = QDir::cleanPath(url.path());
        if (rel.contains(QStringLiteral(".."))) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        const QString base =
            url.host() == QStringLiteral("cesium") ? root_ + "/third_party/cesium" : root_ + "/web";
        auto* file = new QFile(base + rel, job);
        if (!file->open(QIODevice::ReadOnly)) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        const QByteArray mime = QMimeDatabase().mimeTypeForFile(file->fileName()).name().toUtf8();
        job->reply(mime == "application/x-extension-js" ? "text/javascript" : mime, file);
    }

private:
    QString root_;
};

int main(int argc, char* argv[]) {
    QWebEngineUrlScheme scheme("app");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalAccessAllowed |
                    QWebEngineUrlScheme::CorsEnabled | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");  // lets the dark palette apply
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(viewer::applicationName());
    QGuiApplication::setApplicationVersion(viewer::applicationVersion());
    QGuiApplication::setOrganizationName(QStringLiteral("HORYZN"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("recording"),
                                 QStringLiteral("rosbag2 directory to open (optional)."));
    QCommandLineOption smoke(QStringLiteral("smoke-test"),
                             QStringLiteral("Load the UI, then exit (0 = success). Used by CI."));
    parser.addOption(smoke);
    parser.process(app);

    AppSchemeHandler handler{QStringLiteral(WEB_ROOT)};
    QQuickWebEngineProfile::defaultProfile()->installUrlSchemeHandler("app", &handler);

    viewer::ReplayController replay;
    QQmlApplicationEngine engine;
    // The token comes from the environment; it is never written to logs or files.
    engine.rootContext()->setContextProperty("ionToken", qEnvironmentVariable("CESIUM_ION_TOKEN"));
    engine.rootContext()->setContextProperty("shotPath", qEnvironmentVariable("VIEWER_SCREENSHOT"));
    engine.rootContext()->setContextProperty("replay", &replay);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Viewer", "Main");

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) replay.open(args.first());

    if (parser.isSet(smoke)) {
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
