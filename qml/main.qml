import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui
import org.mauikit.filebrowsing as FB
import org.nitrux.vpn 1.0

Maui.ApplicationWindow {
    id: root
    title: "Wirecloak"
    width: 920
    height: 640
    visible: true

    color: "transparent"
    background: null

    Maui.WindowBlur {
        view: root
        geometry: Qt.rect(0, 0, root.width, root.height)
        windowRadius: Maui.Style.radiusV
        enabled: true
    }

    Rectangle {
        anchors.fill: parent
        color: Maui.Theme.backgroundColor
        opacity: 0.76
        radius: Maui.Style.radiusV
        border.color: Qt.rgba(1, 1, 1, 0)
        border.width: 1
    }

    // --- Helper Functions ---

    function cleanName(name) {
        if (!name) return ""
        return name.replace(".conf", "")
    }

    function formatBytes(bytes) {
        if (!bytes || bytes === 0) return "0 B"
        var k = 1024
        var sizes = ["B", "KB", "MB", "GB", "TB"]
        var i = Math.floor(Math.log(bytes) / Math.log(k))
        return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + " " + sizes[i]
    }

    // --- Backend Integration ---

    VpnBackend {
        id: backend
        onProfileImported: {
            refreshProfiles()
            pushActivity("Profiles updated")
        }
        onOperationError: (message) => pushActivity("Error: " + message)
    }

    property var profiles: []
    property string selectedProfile: ""
    property int statsTrigger: 0 

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: root.statsTrigger++
    }

    Connections {
        target: backend
        function onTunnelStateChanged() {
            root.statsTrigger++
            if (selectedProfile !== "") {
                selectedStatus = backend.getTunnelStatus(selectedProfile)
            }
            pushActivity(cleanName(selectedProfile) + " state changed")
        }
    }

    property var selectedStatus: ({ active: false, rx: 0, tx: 0, handshake: 0 })
    ListModel { id: activityModel }

    function refreshProfiles() {
        profiles = backend.listProfiles()
        if (profiles.length === 0) {
            selectedProfile = ""
            selectedStatus = ({ active: false, rx: 0, tx: 0, handshake: 0 })
            return
        }
        if (selectedProfile === "" || !profiles.includes(selectedProfile))
            selectedProfile = profiles[0]
        
        root.statsTrigger++
    }

    function refreshStatus() {
        if (selectedProfile === "") return
        selectedStatus = backend.getTunnelStatus(selectedProfile)
    }

    function pushActivity(text) {
        const d = new Date()
        const hh = String(d.getHours()).padStart(2, "0")
        const mm = String(d.getMinutes()).padStart(2, "0")
        activityModel.insert(0, { "msg": text, "time": hh + ":" + mm })
        if (activityModel.count > 20) activityModel.remove(20, activityModel.count - 20)
    }

    Component.onCompleted: refreshProfiles()

    // --- MauiKit File Dialog ---
    FB.FileDialog {
        id: fileDialog
        
        // Use Open mode for selecting files
        mode: FB.FileDialog.Modes.Open
        
        // Restrict to single file selection
        singleSelection: true
        
        // Start in Home directory (MauiKit default)
        currentPath: FB.FM.homePath()
        
        // When user selects a file, import it
        onUrlsSelected: (urls) => {
            if (urls.length > 0) {
                var path = urls[0]
                backend.importProfile(path)
                pushActivity("Importing " + cleanName(path))
            }
        }
    }

    // --- UI Layout ---

    Maui.Page {
        anchors.fill: parent
        flickable: scrollColumn.flickable 
        background: null
        headerMargins: Maui.Style.contentMargins
        showTitle: false

        // Header Actions
        headBar.leftContent: [
            ToolButton {
                icon.name: "list-add"
                // Trigger the MauiKit File Dialog
                onClicked: fileDialog.open() 
            }
        ]

        headBar.rightContent: [
            Label {
                text: "Select Tunnel"
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            },
            ComboBox {
                implicitWidth: 160
                model: profiles
                currentIndex: Math.max(0, profiles.indexOf(selectedProfile))
                displayText: cleanName(currentText)
                delegate: ItemDelegate {
                    width: parent.width
                    text: cleanName(modelData)
                    highlighted: parent.highlightedIndex === index
                    font.weight: parent.currentIndex === index ? Font.Bold : Font.Normal
                }
                onActivated: (i) => {
                    selectedProfile = profiles[i]
                    selectedStatus = backend.getTunnelStatus(selectedProfile)
                    pushActivity("Selected " + cleanName(selectedProfile))
                }
            },
            ToolSeparator {
                bottomPadding: 10
                topPadding: 10
            },
            Maui.ToolButtonMenu {
                icon.name: "overflow-menu"
                MenuItem {
                    text: "About"
                    icon.name: "documentinfo"
                    onTriggered: Maui.App.aboutDialog()
                }
            }
        ]

        // Main Content
        Maui.ScrollColumn {
            id: scrollColumn
            anchors.fill: parent
            spacing: Maui.Style.space.big

            // 1. TUNNEL OPERATIONS
            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Tunnel Operations")
                text2: qsTr("Manage the selected WireGuard interface.")
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                label1.text: qsTr("VPN Status")
                label2.text: qsTr("Bring tunnel interface up or down.")

                Switch {
                    enabled: selectedProfile !== ""
                    checked: selectedStatus.active
                    onToggled: backend.toggleTunnel(selectedProfile, checked)
                    Connections {
                        target: root
                        function onStatsTriggerChanged() {
                            if (selectedProfile !== "") {
                                selectedStatus = backend.getTunnelStatus(selectedProfile)
                            }
                        }
                    }
                }
            }

            // 2. ACTIVE CONNECTIONS
            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Active Connections")
                text2: qsTr("Manage your available WireGuard tunnels.")
            }
            
            Maui.ListBrowser {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                model: profiles
                
                holder.visible: profiles.length === 0
                holder.emoji: "network-vpn"
                holder.title: qsTr("No Tunnels")
                holder.body: qsTr("Add a WireGuard config file to get started.")

                delegate: Maui.ListDelegate {
                    width: ListView.view.width
                    property var st: {
                        root.statsTrigger
                        return backend.getTunnelStatus(modelData)
                    }
                    label: cleanName(modelData)
                    label2: st.active 
                            ? "Active • ↑ " + formatBytes(st.tx) + " ↓ " + formatBytes(st.rx)
                            : "Disconnected"
                    iconName: st.active ? "emblem-default" : "emblem-unmounted"
                    
                    Button {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        display: AbstractButton.IconOnly
                        icon.name: "edit-delete"
                        onClicked: {
                            backend.removeProfile(modelData)
                            refreshProfiles()
                            pushActivity("Removed " + cleanName(modelData))
                        }
                    }
                }
            }

            // 3. RECENT ACTIVITY
            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Recent Activity")
                text2: qsTr("Log of recent connection events.")
            }
            
            Maui.ListBrowser {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                model: activityModel
                
                holder.visible: activityModel.count === 0
                holder.emoji: "dialog-information"
                holder.title: qsTr("No Activity")
                holder.body: qsTr("Events will appear here.")
                
                delegate: Maui.ListDelegate {
                    width: ListView.view.width
                    label: model.msg
                    label2: model.time
                    iconName: "dialog-information"
                    iconSize: Maui.Style.iconSizes.small
                }
            }
        }
    }
}
