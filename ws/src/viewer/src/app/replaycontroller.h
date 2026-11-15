#pragma once
// Glue between the flight model and the UI: owns the loaded recording and the replay clock,
// and exposes everything the QML needs at the current time. All numbers are formatted here so
// that "no data" is always shown as "—" and units are never forgotten.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <memory>

#include "model/flightmodel.h"
#include "model/replayclock.h"

namespace viewer {

class ReplayController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString error READ error NOTIFY loadedChanged)
    Q_PROPERTY(QString recordingName READ recordingName NOTIFY loadedChanged)
    Q_PROPERTY(QStringList drones READ drones NOTIFY loadedChanged)
    Q_PROPERTY(int droneIndex READ droneIndex WRITE setDroneIndex NOTIFY droneChanged)
    Q_PROPERTY(bool hasGps READ hasGps NOTIFY droneChanged)
    Q_PROPERTY(double duration READ duration NOTIFY loadedChanged)
    Q_PROPERTY(double position READ position NOTIFY tick)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(QString timeText READ timeText NOTIFY tick)
    Q_PROPERTY(QString modeName READ modeName NOTIFY tick)
    Q_PROPERTY(QString modeColor READ modeColor NOTIFY tick)
    Q_PROPERTY(QString armedText READ armedText NOTIFY tick)
    Q_PROPERTY(QString altitudeText READ altitudeText NOTIFY tick)
    Q_PROPERTY(QString groundSpeedText READ groundSpeedText NOTIFY tick)
    Q_PROPERTY(QString verticalSpeedText READ verticalSpeedText NOTIFY tick)
    Q_PROPERTY(QString distanceText READ distanceText NOTIFY tick)
    Q_PROPERTY(QString headingText READ headingText NOTIFY tick)
    Q_PROPERTY(QString batteryText READ batteryText NOTIFY tick)
    Q_PROPERTY(QString voltageText READ voltageText NOTIFY tick)
    Q_PROPERTY(QString flightTimeText READ flightTimeText NOTIFY tick)
    Q_PROPERTY(QString gpsText READ gpsText NOTIFY droneChanged)
    Q_PROPERTY(QVariantList modeBands READ modeBands NOTIFY droneChanged)

public:
    explicit ReplayController(QObject* parent = nullptr);
    ~ReplayController() override;

    Q_INVOKABLE void open(const QString& path);
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void jump(double deltaSeconds);  // relative seek
    Q_INVOKABLE void toStart();
    Q_INVOKABLE void toEnd();
    Q_INVOKABLE void setSpeed(double s);
    Q_INVOKABLE void speedStep(int direction);  // +1 faster, -1 slower (0.25x .. 16x)
    Q_INVOKABLE void shuttle(int direction);    // J (-1) / K (0) / L (+1)
    Q_INVOKABLE void nextDrone(int direction);

    // JavaScript for the map page: the whole path coloured by mode, and the pose now.
    Q_INVOKABLE QString pathScript() const;
    Q_INVOKABLE QString poseScript() const;

    bool loaded() const { return loaded_; }
    QString error() const { return error_; }
    QString recordingName() const { return name_; }
    QStringList drones() const { return droneNames_; }
    int droneIndex() const { return droneIndex_; }
    void setDroneIndex(int i);
    bool hasGps() const;
    double duration() const { return clock_.duration(); }
    double position() const { return clock_.position(); }
    bool playing() const { return clock_.playing(); }
    double speed() const { return clock_.speed(); }

    QString timeText() const;
    QString modeName() const;
    QString modeColor() const;
    QString armedText() const;
    QString altitudeText() const;
    QString groundSpeedText() const;
    QString verticalSpeedText() const;
    QString distanceText() const;
    QString headingText() const;
    QString batteryText() const;
    QString voltageText() const;
    QString flightTimeText() const;
    QString gpsText() const;
    QVariantList modeBands() const;

    // One colour per flight mode, used by the path, the timeline and the chips (UI rule).
    static QString colorForMode(const QString& mode);
    static QString formatClock(double seconds);  // m:ss.s

signals:
    void loadedChanged();
    void droneChanged();
    void playingChanged();
    void speedChanged();
    void tick();  // position moved (or anything derived from it)

private:
    const DroneTrack* track() const;
    void onTimer();

    struct Data;
    std::unique_ptr<Data> d_;
    ReplayClock clock_;
    QTimer timer_;
    qint64 lastMs_ = 0;
    bool loaded_ = false;
    QString error_, name_;
    QStringList droneNames_;
    int droneIndex_ = 0;
};

}  // namespace viewer
