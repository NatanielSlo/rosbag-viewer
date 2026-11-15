#include "osgearthitem.h"

#include <QElapsedTimer>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>
#include <QWheelEvent>

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <osg/GLExtensions>
#include <osg/Geometry>
#include <osg/LineWidth>
#include <osg/MatrixTransform>
#include <osg/ShapeDrawable>
#include <osgDB/ReadFile>
#include <osgGA/GUIEventAdapter>
#include <osgViewer/Viewer>

#include <osgEarth/AutoClipPlaneHandler>
#include <osgEarth/EarthManipulator>
#include <osgEarth/GeoData>
#include <osgEarth/MapNode>
#include <osgEarth/Registry>
#include <osgEarth/ShaderGenerator>
#include <osgEarth/Utils>
#include <osgEarthCesium/CesiumCreditsNode>
#include <osgEarthCesium/CesiumIon>
#include <osgEarthCesium/Context>

OsgEarthItem::OsgEarthItem(QQuickItem* parent) : QQuickFramebufferObject(parent) {
    setAcceptedMouseButtons(Qt::AllButtons);
    setMirrorVertically(true);  // OpenGL framebuffers are bottom-up
}

void OsgEarthItem::setPose(double lon, double lat, double alt, double heading) {
    QMutexLocker l(&mutex_);
    pose_ = {lon, lat, alt, heading, true};
}
OsgEarthItem::Pose OsgEarthItem::pose() const {
    QMutexLocker l(&mutex_);
    return pose_;
}
std::vector<OsgEarthItem::InputEvent> OsgEarthItem::takeEvents() {
    QMutexLocker l(&mutex_);
    return std::exchange(events_, {});
}
void OsgEarthItem::mousePressEvent(QMouseEvent* e) {
    QMutexLocker l(&mutex_);
    events_.push_back({0, e->position().x(), e->position().y(), int(e->button()), 0});
    e->accept();
}
void OsgEarthItem::mouseMoveEvent(QMouseEvent* e) {
    QMutexLocker l(&mutex_);
    events_.push_back({1, e->position().x(), e->position().y(), int(e->buttons()), 0});
    e->accept();
}
void OsgEarthItem::mouseReleaseEvent(QMouseEvent* e) {
    QMutexLocker l(&mutex_);
    events_.push_back({2, e->position().x(), e->position().y(), int(e->button()), 0});
    e->accept();
}
void OsgEarthItem::wheelEvent(QWheelEvent* e) {
    QMutexLocker l(&mutex_);
    events_.push_back({3, e->position().x(), e->position().y(), 0, e->angleDelta().y()});
    e->accept();
}

namespace {

class Renderer : public QQuickFramebufferObject::Renderer {
public:
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat f;
        f.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        size_ = size;
        if (gw_) {
            gw_->resized(0, 0, size.width(), size.height());
            viewer_->getCamera()->setViewport(0, 0, size.width(), size.height());
        }
        return new QOpenGLFramebufferObject(size, f);
    }

    void synchronize(QQuickFramebufferObject* item) override {
        item_ = static_cast<OsgEarthItem*>(item);
        pose_ = item_->pose();
        events_ = item_->takeEvents();
        if (earthFile_.isEmpty()) earthFile_ = item_->earthFile();
    }

