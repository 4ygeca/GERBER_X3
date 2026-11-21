# G-code postprocessors

A postprocessor describes the *dialect* of a CNC controller: how words are
written, how arcs are expressed, what the program header/footer and the
spindle/laser codes look like, which canned drilling cycles the controller
has. It does **not** decide what to cut -- that is the job of the plugin
scripts (`scripts/<plugin>.js`) and the C++ core; the post only supplies the
words.

Every `scripts/posts/<id>.js` file defines a global object `post`. The file name
without `.js` is the post id (stored in the settings), `post.name` is what the
combobox in *Settings -> G-Code -> Postprocessor* shows. The list is rebuilt from
the folder each time the settings dialog opens, so dropping a new file in is
enough. `generic.js` is the default.

```js
var post = {
    name: "grbl 1.1",                  // shown in the combobox
    description: "…",                  // shown under the combobox
    extension: "nc",                   // output file extension (without the dot)
    comment: ";",                      // ";" or "()"
    arcs: "ij",                        // "ij" | "r" | "linear"
    arcTolerance: 0.01,                // chord deviation for "linear", mm

    // Word format, one string per line kind: linear (G0/G1) and arc (G2/G3),
    // separately for milling and laser (laser inherits milling when omitted),
    // plus one for the blocks of a canned drilling cycle (G73, G81..G89 and the
    // "X.. Y.." blocks that repeat it; milling linear when omitted).
    //   ?  write the word only if its value changed
    //   +  always write the word
    //   a space after a word puts a space into the output
    //   a letter missing from the string is written like "?" (only if changed)
    // Letters: G X Y Z I J R S F Q P L. Coordinates X Y Z I J R and the peck
    // step Q go on the output grid; P (dwell) and L (repeats) are written as
    // they are. A word with any other letter is dropped by the formatter -- a
    // script writes such words (M, T, ...) with file.addLine().
    format: {
        milling: { linear: "G?X?Y?Z?F?S?", arc: "G?X?Y?I+J+Z?F?S?" },
        laser:   { linear: "G?X?Y?F?S?",   arc: "G?X?Y?I+J+F?S?" },
        cycle:   "G?X?Y?Z?R?Q?P?F?",
    },

    lineNumbers: false,                // or { start: 10, step: 10 } -> "N10 …"

    // Canned drilling cycles of the controller (optional, see below).
    cycles: {
        drill:     "G81",              // feed to the bottom, rapid out
        dwell:     "G82",              // the same with a dwell P at the bottom
        peck:      "G83",              // pecks of Q, out of the hole after each
        chipBreak: "G73",              // pecks of Q, a short back-off after each
        cancel:    "G80",
        retract:   { initial: "G98", r: "G99" },
    },
    dwellUnit: "s",                    // unit of P in G4/G82: "s" (default) or "ms"

    // Text fields may be a string, an array of strings, or a function(ctx)
    // returning either. Empty -> nothing is written.
    header:     function(ctx) { return ["G21 G17 G90", "M3 S" + ctx.spindleSpeed]; },
    footer:     ["M5", "M30"],
    spindleOn:  function(ctx) { return "M3 S" + ctx.spindleSpeed; },
    spindleOff: "M5",
    laserOn:    function(ctx, dynamic) { return dynamic ? "M4" : "M3"; },
    laserOff:   "M5",
    // Optional: a pause in a dialect that is not "G4 P" (Sinumerik: G4 F<s>).
    dwell:      function(ctx, seconds) { return "G4 F" + seconds; },
};
```

`ctx` is `{laser, name, programName, spindleSpeed, feedRate, plungeRate, tool, properties}`;
`tool` is the tool snapshot, `properties` -- the project properties (safeZ,
clearence, plunge, workRect, tailing, home, zero, …).

What the core always writes itself, regardless of the post: the rapid to the
safe height right after the header, the coordinate moves of the tool path, and
the rapid to the safe height (milling) or laser-off + rapid home (laser) right
before the footer.

