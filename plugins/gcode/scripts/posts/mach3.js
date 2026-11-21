// Postprocessor: Mach3
// See README.md in this folder for the contract.

var post = {
    name: "Mach3",
    description: "Mach3 (ArtSoft). Percent-framed program, parenthesised comments, N line numbers, M3/M5, I/J arcs.",
    extension: "tap",
    comment: "()",
    arcs: "ij",
    arcTolerance: 0.01,
    format: {
        milling: { linear: "G?X?Y?Z?F?S?", arc: "G?X?Y?I+J+Z?F?S?" },
        laser:   { linear: "G?X?Y?Z?F?S?", arc: "G?X?Y?I+J+Z?F?S?" },
        cycle:   "G?X?Y?Z?R?Q?P?F?",
    },
    lineNumbers: { start: 10, step: 10 },

    // Canned drilling cycles (G73 G81 G83, G98/G99, G80). No G82 here: the unit
    // of its P depends on the "G04 Dwell param in ms" setting, so a dwell is
    // expanded (G1 + G4 P in seconds -- keep that setting off).
    cycles: {
        drill:     "G81",
        peck:      "G83",
        chipBreak: "G73",
        cancel:    "G80",
        retract:   { initial: "G98", r: "G99" },
    },

    header: function(ctx) {
        var lines = ["%", "G21 G17 G90 G40 G80"];
        if (!ctx.laser) lines.push("M3 S" + ctx.spindleSpeed);
        return lines;
    },
    footer: function(ctx) {
        return ctx.laser ? ["M30", "%"] : ["M5", "M30", "%"];
    },
    spindleOn:  function(ctx) { return "M3 S" + ctx.spindleSpeed; },
    spindleOff: "M5",
    laserOn:    function(ctx, dynamic) { return dynamic ? "M4" : "M3"; },
    laserOff:   "M5",
};