    void render() override {
        if (!viewer_) init();
        gw_->setDefaultFboId(framebufferObject()->handle());
        applyPose();
        auto* q = gw_->getEventQueue();
        for (const auto& e : events_) {
            float x = float(e.x), y = float(size_.height() - e.y);  // OSG: origin at the bottom
            auto btn = [](int b) {
                return b == Qt::LeftButton ? 1 : b == Qt::MiddleButton ? 2 : 3;
            };
            switch (e.type) {
                case 0:
                    q->mouseButtonPress(x, y, btn(e.buttons));
                    break;
                case 1:
                    q->mouseMotion(x, y);
                    break;
                case 2:
                    q->mouseButtonRelease(x, y, btn(e.buttons));
                    break;
                case 3:
                    q->mouseScroll(e.delta > 0 ? osgGA::GUIEventAdapter::SCROLL_UP
                                               : osgGA::GUIEventAdapter::SCROLL_DOWN);
                    break;
            }
        }
        events_.clear();
        viewer_->getCamera()->getGraphicsContext()->getState()->reset();
        viewer_->frame();
        QQuickOpenGLUtils::resetOpenGLState();

        if (++frames_ == 1) clock_.start();
        if (clock_.elapsed() >= 1000) {
            QMetaObject::invokeMethod(item_, "setFps", Qt::QueuedConnection,
                                      Q_ARG(double, frames_ * 1000.0 / clock_.elapsed()));
            frames_ = 0;
            clock_.restart();
        }
        update();  // render continuously
    }

private:
    void init() {
        // osgEarth (and OSG plugins) may create a temporary offscreen context to query the GPU and
        // then release it, which leaves NO context current behind Qt's back. Remember Qt's
        // context and bind it again once osgEarth is set up.
        QOpenGLContext* qtContext = QOpenGLContext::currentContext();
        QSurface* qtSurface = qtContext->surface();
        osgEarth::initialize();
        viewer_ = new osgViewer::Viewer;
        viewer_->setThreadingModel(osgViewer::ViewerBase::SingleThreaded);
        gw_ = viewer_->setUpViewerAsEmbeddedInWindow(0, 0, size_.width(), size_.height());
        // osgEarth's shaders need OSG's GL3-style state (what GL3RealizeOperation sets up).
        if (auto* st = gw_->getState()) {
            st->setUseModelViewAndProjectionUniforms(true);
            st->setUseVertexAttributeAliasing(true);
        }

        osg::ref_ptr<osg::Node> earth = osgDB::readRefNodeFile(earthFile_.toStdString());
        root_ = new osg::Group;
        if (earth) root_->addChild(earth);
        mapNode_ = osgEarth::MapNode::get(earth.get());

        auto* manip = new osgEarth::Util::EarthManipulator;
        viewer_->setCameraManipulator(manip);
        if (mapNode_)
            viewer_->getCamera()->addCullCallback(
                new osgEarth::Util::AutoClipPlaneCullCallback(mapNode_));
        viewer_->getCamera()->setClearColor(osg::Vec4(0.08f, 0.09f, 0.11f, 1.f));
        viewer_->getCamera()->setViewport(0, 0, size_.width(), size_.height());
        viewer_->getCamera()->setProjectionMatrixAsPerspective(
            30.0, double(size_.width()) / size_.height(), 1.0, 1000.0);

        buildDrone();
        root_->addChild(drone_);
        root_->addChild(trailXform_);

        // Mandatory attribution for Google tiles. The node takes ownership of the raw pointer
        // (it wraps it in a second shared_ptr), so we must never let it be destroyed:
        // we leak one reference on purpose to avoid a double free at exit.
        auto ctx = osgEarth::Cesium::CesiumIon::instance().getContext("");
        qWarning() << "DIAG credits: context" << (void*)ctx << "creditSystem"
                   << (ctx ? (void*)ctx->creditSystem.get() : nullptr);
        if (ctx && ctx->creditSystem) {
            auto* credits =
                new osgEarth::Cesium::CesiumCreditsNode(viewer_.get(), ctx->creditSystem.get());
            credits->ref();
            root_->addChild(credits);
        }
        viewer_->setSceneData(root_);
        viewer_->realize();
        if (qEnvironmentVariableIsSet("PROTO_DIAG")) diagnose();  // before re-binding
        qtContext->doneCurrent();
        qtContext->makeCurrent(qtSurface);
        if (qEnvironmentVariableIsSet("PROTO_DIAG")) diagnose();  // after re-binding
        if (mapNode_) {                                           // initial viewpoint
            manip->setViewpoint(
                osgEarth::Viewpoint("Garching", 11.6710, 48.2615, 520.0, 0.0, -35.0, 700.0), 0.0);
        }
    }

    // Debug: does the current context compile a trivial shader, and what does OSG think of it?
    void diagnose() {
        QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
        const auto fmt = QOpenGLContext::currentContext()->format();
        qWarning() << "DIAG context" << fmt.majorVersion() << fmt.minorVersion() << "profile"
                   << int(fmt.profile()) << "GL_VERSION" << (const char*)f->glGetString(GL_VERSION);
        const char* src = "#version 330 core\nvoid main(){ gl_Position = vec4(0.0); }\n";
        GLuint sh = f->glCreateShader(GL_VERTEX_SHADER);
        f->glShaderSource(sh, 1, &src, nullptr);
        f->glCompileShader(sh);
        GLint ok = 0;
        f->glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        char log[512] = {0};
        f->glGetShaderInfoLog(sh, 511, nullptr, log);
        qWarning() << "DIAG raw Qt compile ok=" << ok << "log=" << log << "glError"
                   << f->glGetError();
        f->glDeleteShader(sh);
        auto* ext = osg::GLExtensions::Get(gw_->getState()->getContextID(), true);
        qWarning() << "DIAG OSG ctx" << gw_->getState()->getContextID() << "glCompileShader ptr"
                   << (void*)ext->glCompileShader << "glGetShaderiv" << (void*)ext->glGetShaderiv
                   << "glGetShaderInfoLog" << (void*)ext->glGetShaderInfoLog << "glsl"
                   << ext->glslLanguageVersion << "glVersion" << ext->glVersion << "aliasing"
                   << gw_->getState()->getUseVertexAttributeAliasing();
    }

