// Postprocessor: grblHAL
// See README.md in this folder for the contract.

var post = {
    name: "grblHAL",
    description: "grblHAL (32-bit grbl). Same dialect as grbl 1.1: M3 constant / M4 dynamic laser power (chosen in the tool).",
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

    // Jobs (several programs in one file, see README): by default the manual
    // tool change of grblHAL ($341): M6 stops, the sender runs the change and
    // the touch-off. With "Tool lengths: controller table and probe" in the job
    // dialog ($341=0, M6 is a pause, N_TOOLS cells) the job itself writes the
    // lengths: G43 H<n> from the table or a touch of the probe at X0 Y0.
    spinUp: 0, // s: G4 P after M3 of a tool change (spindle spin-up); 0 -- none

    // The probe of the tool lengths (README, "Tool lengths"): a wire on the
    // copper, a clip on the tool, G38.2 straight down from the tool change
    // height. All in mm and mm/min.
    probe: {
        search:   40, // the longest way down to the copper; no contact -> ALARM:5, the program stops
        feed:     60, // search feed
        backoff:   1, // up after the search touch
        fine:      2, // the precise touch: at most this far down
        fineFeed: 15, // its feed
        lift:     20, // up after a touch of the measuring file, room to put the next tool in
    },

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
        lines.push("M5", ctx.message);
        if (ctx.probe) lines.push(this.message("Then clip the probe on the tool, press RUN"));
        lines.push("T" + ctx.toolNumber + " M6");
        // Tool length (README, "Tool lengths"): the length from the table, or
        // none at all when this touch sets Z0 of the work with this very tool.
        if (ctx.length === "table") lines.push("G43 H" + ctx.toolNumber);
        else if (ctx.length === "probe" && ctx.setZero) lines.push("G49");
        if (ctx.probe) lines = lines.concat(this.touch(ctx));
        lines.push("M3 S" + ctx.spindleSpeed);
        if (this.spinUp > 0) lines.push("G4 P" + this.spinUp);
        return lines;
    },
    // A touch of the copper at X0 Y0 in a tool change: Z0 of the work
    // (ctx.setZero: G10 L20 with the current length) or the length of the
    // tool into its cell against the Z0 already set (G10 L10, then G43 H).
    // Then up to the safe height and a pause to take the clip off.
    touch: function(ctx) {
        var p = this.probe;
        var lines = ["G0 X0 Y0", "G91", "G38.2 Z-" + p.search + " F" + p.feed, "G0 Z" + p.backoff,
                     "G38.2 Z-" + p.fine + " F" + p.fineFeed, "G90"];
        if (ctx.setZero) lines.push("G10 L20 P1 Z0");
        else lines.push("G10 L10 P" + ctx.toolNumber + " Z0", "G43 H" + ctx.toolNumber);
        lines.push("G0 Z" + num(ctx.safeZ), this.message("Remove the probe clip, press RUN"), "M0");
        return lines;
    },
    // The measuring file of the tool table (job dialog): every tool of
    // ctx.tools touches one point of the copper, its length goes into its
    // cell (G10 L10); the cells up to ctx.cells this job does not use are
    // zeroed (G10 L1) -- an old length there would be taken for a new tool.
    toolTable: function(ctx) {
        var p = this.probe;
        var lines = [
            "(=== TOOL TABLE OF " + ctx.name + ": MEASURE THE TOOLS INTO THEIR CELLS ===)",
            "(GRBLHAL WITH N_TOOLS CELLS AND $341=0: M6 IS A PAUSE, RUN CONTINUES)",
            "(PROBE: WIRE ON THE COPPER, CLIP ON THE TOOL. ALL TOOLS TOUCH ONE POINT)",
            "(BEFORE START: SPINDLE OVER SOLID COPPER, OUTSIDE THE BOARD OUTLINE,)",
            "(HIGH ENOUGH TO PUT A TOOL IN: ITS TIP 10..30 MM ABOVE THE COPPER)",
            "(EVERY PAUSE: PUT THE TOOL IN, MOVE THE CLIP ONTO IT, PRESS RUN)",
            "(HAND ON HOLD/RESET: A FORGOTTEN CLIP IS NOT DETECTED)",
            "(NO COPPER WITHIN " + p.search + " MM -> ALARM:5, THE PROGRAM STOPS)",
            "(LIFT AFTER EACH TOUCH IS " + p.lift + " MM: CHANGE EVERY Z" + p.lift + " IF YOU NEED MORE ROOM)",
            "(G10 L10 KEEPS THE CELLS IN RAM ONLY. TO SAVE THEM AFTER THE RUN SEND $#)",
            "(AND FOR EACH LINE [T:n|0.000,0.000,z|...] SEND G10 L1 Pn Zz)",
            "(LENGTHS ARE RELATIVE: THE JOB SETS Z0 WITH G43 Hn OF THE TOOL THAT TOUCHES)",
            "",
            "G21 G90 G94 G17",
            "G49             (NO LENGTH OFFSET WHILE MEASURING)",
            "M5              (SPINDLE OFF)",
            "M61 Q0          (CURRENT TOOL = T0, SO THE FIRST M6 PAUSES TOO)",
        ];
        var used = {};
        for (var i = 0; i < ctx.tools.length; i++) {
            var tool = ctx.tools[i];
            used[tool.number] = true;
            lines.push("", "(--- " + tool.text + " ---)",
                this.message(tool.text + ": insert the tool, clip on, press RUN"),
                "T" + tool.number + " M6",
                "G91",
                "G38.2 Z-" + p.search + " F" + p.feed + " (SEARCH THE COPPER)",
                "G0 Z" + p.backoff,
                "G38.2 Z-" + p.fine + " F" + p.fineFeed + " (PRECISE TOUCH)",
                "G90",
                "G10 L10 P" + tool.number + " Z0 (CELL " + tool.number + ")",
                "G91",
                "G0 Z" + p.lift + " (UP FOR THE NEXT TOOL)",
                "G90");
        }
        var zero = [];
        for (var n = 1; n <= ctx.cells; n++)
            if (!used[n]) zero.push("G10 L1 P" + n + " Z0");
        if (zero.length) lines = lines.concat(["", "(--- CELLS THIS JOB DOES NOT USE: ZERO ---)"], zero);
        lines.push("", "(--- END ---)", this.message("Done: remove the clip, send $# and save the cells with G10 L1"), "M30");
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

// A number for a G-code word: at most 3 decimals, no trailing zeros (20, 5.5).
function num(value) { return String(Math.round(value * 1000) / 1000); }
