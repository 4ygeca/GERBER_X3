// Postprocessor: grbl 1.1
// See README.md in this folder for the contract.

var post = {
    name: "grbl 1.1",
    description: "grbl 1.1 (Arduino CNC/laser). I/J arcs, M3 constant / M4 dynamic laser power, S = laser power ($30 max).",
    extension: "nc",
    comment: ";",
    arcs: "ij",
    arcTolerance: 0.01,
    format: {
        milling: { linear: "G?X?Y?Z?F?S?", arc: "G?X+Y+I+J+Z?F?S?" },
        laser:   { linear: "G?X?Y?Z?F?S?", arc: "G?X+Y+I+J+Z?F?S?" },
    },
    lineNumbers: false,

    // Jobs: grbl 1.1 has no M6 -- a tool change is a pause (M0) after the
    // message comment; change the tool, touch off and resume (cycle start).
    spinUp: 0, // s: G4 P after M3 of a tool change (spindle spin-up); 0 -- none

    header: function(ctx) {
        var lines = ["G21 G17 G90", "G94"];
        // A job starts the spindle in its first tool change, not here.
        if (ctx.laser) lines.push("M5 S0"); else if (!ctx.job) lines.push("M3 S" + ctx.spindleSpeed);
        return lines;
    },
    toolChange: function(ctx) {
        var lines = ["M5", ctx.message, "M0", "M3 S" + ctx.spindleSpeed];
        if (post.spinUp > 0) lines.push("G4 P" + post.spinUp);
        return lines;
    },
    footer: function(ctx) {
        return ctx.laser ? ["M5 S0", "M2"] : ["M5", "M2"];
    },
    spindleOn:  function(ctx) { return "M3 S" + ctx.spindleSpeed; },
    spindleOff: "M5",
    laserOn:    function(ctx, dynamic) { return dynamic ? "M4" : "M3"; },
    laserOff:   "M5",
};
