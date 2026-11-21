// Postprocessor: Fanuc
// See README.md in this folder for the contract.

var post = {
    name: "Fanuc",
    description: "Fanuc-style ISO (0i/30i and compatibles). Percent-framed, O-number, () comments, N line numbers, G91 G28 Z0 return. Template -- check against your machine.",
    extension: "nc",
    comment: "()",
    arcs: "ij",
    arcTolerance: 0.01,
    format: {
        milling: { linear: "G?X?Y?Z?F?S?", arc: "G?X?Y?I+J+Z?F?S?" },
        laser:   { linear: "G?X?Y?Z?F?S?", arc: "G?X?Y?I+J+Z?F?S?" },
        cycle:   "G?X?Y?Z?R?Q?P?F?",
    },
    lineNumbers: { start: 10, step: 10 },

    // Canned drilling cycles (G73 G81 G82 G83, G98/G99, G80). The G73 back-off
    // and the G83 re-entry clearance are machine parameters. Whole numbers are
    // written without a decimal point (X10, Q1): a control without decimal
    // point programming reads them in least input increments -- check first.
    cycles: {
        drill:     "G81",
        dwell:     "G82",
        peck:      "G83",
        chipBreak: "G73",
        cancel:    "G80",
        retract:   { initial: "G98", r: "G99" },
    },
    dwellUnit: "ms", // P of G4/G82 in milliseconds, written without a decimal point

    header: function(ctx) {
        var lines = ["%", "O0001 (" + (ctx.programName || ctx.name).replace(/[()]/g, "") + ")",
                     "G21 G17 G40 G80 G90 G94", "G54"];
        // A job starts the spindle in its first tool change, not here.
        if (!ctx.laser && !ctx.job) lines.push("S" + ctx.spindleSpeed + " M3");
        return lines;
    },
    // Jobs: the ISO tool change T<n> M6; the message is a comment.
    toolChange: function(ctx) {
        return ["M5", ctx.message, "T" + ctx.toolNumber + " M6", "S" + ctx.spindleSpeed + " M3"];
    },
    footer: function(ctx) {
        return ["M5", "M9", "G91 G28 Z0", "G90", "M30", "%"];
    },
    spindleOn:  function(ctx) { return "S" + ctx.spindleSpeed + " M3"; },
    spindleOff: "M5",
    laserOn:    function(ctx, dynamic) { return dynamic ? "M4" : "M3"; },
    laserOff:   "M5",
};
