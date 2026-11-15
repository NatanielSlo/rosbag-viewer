import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtWebEngine

Window {
    id: win
    width: 1280; height: 800; visible: true
    title: replay.loaded ? replay.recordingName + " — " + Qt.application.name : Qt.application.name
    color: "#14181d"
    palette {   // dark palette for the Fusion style (set in main.cpp)
        window: "#1a2028"; windowText: "#d8dee6"; base: "#10151a"; text: "#d8dee6"
        button: "#2a3340"; buttonText: "#d8dee6"; highlight: "#4aa3ff"; highlightedText: "#ffffff"
        toolTipBase: "#14181d"; toolTipText: "#d8dee6"
    }

    readonly property color fg: "#d8dee6"
    readonly property color dim: "#8d9bab"
    readonly property color panel: "#e61a2028"
    readonly property string mono: "monospace"
    property bool mapReady: false
    property bool showHelp: false
    property string webStatus: ""
    property real webFps: 0
    property string cameraMode: "free"

    function pushPath() { if (mapReady) web.runJavaScript(replay.pathScript()) }
    function pushPose() { if (mapReady) { var s = replay.poseScript(); if (s) web.runJavaScript(s) } }
    function setCamera(m) { web.runJavaScript("camera('" + m + "')") }
    function view(a) { web.runJavaScript("view('" + a + "')") }

    Connections {
        target: replay
        function onTick() { win.pushPose() }
        function onDroneChanged() { win.pushPath(); win.pushPose() }
    }
    onMapReadyChanged: if (mapReady) { pushPath(); pushPose() }

    FolderDialog {
        id: openDialog
        title: "Open a recording (rosbag2 folder)"
        onAccepted: replay.open(selectedFolder.toString().replace("file://", ""))
    }

    // ---- shortcuts (every one also has a visible control) ----
    Shortcut { sequence: "Space"; onActivated: replay.togglePlay() }
    Shortcut { sequence: "J"; onActivated: replay.shuttle(-1) }
    Shortcut { sequence: "K"; onActivated: replay.shuttle(0) }
    Shortcut { sequence: "L"; onActivated: replay.shuttle(1) }
    Shortcut { sequence: "Shift+,"; onActivated: replay.jump(-10) }
    Shortcut { sequence: "Shift+."; onActivated: replay.jump(10) }
    Shortcut { sequences: ["<"]; onActivated: replay.jump(-10) }
    Shortcut { sequences: [">"]; onActivated: replay.jump(10) }
    Shortcut { sequence: "Home"; onActivated: replay.toStart() }
    Shortcut { sequence: "End"; onActivated: replay.toEnd() }
    Shortcut { sequences: ["+", "="]; onActivated: replay.speedStep(1) }
    Shortcut { sequence: "-"; onActivated: replay.speedStep(-1) }
    Shortcut { sequences: ["Left", "A"]; onActivated: win.view("left") }
    Shortcut { sequences: ["Right", "D"]; onActivated: win.view("right") }
    Shortcut { sequences: ["Up", "W"]; onActivated: win.view("up") }
    Shortcut { sequences: ["Down", "S"]; onActivated: win.view("down") }
    Shortcut { sequence: "Q"; onActivated: win.view("rotLeft") }
    Shortcut { sequence: "E"; onActivated: win.view("rotRight") }
    Shortcut { sequence: "PgUp"; onActivated: win.view("in") }
    Shortcut { sequence: "PgDown"; onActivated: win.view("out") }
    Shortcut { sequence: "F"; onActivated: win.setCamera("follow") }
    Shortcut { sequence: "T"; onActivated: win.setCamera("top") }
    Shortcut { sequence: "R"; onActivated: win.setCamera("reset") }
    Shortcut { sequence: "Z"; onActivated: win.setCamera("zoom") }
    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Tab"; onActivated: replay.nextDrone(1) }
    Shortcut { sequence: "Shift+Tab"; onActivated: replay.nextDrone(-1) }
    Shortcut { sequences: ["?", "Shift+/", "F1"]; onActivated: win.showHelp = !win.showHelp }
    Shortcut { sequence: "Esc"; onActivated: win.showHelp = false }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ================= toolbar =================
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 44
            color: "#1a2028"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 10
                ToolButton { text: "Open…"; onClicked: openDialog.open(); ToolTip.visible: hovered; ToolTip.text: "Open a recording (Ctrl+O)" }
                Label { text: replay.loaded ? replay.recordingName : "No recording open"; color: win.fg; font.bold: true }
                ComboBox {
                    visible: replay.drones.length > 1
                    model: replay.drones
                    currentIndex: replay.droneIndex
                    onActivated: (i) => replay.droneIndex = i
                    ToolTip.visible: hovered; ToolTip.text: "Drone (Tab / Shift+Tab)"
                }
                Chip { text: replay.armedText; dot: replay.armedText === "ARMED" ? "#3ecf8e" : "#6b7785"; visible: replay.loaded }
                Chip { text: replay.modeName; dot: replay.modeColor; visible: replay.loaded }
                Chip { text: replay.gpsText; dot: replay.hasGps ? "#3ecf8e" : "#6b7785"; visible: replay.loaded }
                Chip { text: "battery " + replay.batteryText; dot: "#6b7785"; visible: replay.loaded }
                Item { Layout.fillWidth: true }
                ToolButton { text: "?"; onClicked: win.showHelp = !win.showHelp; ToolTip.visible: hovered; ToolTip.text: "Keyboard shortcuts (?)" }
            }
        }

        // ================= main view =================
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true

            WebEngineView {
                id: web
                anchors.fill: parent
                url: "app://demo/index.html"
                settings.webGLEnabled: true
                onLoadingChanged: function(req) {
                    if (req.status === WebEngineView.LoadSucceededStatus)
                        runJavaScript("init(" + JSON.stringify(ionToken) + ")")
                }
                onJavaScriptConsoleMessage: function(level, message) {
                    if (message === "READY") win.mapReady = true
                    else if (message.indexOf("SEEK ") === 0) replay.seek(parseFloat(message.substring(5)))
                    else if (message.indexOf("CAMERA ") === 0) win.cameraMode = message.substring(7)
                    else if (message.indexOf("FPS ") === 0) win.webFps = parseFloat(message.substring(4))
                    else if (message.indexOf("STATUS ") === 0) win.webStatus = message.substring(7)
                    else console.log("[web]", message)
                }
            }

            // No GPS: the local 3D scene is not built yet, say so instead of an empty globe.
            Rectangle {
                anchors.centerIn: parent; visible: replay.loaded && !replay.hasGps
                color: win.panel; radius: 8; width: noGps.width + 40; height: noGps.height + 30
                Text { id: noGps; anchors.centerIn: parent; color: win.fg; font.pixelSize: 15
                       text: "This recording has no GPS position.\nThe local 3D scene (indoor flights) is not built yet." }
            }
            Rectangle {
                anchors.centerIn: parent; visible: !replay.loaded
                color: win.panel; radius: 8; width: hint.width + 40; height: hint.height + 30
                Text { id: hint; anchors.centerIn: parent; color: win.fg; font.pixelSize: 15
                       text: replay.error !== "" ? replay.error : "Open a recording with Ctrl+O" }
            }

            // Telemetry (top right)
            Rectangle {
                visible: replay.loaded
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 12
                width: 250; height: tel.height + 20; color: win.panel; radius: 8
                GridLayout {
                    id: tel
                    x: 14; y: 10; columns: 2; columnSpacing: 12; rowSpacing: 3
                    width: parent.width - 28
                    Repeater {
                        model: [
                            ["Altitude", replay.altitudeText], ["Ground speed", replay.groundSpeedText],
                            ["Vertical speed", replay.verticalSpeedText], ["From home", replay.distanceText],
                            ["Heading", replay.headingText], ["Battery", replay.batteryText + "  " + replay.voltageText],
                            ["Flight time", replay.flightTimeText]]
                        delegate: RowLayout {
                            Layout.columnSpan: 2; Layout.fillWidth: true
                            Text { text: modelData[0]; color: win.dim; font.pixelSize: 12; Layout.fillWidth: true }
                            Text { text: modelData[1]; color: win.fg; font.pixelSize: 13; font.family: win.mono }
                        }
                    }
                }
            }

            // View controls (bottom right, never bottom left: the map attribution lives there)
            Rectangle {
                visible: replay.loaded && replay.hasGps
                anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 12
                width: vc.width + 16; height: vc.height + 16; color: win.panel; radius: 8
                Row {
                    id: vc; x: 8; y: 8; spacing: 4
                    Button { text: "Follow"; checkable: true; checked: win.cameraMode === "follow"; onClicked: win.setCamera("follow"); ToolTip.visible: hovered; ToolTip.text: "Follow the drone (F)" }
                    Button { text: "Top"; onClicked: win.setCamera("top"); ToolTip.visible: hovered; ToolTip.text: "Top-down, north up (T)" }
                    Button { text: "Reset"; onClicked: win.setCamera("reset"); ToolTip.visible: hovered; ToolTip.text: "Reset view (R)" }
                    Button { text: "Whole flight"; onClicked: win.setCamera("zoom"); ToolTip.visible: hovered; ToolTip.text: "Zoom to the whole flight (Z)" }
                    Button { text: "+"; onClicked: win.view("in"); ToolTip.visible: hovered; ToolTip.text: "Zoom in (PageUp)" }
                    Button { text: "−"; onClicked: win.view("out"); ToolTip.visible: hovered; ToolTip.text: "Zoom out (PageDown)" }
                }
            }

            // Shortcut overlay
            Rectangle {
                visible: win.showHelp; anchors.centerIn: parent; width: 560; height: helpText.height + 50
                color: "#f2161b22"; radius: 10; border.color: "#34404d"
                MouseArea { anchors.fill: parent; onClicked: win.showHelp = false }
                Text {
                    id: helpText; x: 25; y: 25; color: win.fg; font.pixelSize: 13; font.family: win.mono
                    text: "Keyboard shortcuts\n\n" +
                          "Space        play / pause\n" +
                          "J / K / L    rewind / pause / fast-forward (again = faster)\n" +
                          "Shift+, / .  10 s back / forward\n" +
                          "Home / End   start / end of the flight\n" +
                          "+ / -        playback speed\n" +
                          "Arrows WASD  pan the view\n" +
                          "Q / E        rotate the view\n" +
                          "PgUp / PgDn  zoom in / out\n" +
                          "F T R Z      follow / top-down / reset / whole flight\n" +
                          "Ctrl+O       open a recording\n" +
                          "Tab          next drone\n" +
                          "?            this help      (Esc closes)"
                }
            }
        }

        // ================= timeline =================
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 104
            color: "#1a2028"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 6
                RowLayout {
                    spacing: 6
                    ToolButton { text: "|◂ Start"; onClicked: replay.toStart(); ToolTip.visible: hovered; ToolTip.text: "Start (Home)" }
                    ToolButton { text: "−10 s"; onClicked: replay.jump(-10); ToolTip.visible: hovered; ToolTip.text: "10 s back (Shift+,)" }
                    ToolButton { text: replay.playing ? "Pause" : "Play"; onClicked: replay.togglePlay(); ToolTip.visible: hovered; ToolTip.text: "Play / pause (Space)" }
                    ToolButton { text: "+10 s"; onClicked: replay.jump(10); ToolTip.visible: hovered; ToolTip.text: "10 s forward (Shift+.)" }
                    ToolButton { text: "End ▸|"; onClicked: replay.toEnd(); ToolTip.visible: hovered; ToolTip.text: "End (End)" }
                    Text { text: replay.timeText; color: win.fg; font.pixelSize: 15; font.family: win.mono; Layout.leftMargin: 8 }
                    Item { Layout.fillWidth: true }
                    ToolButton { text: "−"; onClicked: replay.speedStep(-1); ToolTip.visible: hovered; ToolTip.text: "Slower (-)" }
                    Text { text: replay.speed + "×"; color: win.fg; font.pixelSize: 14; font.family: win.mono
                           horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 52 }
                    ToolButton { text: "+"; onClicked: replay.speedStep(1); ToolTip.visible: hovered; ToolTip.text: "Faster (+)" }
                }
                // bar: flight-mode bands, playhead, click or drag to seek
                Item {
                    id: bar
                    Layout.fillWidth: true; Layout.fillHeight: true
                    property real hoverT: -1
                    Rectangle { anchors.fill: parent; color: "#10151a"; radius: 4 }
                    Repeater {
                        model: replay.modeBands
                        delegate: Rectangle {
                            x: bar.width * modelData.start / Math.max(replay.duration, 0.001)
                            width: Math.max(2, bar.width * (modelData.end - modelData.start) / Math.max(replay.duration, 0.001))
                            height: parent.height; color: modelData.color; opacity: 0.75
                        }
                    }
                    Rectangle {   // playhead
                        x: bar.width * replay.position / Math.max(replay.duration, 0.001) - 1
                        width: 2; height: parent.height; color: "white"
                    }
                    MouseArea {
                        anchors.fill: parent; hoverEnabled: true
                        function at(mx) { return Math.max(0, Math.min(1, mx / width)) * replay.duration }
                        onPressed: (m) => replay.seek(at(m.x))
                        onPositionChanged: (m) => { bar.hoverT = at(m.x); if (pressed) replay.seek(bar.hoverT) }
                        onExited: bar.hoverT = -1
                    }
                    Rectangle {
                        visible: bar.hoverT >= 0
                        x: Math.min(bar.width - width, bar.width * bar.hoverT / Math.max(replay.duration, 0.001))
                        y: -26; width: tipText.width + 14; height: 22; radius: 4; color: "#e614181d"
                        Text { id: tipText; anchors.centerIn: parent; color: win.fg; font.pixelSize: 12; font.family: win.mono
                               text: replayFormat(bar.hoverT) }
                    }
                }
            }
        }
    }

    function replayFormat(s) {
        var m = Math.floor(s / 60); var r = s - m * 60
        return m + ":" + (r < 10 ? "0" : "") + r.toFixed(1)
    }

    // Debug aid: VIEWER_SCREENSHOT=/path/file.png saves the window after 20 s.
    Timer {
        interval: 20000; running: win.mapReady && shotPath !== ""
        onTriggered: win.contentItem.grabToImage(function(r) {
            console.log("SCREENSHOT", r.saveToFile(shotPath) ? "saved" : "failed") })
    }

    component Chip: Rectangle {
        property string text: ""
        property color dot: "#6b7785"
        implicitHeight: 26; implicitWidth: chipRow.width + 16; radius: 13; color: "#252e39"
        Row {
            id: chipRow; anchors.centerIn: parent; spacing: 6
            Rectangle { width: 9; height: 9; radius: 4.5; color: parent.parent.dot; anchors.verticalCenter: parent.verticalCenter }
            Text { text: parent.parent.text; color: win.fg; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
        }
    }
}
