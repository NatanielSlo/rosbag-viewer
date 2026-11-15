// Prototype A: osgEarth + cesium-native rendered into a Qt Quick framebuffer item.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    // osgEarth needs OpenGL, so Qt Quick must use its OpenGL backend (not Vulkan/Metal/etc).
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);  // the apt OpenSceneGraph is GL2-style
    fmt.setVersion(4, 5);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    const bool token = !qEnvironmentVariableIsEmpty("OSGEARTH_CESIUMION_KEY");
    engine.rootContext()->setContextProperty(
        "earthFile", QStringLiteral(EARTH_DIR "/") + (token ? "google_ion.earth" : "bare.earth"));
    engine.rootContext()->setContextProperty("hasToken", token);
    engine.rootContext()->setContextProperty("shotPath", qEnvironmentVariable("PROTO_SCREENSHOT"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Proto", "Main");
    return app.exec();
}
