// Postprocessor: grblHAL
// See README.md in this folder for the contract.

var post = {
    name: "grblHAL",
    description: "grblHAL (32-bit grbl). Same dialect as grbl 1.1 with M4 dynamic laser power.",
    extension: "nc",
    comment: ";",
    arcs: "ij",
    arcTolerance: 0.01,
    format: {
        milling: { linear: "G?X?Y?Z?F?S?", arc: "G?X+Y+I+J+Z?F?S?" },
        laser:   { linear: "G?X?Y?Z?F?S?", arc: "G?X+Y+I+J+Z?F?S?" },
        cycle:   "G?X?Y?Z?R?Q?P?F?",
    },
    lineNumbers: false,

    // Canned drilling cycles of the grblHAL core (G73 G81 G82 G83, G98/G99, G80).
    // Firmware details: G73/G83 dwell 0.25 s at the bottom of every peck; G73
    // backs off by $28 (0.1 mm by default); G83 retracts to the initial plane
    // (G98) or to R (G99) after EVERY peck and feeds back down from there
    // (peckRefeed: Auto then expands pecks into G0/G1; an explicit G83 in the
    // tool is still written -- with it prefer "retract to R" (G99)).
    cycles: {
        drill:     "G81",
        dwell:     "G82",
        peck:      "G83",
        chipBreak: "G73",
        cancel:    "G80",
        retract:   { initial: "G98", r: "G99" },
        peckRefeed: true,
    },
    dwellUnit: "s", // P of G4/G82 in seconds

    // Jobs (several programs in one file, see README): manual tool change of
    // grblHAL ($341): M6 stops, the sender runs the change and the touch-off.
    spinUp: 0, // s: G4 P after M3 of a tool change (spindle spin-up); 0 -- none

    header: function(ctx) {
        var lines = ["G21 G17 G90", "G94"];
        // A job starts the spindle in its first tool change, not here.
        if (ctx.laser) lines.push("M5 S0"); else if (!ctx.job) lines.push("M3 S" + ctx.spindleSpeed);
        return lines;
    },
    // (MSG, ...) -- grblHAL reports it to the sender ([MSG:...]).
    message: function(text) { return "(MSG, " + text + ")"; },
    toolChange: function(ctx) {
        var lines = [];
        // grblHAL skips "T1 M6" when tool 1 is already current: M61 Q0 (no tool)
        // first -- and again when wrapped numbers repeat the previous one.
        if (ctx.first || ctx.previousToolNumber === ctx.toolNumber) lines.push("M61 Q0");
        lines.push("M5", ctx.message, "T" + ctx.toolNumber + " M6", "M3 S" + ctx.spindleSpeed);
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
