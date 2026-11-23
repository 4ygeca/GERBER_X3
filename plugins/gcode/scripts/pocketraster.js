// Pocket (raster) gcode generation.
// Requires: common_gcode.js

function generate(file) {
    var depths = file.getDepths();
    // HLDI raster ("Type: HLDI" of the form): file.ext.hldi, see laserHldiTile.
    var hldi = file.laser && file.ext !== undefined && file.ext.hldi === true;
    forEachTile(file, function(file, pathss) {
        if (hldi)
            laserHldiTile(file, pathss);
        else if (file.laser)
            laserTile(file, pathss);
        else
            millingRasterTile(file, pathss, depths);
    });
}

// HLDI raster: pathss[0] is ONE continuous pass of the head. Its odd paths are
// the exposed strokes, the even ones -- moves with the beam off between them
// (PocketRaster::Creator::addAcc): the run-up to the first stroke, the run-out,
// the turn to the next row and its run-up (all of them beyond the exposed
// area; a unidirectional raster -- every row in the same direction -- has
// instead the run-out, the return back over the board and the run-up of the
// next row), the gaps between the strokes of a row (unexposed areas inside the
// board), the run-out after the last stroke. The off moves are G1 at the same
// feed with S0, not G0: the head keeps its speed through them -- the run-up is
// there exactly so that every stroke is exposed at the full feed, a G0 would
// brake and accelerate at every gap. pathss[1], if any, is the outline of the
// exposed area ("Outline the contour after the raster"), traced after all the
// raster lines.
function laserHldiTile(file, pathss) {
    var pass = pathss[0];
    file.startPath(pass[0][0].x, pass[0][0].y);
    switchLaserOn(file);
    // The whole pass in one call (GcFileProxy::addPassLines): the very same
    // lines as the loop below -- odd paths as savePathLines, even ones as
    // laserOffPath with these words ("X"/"Y" -- the vertex) -- without a
    // JS<->C++ round trip per path and per vertex: on a full board (half a
    // million lines) a minute of the loop becomes about a second. The loop
    // stays the reference and the way where the helper is absent (the node
    // test harness).
    if (typeof file.addPassLines === "function") {
        file.addPassLines(0, [file.g1(), "X", "Y", file.strFeed, file.fmtS(0)]);
    } else {
        for (var j = 0; j < pass.length; j++) {
            if (j % 2)
                appendLines(file, file.savePathLines(0, j, false, 0, 0));
            else
                laserOffPath(file, pass[j]);
        }
    }
    laserCurves(file, pathss, 1, false);
}

// An off move of the HLDI pass: straight G1 lines (addAcc builds them of
// points) at the feed with S0. A vertex that does not move the head in the
// output units is skipped: a line without a move would be a bare "S0", and an
// S change without motion makes grbl wait for the planner to empty -- the head
// would stop in the middle of the pass.
function laserOffPath(file, path) {
    var last = file.fmtX(path[0].x) + file.fmtY(path[0].y);
    for (var k = 1; k < path.length; k++) {
        var x = file.fmtX(path[k].x), y = file.fmtY(path[k].y);
        if (x + y === last)
            continue;
        last = x + y;
        file.addLine(file.formatted([file.g1(), x, y, file.strFeed, file.fmtS(0)]));
    }
}
