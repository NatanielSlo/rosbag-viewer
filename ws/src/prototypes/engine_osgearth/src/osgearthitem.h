#pragma once
// Qt Quick item that renders an osgEarth scene into a framebuffer (prototype A).
#include <QtQml/qqmlregistration.h>
#include <QMutex>
#include <QQuickFramebufferObject>
#include <vector>

class OsgEarthItem : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(QString earthFile READ earthFile WRITE setEarthFile)
public:
    struct Pose {
        double lon = 0, lat = 0, alt = 0, heading = 0;
        bool valid = false;
    };
    struct InputEvent {
        int type;
        double x, y;
        int buttons;
        int delta;
    };  // type: 0 press 1 move 2 release 3 wheel

    explicit OsgEarthItem(QQuickItem* parent = nullptr);
    Renderer* createRenderer() const override;

    Q_INVOKABLE void setPose(double lon, double lat, double alt, double heading);
    double fps() const { return fps_; }
    QString earthFile() const { return earthFile_; }
    void setEarthFile(const QString& f) { earthFile_ = f; }

    // called by the renderer while the GUI thread is blocked
    Pose pose() const;
    std::vector<InputEvent> takeEvents();

public slots:
    void setFps(double f) {
        fps_ = f;
        emit fpsChanged();
    }
signals:
    void fpsChanged();

protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    mutable QMutex mutex_;
    Pose pose_;
    std::vector<InputEvent> events_;
    double fps_ = 0;
    QString earthFile_;
};
