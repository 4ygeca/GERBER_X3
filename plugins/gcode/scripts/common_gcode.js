// Common utilities shared by all GCode generation scripts.
// Loaded automatically before the plugin script (see GCode::File::runJsScript).
//
// The `file` object (GCode::GcFileProxy):
//   file.tool         - tool snapshot (diameter, feedRate, passDepth, ...; drill
//                       fields drillCycle, dwell, retractToR when the tool has them)
//   file.properties   - project properties (GCode::Properties form) as one object:
//                       safeZ, clearence, plunge, boardThickness, copperThickness, glue,
//                       tailing{spacingX, spacingY, stepsX, stepsY},
//                       workRect{x, y, width, height}, home{x, y}, zero{x, y}, pins[]
//   file.laser, file.spiralRamp, file.notTile, file.slotPaths, file.toolDiameter, ...
//   file.getDepths(), file.getToolPaths(ox, oy), file.savePathLines(...),
//   file.startPath(x, y), file.endPath(), file.addLine(s), file.formatted([...]),
//   file.forceWords(letters) - next formatted() writes these letters even if unchanged,
//   file.g0()..g3(), file.fmtX()..fmtF(), file.fmtR/fmtQ/fmtP(v), file.fmtDwell(seconds),
//   file.strFeed/strPlungeFeed/strSpindle,
//   file.comment(text), file.spindleOn(), file.spindleOff(), file.laserOn(dyn), file.laserOff(),
//   file.dwell(seconds) - pause line in the post dialect ("G4 P..." by default)
//   file.cycles       - canned drilling cycles of the post ({drill, dwell, peck,
//                       chipBreak, cancel, retract{initial, r}}), undefined if none
//   file.dwellUnit    - unit of the dwell word P of the post: "s" or "ms"
//   file.ext          - plugin-specific extension object (Profile: bridges), may be undefined

// Append an array of gcode line strings to the file output.
function appendLines(file, lines) {
    for (var l = 0; l < lines.length; l++)
        file.addLine(lines[l]);
}

// Iterate over all tile positions and invoke callback(file, pathss, ox, oy)
// for each tile that has non-empty tool paths. Honours file.notTile.
function forEachTile(file, callback) {
    var props      = file.properties;
    var stepsX     = props.tailing.stepsX;
    var stepsY     = props.tailing.stepsY;
    var workWidth  = props.workRect.width;
    var workHeight = props.workRect.height;
    var spaceX     = props.tailing.spacingX;
    var spaceY     = props.tailing.spacingY;

    for (var xi = 0; xi < stepsX; xi++) {
        for (var yi = 0; yi < stepsY; yi++) {
            var ox     = (workWidth  + spaceX) * xi;
            var oy     = (workHeight + spaceY) * yi;
            var pathss = file.getToolPaths(ox, oy);
            if (pathss.length > 0)
                callback(file, pathss, ox, oy);
            if (file.notTile)
                return;
        }
    }
}

// Plunge to depth at the current XY (vertical feed).
function plungeTo(file, depth) {
    file.z = depth;
    file.addLine(file.formatted([file.g1(), file.fmtZ(depth), file.strPlungeFeed]));
}

// ---------------------------------------------------------------------------
// Canned drilling cycles expanded into plain G0/G1: for controllers without
// them (a post without `cycles`) or when the tool asks so (drillCycle = Expand).
// The same idea as Fusion's expandCyclePoint -- one motion for every post.
//
// cycle: {
//   kind:   "drill" (G81) | "dwell" (G82) | "peck" (G83) | "chipBreak" (G73)
//   depths: pass bottoms from file.getDepths() (negative, deepest last); pecks
//           (G83/G73) stop at each of them, G81/G82 go straight to the last
//   r:      R plane -- the project plunge height
//   dwell:  G82 dwell at the bottom, s (0 -- none)
//   toR:    G99: stay at R after the hole (the next one is reached at R
//           height); otherwise G98: back to the initial plane (clearence)
// }
//
// No rapid ever goes below the deepest cut of the hole so far: G83 clears the
// chips up at R and comes back down at rapid only to DRILL_PECK_CLEARANCE
// above the previous bottom, then feeds on; G73 only backs off by
// DRILL_CHIP_BREAK and feeds on.
var DRILL_PECK_CLEARANCE = 0.2; // mm above the previous peck bottom (G83 re-entry)
var DRILL_CHIP_BREAK     = 0.1; // mm back-off after a peck (G73); grblHAL $28 default

function expandDrillCycle(file, x, y, cycle) {
    // G0 X Y S, G0 Z plunge (= R), G1 Z0: the entry every hole had before the
    // cycles, so single-pass drilling stays exactly what it was.
    file.startPath(x, y);
    var pecks  = cycle.kind === "peck" || cycle.kind === "chipBreak";
    var depths = pecks ? cycle.depths : [cycle.depths[cycle.depths.length - 1]];
    for (var d = 0; d < depths.length; d++) {
        if (d > 0) {
            var bottom = depths[d - 1]; // the deepest cut so far
            if (cycle.kind === "peck") {
                // Chips out: up to R, back down at rapid -- only to just above the bottom.
                file.addLine(file.formatted([file.g0(), file.fmtZ(cycle.r)]));
                if (bottom + DRILL_PECK_CLEARANCE < cycle.r)
                    file.addLine(file.formatted([file.g0(), file.fmtZ(bottom + DRILL_PECK_CLEARANCE)]));
            } else {
                // Chip break: a short back-off, the drill stays in the hole.
                file.addLine(file.formatted([file.g0(), file.fmtZ(Math.min(bottom + DRILL_CHIP_BREAK, cycle.r))]));
            }
        }
        plungeTo(file, depths[d]);
    }
    if (cycle.kind === "dwell" && cycle.dwell > 0)
        file.addLine(file.dwell(cycle.dwell));
    if (cycle.toR)
        file.addLine(file.formatted([file.g0(), file.fmtZ(cycle.r)]));
    else
        file.endPath();
}

