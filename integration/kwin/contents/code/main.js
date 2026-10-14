// Wayland reserva al compositor la posición de las ventanas.
function watchQuickLook(window) {
    var appId = String(window.resourceClass).toLowerCase();
    if (appId !== "org.quicklook.viewer" && appId !== "org.quicklook.softblur") {
        return;
    }
    var width = -1;
    var height = -1;
    function center() {
        var geometry = window.frameGeometry;
        if (geometry.width <= 0 || geometry.height <= 0) {
            return;
        }
        width = geometry.width;
        height = geometry.height;
        var area = workspace.clientArea(KWin.PlacementArea, window);
        var x = Math.round(area.x + (area.width - width) / 2);
        var y = Math.round(area.y + (area.height - height) / 2);
        if (geometry.x !== x || geometry.y !== y) {
            window.frameGeometry = {x: x, y: y, width: width, height: height};
        }
    }
    window.frameGeometryChanged.connect(function () {
        var geometry = window.frameGeometry;
        // Permitir que el usuario la mueva; solo recentrar si cambia el tamaño.
        if (geometry.width !== width || geometry.height !== height) {
            center();
        }
    });
    window.outputChanged.connect(center);
    center();
}

workspace.windowAdded.connect(watchQuickLook);
workspace.windowList().forEach(watchQuickLook);
