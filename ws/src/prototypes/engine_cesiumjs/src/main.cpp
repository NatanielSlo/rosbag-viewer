// Prototype, option B: CesiumJS inside Qt WebEngine, driven from native code.
// Pages are served through our own app:// scheme, so Cesium's web workers work without a
// web server, and the same mechanism would serve bundled files in the real viewer.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMimeDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QtWebEngineQuick>
#include <QQuickWebEngineProfile>

class AppSchemeHandler : public QWebEngineUrlSchemeHandler {
public:
    explicit AppSchemeHandler(QString root, QObject* parent = nullptr)
        : QWebEngineUrlSchemeHandler(parent), root_(std::move(root)) {}

    void requestStarted(QWebEngineUrlRequestJob* job) override {
        // app://demo/<path>  ->  <WEB_ROOT>/web/<path>, app://cesium/<path> -> third_party/cesium
        const QUrl url = job->requestUrl();
        const QString host = url.host();
        const QString rel = QDir::cleanPath(url.path());
        if (rel.contains(QStringLiteral(".."))) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        QString base = host == QStringLiteral("cesium") ? root_ + "/third_party/cesium"
                                                        : root_ + "/web";
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

    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    AppSchemeHandler handler{QStringLiteral(WEB_ROOT)};
    QQuickWebEngineProfile::defaultProfile()->installUrlSchemeHandler("app", &handler);

    QQmlApplicationEngine engine;
    // The token comes from the environment; it is never written to logs or files.
    engine.rootContext()->setContextProperty("ionToken",
                                             qEnvironmentVariable("CESIUM_ION_TOKEN"));
    engine.rootContext()->setContextProperty("shotPath", qEnvironmentVariable("PROTO_SCREENSHOT"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Proto", "Main");
    return app.exec();
}