// ---------------------------------------------------------------------------
// Shared tile strategies.

// Laser: every curve is one pass at full power, no depth.
function laserTile(file, pathss) {
    file.addLine(file.laserOn(true));
    for (var i = 0; i < pathss.length; i++) {
        var paths = pathss[i];
        for (var j = 0; j < paths.length; j++) {
            var path = paths[j];
            file.startPath(path[0].x, path[0].y);
            appendLines(file, file.savePathLines(i, j, false, 0, 0));
            file.endPath();
        }
    }
}

// One contour pathss[i][j] milled through all depths as one entry: closed
// contour -> spiral ramp (or plunge per level), open contour -> zigzag
// alternating direction. Starts with startPath, ends with endPath.
function millingProfilePath(file, pathss, i, j, depths) {
    var path  = pathss[i][j];
    var perim = path.perimeter;
    file.startPath(path[0].x, path[0].y);
    if (path.closed) {
        if (file.spiralRamp) {
            // Spiral: ramp down through each depth pass, then one flat finishing pass
            for (var d = 0; d < depths.length; d++)
                appendLines(file, file.savePathLines(i, j, false, perim, depths[d]));
            appendLines(file, file.savePathLines(i, j, false, 0, 0));
        } else {
            // Plunge straight down at each step; no spiral step left to clean up
            for (var d = 0; d < depths.length; d++) {
                plungeTo(file, depths[d]);
                appendLines(file, file.savePathLines(i, j, false, 0, 0));
            }
        }
    } else {
        if (file.spiralRamp) {
            // Zigzag: alternate direction on each depth pass
            for (var d = 0; d < depths.length; d++)
                appendLines(file, file.savePathLines(i, j, (d & 1) !== 0, perim, depths[d]));
            appendLines(file, file.savePathLines(i, j, (depths.length & 1) !== 0, 0, 0));
        } else {
            // Still alternate: plunging where the tool already stands is cheaper
            for (var d = 0; d < depths.length; d++) {
                plungeTo(file, depths[d]);
                appendLines(file, file.savePathLines(i, j, (d & 1) !== 0, 0, 0));
            }
        }
    }
    file.endPath();
}

// Milling profile: closed contour -> spiral ramp (or plunge per level),
// open contour -> zigzag alternating direction, several segments -> step-down.
function millingProfileTile(file, pathss, depths) {
    for (var i = 0; i < pathss.length; i++) {
        var paths = pathss[i];
        if (paths.length === 1) {
            millingProfilePath(file, pathss, i, 0, depths);
        } else {
            // Multiple segments: step-down across all segments at each depth level
            file.startPath(paths[0][0].x, paths[0][0].y);
            for (var d = 0; d < depths.length; d++) {
                for (var j = 0; j < paths.length; j++) {
                    var pt = paths[j][0];
                    file.addLine(file.formatted([file.g0(), file.fmtX(pt.x), file.fmtY(pt.y)]));
                    plungeTo(file, depths[d]);
                    appendLines(file, file.savePathLines(i, j, false, 0, 0));
                    file.addLine(file.formatted([file.g0(), file.fmtZ(0)]));
                }
            }
            file.endPath();
        }
    }
}

// Milling pocket (offset loops): consecutive loops of the SAME pocket (i
// unchanged) are linked at depth, a jump longer than three tool diameters is
// a break -- lift and re-enter. Crossing to a different pathss[i] always
// lifts, even when its start point lands close by: separate i's are
// independently computed regions (e.g. two nearby drilled pads) with no
// guarantee their boundaries actually touch -- dragging the tool through
// whatever solid stock sits between them at full depth breaks it.
function millingPocketTile(file, pathss, depths) {
    var toolDiameter = file.toolDiameter;
    var prevPt = pathss[0][0][0];
    var prevI = 0;
    file.startPath(prevPt.x, prevPt.y);

    for (var i = 0; i < pathss.length; i++) {
        var paths = pathss[i];
        for (var d = 0; d < depths.length; d++) {
            for (var j = 0; j < paths.length; j++) {
                var pt = paths[j][0];
                var dx = pt.x - prevPt.x;
                var dy = pt.y - prevPt.y;
                if (i !== prevI || Math.sqrt(dx * dx + dy * dy) > toolDiameter * 3) {
                    file.endPath();
                    file.startPath(pt.x, pt.y);
                }
                prevPt = pt;
                prevI = i;
                file.addLine(file.formatted([file.g1(), file.fmtX(pt.x), file.fmtY(pt.y)]));
                plungeTo(file, depths[d]);
                appendLines(file, file.savePathLines(i, j, false, 0, 0));
            }
        }
    }
    file.endPath();
}

// Milling raster: every stroke is its own pass at every depth.
function millingRasterTile(file, pathss, depths) {
    for (var i = 0; i < pathss.length; i++) {
        var paths = pathss[i];
        for (var d = 0; d < depths.length; d++) {
            for (var j = 0; j < paths.length; j++) {
                file.startPath(paths[j][0].x, paths[j][0].y);
                plungeTo(file, depths[d]);
                appendLines(file, file.savePathLines(i, j, false, 0, 0));
                file.endPath();
            }
        }
    }
}

// Profile-like plugin (Thermal, CrossHatch, ...): laser or milling profile.
function profileGenerate(file) {
    var depths = file.getDepths();
    forEachTile(file, function(file, pathss) {
        if (file.laser)
            laserTile(file, pathss);
        else
            millingProfileTile(file, pathss, depths);
    });
}