    void buildDrone() {
        drone_ = new osg::MatrixTransform;
        auto* box = new osg::ShapeDrawable(new osg::Box(osg::Vec3(), 4.f, 6.f, 1.f));
        box->setColor(osg::Vec4(1.f, 0.55f, 0.f, 1.f));
        auto* nose = new osg::ShapeDrawable(new osg::Box(osg::Vec3(0, 3.5f, 0), 1.f, 2.f, 1.f));
        nose->setColor(osg::Vec4(1.f, 1.f, 1.f, 1.f));
        auto* geode = new osg::Geode;
        geode->addDrawable(box);
        geode->addDrawable(nose);
        geode->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        drone_->addChild(geode);

        trailXform_ = new osg::MatrixTransform;
        trailGeom_ = new osg::Geometry;
        trailPts_ = new osg::Vec3Array;
        trailGeom_->setVertexArray(trailPts_);
        trailGeom_->setUseDisplayList(false);
        trailGeom_->setUseVertexBufferObjects(true);
        trailGeom_->setColorArray(new osg::Vec4Array(1, new osg::Vec4(0.f, 1.f, 1.f, 1.f)),
                                  osg::Array::BIND_OVERALL);
        trailPrim_ = new osg::DrawArrays(GL_LINE_STRIP, 0, 0);
        trailGeom_->addPrimitiveSet(trailPrim_);
        auto* tg = new osg::Geode;
        tg->addDrawable(trailGeom_);
        tg->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        tg->getOrCreateStateSet()->setAttribute(new osg::LineWidth(3.f));
        trailXform_->addChild(tg);
        osgEarth::Registry::shaderGenerator().run(drone_.get());
        osgEarth::Registry::shaderGenerator().run(trailXform_.get());
    }

    void applyPose() {
        if (!pose_.valid) return;
        auto srs = osgEarth::SpatialReference::get("wgs84");
        osgEarth::GeoPoint gp(srs, pose_.lon, pose_.lat, pose_.alt, osgEarth::ALTMODE_ABSOLUTE);
        osg::Matrixd l2w;
        gp.createLocalToWorld(l2w);
        drone_->setMatrix(osg::Matrixd::rotate(-pose_.heading, osg::Vec3d(0, 0, 1)) * l2w);

        if (!trailOrigin_) {  // trail vertices are metres in the ENU frame of the first point
            trailL2W_ = l2w;
            trailW2L_ = osg::Matrixd::inverse(l2w);
            trailXform_->setMatrix(l2w);
            trailOrigin_ = true;
        }
        osg::Vec3d local = osg::Vec3d(0, 0, 0) * l2w * trailW2L_;
        if (trailPts_->empty() || (local - osg::Vec3d(trailPts_->back())).length() > 3.0) {
            trailPts_->push_back(osg::Vec3(local));
            trailPrim_->setCount(trailPts_->size());
            trailPts_->dirty();
            trailGeom_->dirtyBound();
        }
    }

    QSize size_{1280, 800};
    OsgEarthItem* item_ = nullptr;
    OsgEarthItem::Pose pose_;
    std::vector<OsgEarthItem::InputEvent> events_;
    QString earthFile_;
    osg::ref_ptr<osgViewer::Viewer> viewer_;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> gw_;
    osg::ref_ptr<osg::Group> root_;
    osg::ref_ptr<osg::MatrixTransform> drone_, trailXform_;
    osg::ref_ptr<osg::Geometry> trailGeom_;
    osg::ref_ptr<osg::Vec3Array> trailPts_;
    osg::ref_ptr<osg::DrawArrays> trailPrim_;
    osg::ref_ptr<osgEarth::MapNode> mapNode_;
    osg::Matrixd trailL2W_, trailW2L_;
    bool trailOrigin_ = false;
    int frames_ = 0;
    QElapsedTimer clock_;
};

}  // namespace

QQuickFramebufferObject::Renderer* OsgEarthItem::createRenderer() const { return new ::Renderer; }
