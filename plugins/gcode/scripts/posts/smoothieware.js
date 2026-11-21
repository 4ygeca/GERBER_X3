// Postprocessor: Smoothieware
// See README.md in this folder for the contract.

var post = {
    name: "Smoothieware",
    description: "Smoothieware (Smoothieboard). Native G2/G3, M3/M5 spindle, laser module via S on G1.",
    extension: "gcode",
    comment: ";",
    arcs: "ij",
    arcTolerance: 0.01,
    format: {
        milling: { linear: "G?X?Y?Z?F?S?", arc: "G?X+Y+I+J+Z?F?S?" },
        laser:   { linear: "G?X?Y?Z?F?S?", arc: "G?X+Y+I+J+Z?F?S?" },
    },
    lineNumbers: false,

    header: function(ctx) {
        var lines = ["G21", "G90", "G17"];
        // A job starts the spindle in its first tool change, not here.
        if (ctx.laser) lines.push("M5"); else if (!ctx.job) lines.push("M3 S" + ctx.spindleSpeed);
        return lines;
    },
    // Jobs: no M6 -- a tool change is a pause after the message comment:
    // M600 suspends the job; M601 (or the play button) resumes.
    toolChange: function(ctx) {
        return ["M5", ctx.message, "M600", "M3 S" + ctx.spindleSpeed];
    },
    footer: function(ctx) {
        return ["M5", "M2"];
    },
    spindleOn:  function(ctx) { return "M3 S" + ctx.spindleSpeed; },
    spindleOff: "M5",
    laserOn:    function(ctx, dynamic) { return dynamic ? "M4" : "M3"; },
    laserOff:   "M5",
};
