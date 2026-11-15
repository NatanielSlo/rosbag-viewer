import QtQuick
import QtQuick.Window
import Proto

// Native Qt Quick shell: owns the replay clock and the HUD, the web view only draws the globe.
Window {
    id: win
    width: 1280; height: 800; visible: true
    title: "Prototype A: osgEarth + cesium-native"
    color: "#14181d"

    // Synthetic flight: a 150 m radius circle at TUM Garching, climbing 20 -> 60 m, 90 s per lap.
    readonly property real lat0: 48.2655
    readonly property real lon0: 11.6710
    readonly property real groundEllipsoid: 527   // ~480 m AMSL + ~47 m geoid offset (L8)
    property real t: 0
    property real fps: 0
    property string status: hasToken ? "Google 3D Tiles via ion" : "No ion token: bare globe"
    property bool ready: true
    property string earthPath: earthFile
    property int logCount: 0

    function pose(time) {
        var a = 2 * Math.PI * time / 90
        var r = 150
        var dLat = r * Math.sin(a) / 111320
        var dLon = r * Math.cos(a) / (111320 * Math.cos(lat0 * Math.PI / 180))
        var alt = groundEllipsoid + 20 + 40 * (0.5 - 0.5 * Math.cos(a / 2))
        var heading = a + Math.PI            // tangent direction, radians
        return { lat: lat0 + dLat, lon: lon0 + dLon, alt: alt, heading: heading }
    }

    OsgEarthItem {
        id: web
        anchors.fill: parent
        earthFile: win.earthPath
        onFpsChanged: { win.fps = fps; if (++win.logCount % 5 === 0) console.log("FPS", fps.toFixed(1), "t", win.t.toFixed(0)) }
    }

    Timer {   // 30 Hz native replay clock pushing the pose into the page
        interval: 33; repeat: true; running: win.ready
        onTriggered: {
            win.t += 0.033 * speed.value
            var p = win.pose(win.t)
            web.setPose(p.lon, p.lat, p.alt, p.heading)
        }
    }

    // Debug aid: PROTO_SCREENSHOT=/path/file.png saves the window after 25 s.
    Timer {
        interval: 25000; running: win.ready && shotPath !== ""
        onTriggered: win.contentItem.grabToImage(function(r) {
            console.log("SCREENSHOT", r.saveToFile(shotPath) ? "saved" : "failed") })
    }

    // Native overlay on top of the web view (checks that Qt Quick items can float over it).
    Rectangle {
        x: 16; y: 16; width: hud.width + 24; height: hud.height + 16
        color: "#cc14181d"; radius: 6
        Column {
            id: hud
            x: 12; y: 8; spacing: 4
            Text { color: "#d8dee6"; font.pixelSize: 14
                   text: "t = " + win.t.toFixed(1) + " s    web fps = " + win.fps.toFixed(0) }
            Text { color: "#9fb0c0"; font.pixelSize: 12; text: win.status }
            Row { spacing: 8
                Text { color: "#d8dee6"; font.pixelSize: 12; text: "speed"; anchors.verticalCenter: parent.verticalCenter }
                Rectangle { id: speed; width: 120; height: 14; color: "#2a3340"; radius: 3
                    property real value: 1 + 9 * handle.x / (width - handle.width)
                    Rectangle { id: handle; width: 14; height: 14; radius: 7; color: "#4aa3ff"; x: 0 }
                    MouseArea { anchors.fill: parent
                        onPositionChanged: (m) => handle.x = Math.max(0, Math.min(speed.width - handle.width, m.x - 7))
                        onPressed: (m) => handle.x = Math.max(0, Math.min(speed.width - handle.width, m.x - 7)) }
                }
            }
        }
    }
}
