// Drilling: canned drilling cycles (G81/G82/G83/G73) through the post, or their
// expansion into plain G0/G1 (common_gcode.js: expandDrillCycle) for controllers
// without them. Slot programs of the drill form are milled along their paths.
// Requires: common_gcode.js
//
// Tool fields (file.tool; a tool that has none of them behaves as the defaults):
//   drillCycle  0 Auto (default): G81; G82 if the tool has a dwell; G83 if
//                 getDepths() splits the depth into several passes (tool Pass < depth)
//               1 Expand: the Auto cycle, always as plain G0/G1 (no canned cycles)
//               2 G81 (feed to the bottom), 3 G82 (+ dwell at the bottom),
//               4 G83 (peck, full retract), 5 G73 (peck, chip break)
//   dwell       G82 dwell at the bottom, s (0 -- none)
//   retractToR  true: G99 -- between holes the tool stays at R (plunge height);
//               false (default): G98 -- back to the initial plane (clearence)
//
// Heights (project properties): initial plane = clearence, R = plunge,
// Z = getDepths().back(). Q (peck step) = the first pass of getDepths() -- the
// even step the depth is split into, never more than the tool pass depth.
//
// A post with `cycles` gets one block per hole, per tile:
//   G0 X Y S           first hole, at the current height (like startPath)
//   G0 Z clearence     the initial plane
//   G98 | G99
//   G81 X Y Z R F      first block: every word forced (+ Q for G83/G73, P for G82)
//   X Y                next holes: the cycle is modal
//   G80
//   G0 Z clearence
// Without `cycles` (or without the code of this cycle or of its retract mode, or
// with drillCycle = Expand) every hole is expanded; one pass then gives exactly
// the old output: G0 X Y S, G0 Z plunge, G1 Z0, G1 Z depth, G0 Z clearence.

var DRILL_CYCLE_AUTO   = 0;
var DRILL_CYCLE_EXPAND = 1;
var DRILL_CYCLE_G81    = 2;
var DRILL_CYCLE_G82    = 3;
var DRILL_CYCLE_G83    = 4;
var DRILL_CYCLE_G73    = 5;

function generate(file) {
    var depths = file.getDepths();

    if (file.slotPaths) {
        // Slots: centre lines (slot no wider than the mill) or contours around
        // them (wider slot) -- every path is milled along, like a profile.
        forEachTile(file, function(file, pathss) {
            for (var i = 0; i < pathss.length; i++)
                for (var j = 0; j < pathss[i].length; j++)
                    millingProfilePath(file, pathss, i, j, depths);
        });
        return;
    }

    var cycle = drillCycle(file, depths);
    forEachTile(file, function(file, pathss) {
        var points = pathss[0][0]; // drill points: vertices of the first path of the first group
        if (cycle.code) {
            cannedCycleTile(file, points, cycle);
        } else {
            for (var k = 0; k < points.length; k++)
                expandDrillCycle(file, points[k].x, points[k].y, cycle);
            // G99 leaves the tool at R after every hole: up to clearence before the next tile.
            if (cycle.toR)
                file.endPath();
        }
    });
}

// What to drill with: the cycle kind, its heights and, when the post can write
// the cycle itself, its codes (otherwise cycle.code stays undefined -> expansion).
function drillCycle(file, depths) {
    var tool  = file.tool;
    var mode  = typeof tool.drillCycle === "number" ? tool.drillCycle : DRILL_CYCLE_AUTO;
    var dwell = typeof tool.dwell === "number" && tool.dwell > 0 ? tool.dwell : 0;

    var kind;
    switch (mode) {
    case DRILL_CYCLE_G81: kind = "drill"; break;
    case DRILL_CYCLE_G82: kind = "dwell"; break;
    case DRILL_CYCLE_G83: kind = "peck"; break;
    case DRILL_CYCLE_G73: kind = "chipBreak"; break;
    default: // Auto, Expand
        kind = depths.length > 1 ? "peck" : dwell > 0 ? "dwell" : "drill";
    }

    var props = file.properties;
    var cycle = {
        kind:    kind,
        depths:  depths,
        bottom:  depths[depths.length - 1],
        r:       props.plunge,
        initial: props.clearence,
        // The same step getDepths() splits the depth with: the expansion pecks
        // exactly at depths[], the cycle by Q from R.
        q:       Math.min(tool.passDepth, -depths[0]),
        dwell:   dwell,
        toR:     !!tool.retractToR, // bool in the tool; 0/1 works as well
    };
    // A peck needs Q > 0 (grblHAL refuses Q <= 0): nothing to peck -- one feed.
    if ((kind === "peck" || kind === "chipBreak") && !(cycle.q > 0))
        cycle.kind = "drill";

    var c = file.cycles;
    // R must lie above the bottom, or the controller rejects the cycle.
    if (c && mode !== DRILL_CYCLE_EXPAND && cycle.bottom < cycle.r) {
        var code    = c[cycle.kind];
        var retract = c.retract ? (cycle.toR ? c.retract.r : c.retract.initial) : undefined;
        if (code && retract && c.cancel) {
            cycle.code    = code;
            cycle.retract = retract;
            cycle.cancel  = c.cancel;
        }
    }
    return cycle;
}

// All holes of one tile with the canned cycle of the post.
function cannedCycleTile(file, points, cycle) {
    var first = points[0];
    // To the first hole at the current height, with the spindle word -- as startPath does.
    file.addLine(file.formatted([file.g0(), file.fmtX(first.x), file.fmtY(first.y), file.strSpindle]));
    // The initial plane: G98 returns here after every hole.
    file.addLine(file.formatted([file.g0(), file.fmtZ(cycle.initial)]));
    // Retract mode right before the cycle: grblHAL resets it to G98 on G80 and
    // on every G1/G2/G3, so set earlier it could be lost.
    file.addLine(cycle.retract);
    // The first block of a cycle must carry Z, R, F (and Q/P): grblHAL does not
    // start a cycle without them, and the formatter would drop unchanged ones.
    file.forceWords("GXYZRQPF");
    for (var k = 0; k < points.length; k++) {
        var words = [cycle.code, file.fmtX(points[k].x), file.fmtY(points[k].y),
                     file.fmtZ(cycle.bottom), file.fmtR(cycle.r)];
        if (cycle.kind === "peck" || cycle.kind === "chipBreak")
            words.push(file.fmtQ(cycle.q));
        if (cycle.kind === "dwell")
            words.push(file.fmtDwell(cycle.dwell));
        words.push(file.strPlungeFeed);
        file.addLine(file.formatted(words));
    }
    // Through formatted(): the formatter must know the modal G is no longer the
    // cycle, or the G81 of the next tile would be dropped as unchanged.
    file.addLine(file.formatted([cycle.cancel]));
    // Where the cycle left the tool (initial plane or R) the formatter cannot know.
    file.forceWords("Z");
    file.addLine(file.formatted([file.g0(), file.fmtZ(cycle.initial)]));
}
