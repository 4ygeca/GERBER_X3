// Postprocessor: LinuxCNC
// See README.md in this folder for the contract.

var post = {
    name: "LinuxCNC",
    description: "LinuxCNC (RS274NGC). Parenthesised comments, G64 path blending, M3/M5, I/J arcs.",
    extension: "ngc",
    comment: "()",
    arcs: "ij",
    arcTolerance: 0.01,
    format: {
        milling: { linear: "G?X?Y?Z?F?S?", arc: "G?X?Y?I+J+Z?F?S?" },
        laser:   { linear: "G?X?Y?Z?F?S?", arc: "G?X?Y?I+J+Z?F?S?" },
        cycle:   "G?X?Y?Z?R?Q?P?F?",
    },
    lineNumbers: false,

    // Canned drilling cycles (RS274NGC: G73 G81 G82 G83, G98/G99, G80).
    // G83 retracts to R after each peck and comes back at rapid to just above
    // the previous bottom; G73 backs off by 0.254 mm.
    cycles: {
        drill:     "G81",
        dwell:     "G82",
        peck:      "G83",
        chipBreak: "G73",
        cancel:    "G80",
        retract:   { initial: "G98", r: "G99" },
    },
    dwellUnit: "s", // P of G4/G82 in seconds

    // Jobs: T<n> M6 with the machine's own tool change; T1..Tn of the job
    // must be in the tool table. The tool length offset is the machine's
    // business (no G43 here).
    spinUp: 0, // s: G4 P after M3 of a tool change (spindle spin-up); 0 -- none

    header: function(ctx) {
        var lines = ["G21 G17 G90 G40 G94", "G64 P0.01"];
        // A job starts the spindle in its first tool change, not here.
        if (!ctx.laser && !ctx.job) lines.push("M3 S" + ctx.spindleSpeed);
        return lines;
    },
    // (MSG, ...) -- shown to the operator by the GUI.
    message: function(text) { return "(MSG, " + text + ")"; },
    toolChange: function(ctx) {
        var lines = ["M5", ctx.message, "T" + ctx.toolNumber + " M6", "M3 S" + ctx.spindleSpeed];
        if (post.spinUp > 0) lines.push("G4 P" + post.spinUp);
        return lines;
    },
    footer: function(ctx) {
        return ctx.laser ? ["M2"] : ["M5", "M2"];
    },
    spindleOn:  function(ctx) { return "M3 S" + ctx.spindleSpeed; },
    spindleOff: "M5",
    laserOn:    function(ctx, dynamic) { return dynamic ? "M4" : "M3"; },
    laserOff:   "M5",
};
