#include "app/replaycontroller.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

#include "bag/bagsource.h"
#include "model/telemetry.h"

namespace viewer {

struct ReplayController::Data {
    FlightData flight;
    std::vector<std::string> keys;  // drone namespaces in display order
    QElapsedTimer wall;
};

namespace {
const QString kNoData = QStringLiteral("—");
const double kSpeeds[] = {0.25, 0.5, 1, 2, 4, 8, 16};

QString num(const std::optional<double>& v, int decimals, const QString& unit) {
    return v ? QStringLiteral("%1 %2").arg(*v, 0, 'f', decimals).arg(unit) : kNoData;
}
}  // namespace

ReplayController::ReplayController(QObject* parent) : QObject(parent), d_(new Data) {
    timer_.setInterval(33);  // ~30 Hz
    connect(&timer_, &QTimer::timeout, this, &ReplayController::onTimer);
}
ReplayController::~ReplayController() = default;

QString ReplayController::colorForMode(const QString& mode) {
    static const QHash<QString, QString> fixed = {
        {"MANUAL", "#9aa7b4"},       {"STABILIZED", "#9aa7b4"},   {"ALTCTL", "#7fb2e5"},
        {"POSCTL", "#4aa3ff"},       {"OFFBOARD", "#f5a623"},     {"MISSION", "#3ecf8e"},
        {"AUTO.MISSION", "#3ecf8e"}, {"AUTO_MISSION", "#3ecf8e"}, {"HOLD", "#b48ef0"},
        {"AUTO.LOITER", "#b48ef0"},  {"RTL", "#ff6b6b"},          {"AUTO.RTL", "#ff6b6b"},
        {"LAND", "#e85d9a"},         {"AUTO.LAND", "#e85d9a"},    {"TAKEOFF", "#2fd0d0"},
        {"AUTO.TAKEOFF", "#2fd0d0"}};
    const auto it = fixed.find(mode.toUpper());
    return it == fixed.end() ? QStringLiteral("#c9d1d9") : *it;
}

QString ReplayController::formatClock(double s) {
    s = std::max(0.0, s);
    const int m = int(s) / 60;
    return QStringLiteral("%1:%2").arg(m).arg(s - m * 60, 4, 'f', 1, QLatin1Char('0'));
}

void ReplayController::open(const QString& path) {
    timer_.stop();
    clock_ = ReplayClock();
    loaded_ = false;
    error_.clear();
    try {
        d_->flight = loadFlight(path.toStdString());
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        emit loadedChanged();
        emit droneChanged();
        emit tick();
        return;
    }
    d_->keys.clear();
    droneNames_.clear();
    for (const auto& [ns, tr] : d_->flight.drones) {
        d_->keys.push_back(ns);
        droneNames_ << (ns.empty() ? QStringLiteral("drone") : QString::fromStdString(ns).mid(1));
    }
    if (d_->keys.empty()) {
        error_ = QStringLiteral("No drone position found in this recording.");
    } else {
        loaded_ = true;
    }
    name_ = QFileInfo(path).completeBaseName().isEmpty() ? QFileInfo(path).fileName()
                                                         : QFileInfo(path).completeBaseName();
    droneIndex_ = 0;
    clock_.setRange(0, d_->flight.durationSeconds);
    clock_.seek(0);
    emit loadedChanged();
    emit droneChanged();
    emit playingChanged();
    emit tick();
}

const DroneTrack* ReplayController::track() const {
    if (!loaded_ || droneIndex_ < 0 || droneIndex_ >= int(d_->keys.size())) return nullptr;
    return &d_->flight.drones.at(d_->keys[droneIndex_]);
}

bool ReplayController::hasGps() const {
    const auto* t = track();
    return t && !t->gps().empty();
}

void ReplayController::setDroneIndex(int i) {
    if (!loaded_ || d_->keys.empty()) return;
    const int n = int(d_->keys.size());
    droneIndex_ = ((i % n) + n) % n;
    emit droneChanged();
    emit tick();
}
void ReplayController::nextDrone(int dir) { setDroneIndex(droneIndex_ + dir); }

void ReplayController::onTimer() {
    const qint64 now = d_->wall.elapsed();
    const double dt = (now - lastMs_) / 1000.0;
    lastMs_ = now;
    const bool was = clock_.playing();
    clock_.advance(dt);
    if (was && !clock_.playing()) {
        timer_.stop();
        emit playingChanged();
    }
    emit tick();
}

void ReplayController::togglePlay() {
    if (!loaded_) return;
    clock_.toggle();
    if (clock_.playing()) {
        d_->wall.start();
        lastMs_ = 0;
        timer_.start();
    } else {
        timer_.stop();
    }
    emit playingChanged();
    emit tick();
}

void ReplayController::seek(double s) {
    clock_.seek(s);
    emit tick();
}
void ReplayController::jump(double d) { seek(clock_.position() + d); }
void ReplayController::toStart() { seek(clock_.start()); }
void ReplayController::toEnd() { seek(clock_.end()); }

void ReplayController::setSpeed(double s) {
    clock_.setSpeed(s);
    emit speedChanged();
}
void ReplayController::speedStep(int dir) {
    const double cur = std::abs(clock_.speed());
    int idx = 2;
    for (int i = 0; i < 7; ++i)
        if (std::abs(kSpeeds[i] - cur) < 1e-9) idx = i;
    idx = std::clamp(idx + dir, 0, 6);
    setSpeed(clock_.speed() < 0 ? -kSpeeds[idx] : kSpeeds[idx]);
}

// J / K / L: press again for faster (in the same direction), K pauses.
void ReplayController::shuttle(int dir) {
    if (!loaded_) return;
    if (dir == 0) {
        if (clock_.playing()) togglePlay();
        return;
    }
    const bool sameDir = clock_.playing() && (clock_.speed() > 0) == (dir > 0);
    if (sameDir) {
        speedStep(+1);
    } else {
        setSpeed(dir > 0 ? 1.0 : -1.0);
    }
    if (!clock_.playing())
        togglePlay();
    else
        emit tick();
}

QString ReplayController::timeText() const {
    return QStringLiteral("%1 / %2").arg(formatClock(clock_.position()),
                                         formatClock(clock_.duration()));
}

QString ReplayController::modeName() const {
    const auto* t = track();
    const auto m = t ? t->modeAt(clock_.position()) : std::nullopt;
    return m ? QString::fromStdString(*m) : kNoData;
}
QString ReplayController::modeColor() const {
    const auto m = modeName();
    return m == kNoData ? QStringLiteral("#6b7785") : colorForMode(m);
}

QString ReplayController::armedText() const {
    const auto* t = track();
    if (!t) return kNoData;
    const auto a = telemetryAt(*t, clock_.position()).armed;
    return !a ? kNoData : *a ? QStringLiteral("ARMED") : QStringLiteral("DISARMED");
}

#define TELEM(name, expr)                                        \
    QString ReplayController::name() const {                     \
        const auto* t = track();                                 \
        if (!t) return kNoData;                                  \
        const Telemetry tm = telemetryAt(*t, clock_.position()); \
        return expr;                                             \
    }
TELEM(altitudeText, num(tm.altitude, 1, "m"))
TELEM(groundSpeedText, num(tm.groundSpeed, 1, "m/s"))
TELEM(verticalSpeedText, num(tm.verticalSpeed, 1, "m/s"))
TELEM(distanceText, num(tm.distanceFromHome, 0, "m"))
TELEM(voltageText, num(tm.batteryVoltage, 1, "V"))
TELEM(flightTimeText, tm.flightTime ? formatClock(*tm.flightTime) : kNoData)
TELEM(headingText, tm.headingRad
                       ? QStringLiteral("%1 °").arg(*tm.headingRad * 180.0 / M_PI, 0, 'f', 0)
                       : kNoData)
TELEM(batteryText, tm.batteryFraction
                       ? QStringLiteral("%1 %").arg(*tm.batteryFraction * 100, 0, 'f', 0)
                       : kNoData)
#undef TELEM

QString ReplayController::gpsText() const {
    const auto* t = track();
    if (!t) return kNoData;
    return t->gps().empty() ? QStringLiteral("no GPS") : QStringLiteral("GPS");
}

QVariantList ReplayController::modeBands() const {
    QVariantList out;
    const auto* t = track();
    if (!t) return out;
    const auto& ch = t->modeChanges();
    for (size_t i = 0; i < ch.size(); ++i) {
        const double start = ch[i].t;
        const double end = i + 1 < ch.size() ? ch[i + 1].t : clock_.end();
        const QString mode = QString::fromStdString(ch[i].mode);
        out.push_back(QVariantMap{
            {"start", start}, {"end", end}, {"mode", mode}, {"color", colorForMode(mode)}});
    }
    return out;
}

QString ReplayController::pathScript() const {
    const auto* t = track();
    QJsonArray segments;
    if (t && !t->gps().empty()) {
        QJsonObject cur;
        QJsonArray pos, times;
        QString curMode;
        auto flush = [&]() {
            if (pos.isEmpty()) return;
            cur["mode"] = curMode;
            cur["color"] = colorForMode(curMode);
            cur["pos"] = pos;
            cur["t"] = times;
            segments.push_back(cur);
            cur = QJsonObject();
            pos = QJsonArray();
            times = QJsonArray();
        };
        for (const auto& g : t->gps()) {
            const auto m = t->modeAt(g.t);
            const QString mode = m ? QString::fromStdString(*m) : QStringLiteral("?");
            if (mode != curMode) {
                // Close the previous segment on this point so the coloured pieces touch.
                if (!pos.isEmpty()) {
                    pos.push_back(g.position.lonDeg);
                    pos.push_back(g.position.latDeg);
                    pos.push_back(g.position.heightM);
                    times.push_back(g.t);
                }
                flush();
                curMode = mode;
            }
            pos.push_back(g.position.lonDeg);
            pos.push_back(g.position.latDeg);
            pos.push_back(g.position.heightM);
            times.push_back(g.t);
        }
        flush();
    }
    return QStringLiteral("setPath(%1)")
        .arg(QString::fromUtf8(QJsonDocument(segments).toJson(QJsonDocument::Compact)));
}

QString ReplayController::poseScript() const {
    const auto* t = track();
    if (!t) return {};
    const double now = clock_.position();
    const auto g = t->geoPointAt(now);
    if (!g) return QStringLiteral("setDrone(null)");
    const Telemetry tm = telemetryAt(*t, now);
    return QStringLiteral("setDrone([%1,%2,%3,%4])")
        .arg(g->lonDeg, 0, 'f', 8)
        .arg(g->latDeg, 0, 'f', 8)
        .arg(g->heightM, 0, 'f', 2)
        .arg(tm.headingRad.value_or(0.0), 0, 'f', 4);
}

}  // namespace viewer
