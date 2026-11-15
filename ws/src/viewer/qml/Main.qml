import QtQuick
import QtQuick.Window

Window {
    width: 1280
    height: 800
    visible: true
    title: Qt.application.name
    color: "#14181d"

    Text {
        anchors.centerIn: parent
        text: Qt.application.name + " " + Qt.application.version
        color: "#d8dee6"
        font.pixelSize: 28
    }
}
