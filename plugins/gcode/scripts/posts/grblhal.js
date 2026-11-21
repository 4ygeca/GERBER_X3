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

    header: function(ctx) {
        var lines = ["G21 G17 G90", "G94"];
        if (ctx.laser) lines.push("M5 S0"); else lines.push("M3 S" + ctx.spindleSpeed);
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