Arcs: `"ij"` writes `G2/G3 X Y I J` (I/J relative to the arc start, a full
circle as one block); `"r"` writes `G2/G3 X Y R` (arcs of 180° and more are
split in two); `"linear"` replaces every arc with G1 chords within
`arcTolerance`.

## Canned drilling cycles

`cycles` lists the canned cycles the controller has. A missing key (or an
empty string) means the controller has no such cycle; a post without `cycles`
has none at all. Whatever the controller cannot do, `drilling.js` expands into
plain G0/G1 itself (`expandDrillCycle` in `common_gcode.js`): G81 -- rapid to
R, feed to the bottom, rapid out; G82 -- plus a dwell (`file.dwell()`); G83 --
feed one peck, rapid up to R, rapid back down to 0.2 mm above the previous
bottom, feed the next peck; G73 -- feed a peck, back off 0.1 mm, feed on. A
rapid never goes below the deepest cut so far. `retract` defaults to
`{ initial: "G98", r: "G99" }`; an empty code means the controller has no such
retract mode (the cycle is expanded then). `cancel` defaults to `"G80"`;
without it the cycles are ignored. Posts with cycles: grblHAL, LinuxCNC,
Fanuc, Haas, Mach3/Mach4 (without G82: their dwell unit is a setting).

With cycles a tile of holes becomes one block per hole:

```
G0 X10 Y10 S12000        ; first hole at the current height (spindle word)
G0 Z2                    ; initial plane = project clearence
G98                      ; or G99 (tool: retract to R)
G81 X10 Y10 Z-1.9 R0.5 F100    ; first block: Z, R, F (Q, P) always written
X12.54                   ; next holes -- the cycle is modal
G80
G0 Z2
```

R is the project plunge height, Z the final depth (`getDepths()`), Q the even
pass step of `getDepths()` (never more than the tool pass depth), P the tool
dwell. grblHAL resets G99 to G98 on G80 and on every G1/G2/G3, so the retract
code is written right before the cycle. The 3D preview draws cycles the way
grblHAL executes them (`mc_canned_drill`): its G83 retracts to the initial
plane (G98) or R (G99) after every peck and feeds back from there, so for
pecks on grblHAL prefer "retract to R". grblHAL also dwells 0.25 s at the
bottom of every G73/G83 peck.

`dwellUnit` is the unit of P in `G4` and in `G82`: `"s"` -- seconds (grbl,
grblHAL, LinuxCNC; the default), `"ms"` -- whole milliseconds without a decimal
point (Fanuc, Haas; Marlin, Klipper and RepRapFirmware for `G4 P`).

## Drilling fields of the tool

`drilling.js` reads these fields of `file.tool` (the tool database); a tool
that has none of them drills as before (Auto, no dwell, G98):

| Field        | Values |
|--------------|--------|
| `drillCycle` | 0 Auto: G81, G82 if `dwell` > 0, G83 if the depth takes several passes; 1 Expand: the Auto cycle as plain G0/G1, never a canned cycle; 2 G81; 3 G82; 4 G83; 5 G73 |
| `dwell`      | G82 dwell at the bottom, seconds; 0 -- none |
| `retractToR` | `true`: G99, between holes the tool stays at R (plunge height); `false`: G98, back to the clearence |

The peck step Q is the tool pass depth (`passDepth`, the drill's "Pass"):
`getDepths()` splits the depth into even passes no deeper than it.

## Notes

Limitation: `threading.js` writes helical `G2/G3 … I J` blocks directly and is
not adapted by the `arcs` setting.

Industrial controller posts (Fanuc, Haas, Sinumerik, Heidenhain ISO) are
reasonable templates, not certified posts -- check the header/footer against
your machine before running.

The bundled scripts and posts are written to `scripts/` and `scripts/posts/`
next to the program at start-up. A bundled file whose copy on disk differs is
replaced by the bundled version, the old copy is kept as `<name>.bak` (only the
latest one). Files with names of your own are never touched -- to change a
bundled post or script, copy it under a new name.
