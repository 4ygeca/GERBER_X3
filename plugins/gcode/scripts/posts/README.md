# G-code postprocessors

A postprocessor describes the *dialect* of a CNC controller: how words are
written, how arcs are expressed, what the program header/footer and the
spindle/laser codes look like, which canned drilling cycles the controller
has. It does **not** decide what to cut -- that is the job of the plugin
scripts (`scripts/<plugin>.js`) and the C++ core; the post only supplies the
words.

Tool lengths are not the CAM's business: no post writes `G43`/`G43.1`/`G49`,
`G10 L1` or probing. The sender (ncSender, ioSender, a plugin) or the operator
sets the tool length offset after a tool change or touch-off; a `G49` in a
header would silently cancel it.

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
        peckRefeed: false,             // true: G83 feeds back down after every peck (grblHAL)
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

    // Jobs -- several programs in one file with tool changes (see "Jobs").
    message:    function(text) { return "(MSG, " + text + ")"; },  // absent -- a comment
    toolChange: function(ctx) {        // absent -- M5, message, T<n> M6, M3 S
        return ["M5", ctx.message, "T" + ctx.toolNumber + " M6", "M3 S" + ctx.spindleSpeed];
    },
};
```

`ctx` is `{laser, job, name, programName, spindleSpeed, feedRate, plungeRate,
tool, properties}` (plus the tool change fields, see "Jobs"); `tool` is the
tool snapshot, `properties` -- the project properties (safeZ, clearence,
plunge, workRect, tailing, home, zero, …). `job` is `true` while the text is
built as a part of a job: the **header must not start the spindle then** --
the operator is about to change the tool, the first tool change starts it.
A function field is called as a method of `post` (`this` is the post); the
bundled posts read their settings as `post.<name>`.

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
bottom of every G73/G83 peck. Such a post sets `peckRefeed: true`: Auto then
expands pecks into G0/G1 (rapid back to 0.2 mm above the previous bottom), and
only a G83 chosen explicitly in the tool is written as G83.

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

## Jobs

*File -> Save as Job...* (and the same item in the context menu of a tool
path, a program folder or a group in the tree) writes the tool paths
selected in the tree into ONE file for one side of the board: for example,
everything before through-hole plating, and later everything after it. Laser
and spindle programs never go into one job (another machine mode), nor do
the two sides of the board.

```
G21 G17 G90                       header of the first program, ctx.job = true:
G94                               no spindle start
;JOB board_TS                     job info and tool table (comments, see below)
;...
G0Z20                             the safe height, as after every header
G0Z20                             before the first step of every tool: up to safeZ,
M61 Q0                            the post's toolChange(ctx) block
M5
(MSG, T1 D0.8 DRILL D=0.80)
T1 M6
M3 S12000
G0Z20                             and safeZ again before any XY move
;...                              body of step 1 (with its own info comments)
M3 S20000                         same tool, another speed: no tool change
;...                              body of step 2
...
M5                                footer of the last program, once
M2
```

- The order is proposed by the rule below and can be changed in the job
  dialog (Up/Down). Default: by physical tool (`Tool::identityKey()`) --
  drills by ascending diameter, then end mills, engravers and other tools,
  each by diameter; inside a tool: drilling, milling, the blank cutout last;
  the tool that has the cutout goes last of all. A Profile outside (Outer) of
  a board outline (a layer marked as the board outline, Edge.Cuts, X2
  Profile) is found as the cutout automatically; the dialog has a checkbox
  per step.
- T numbers are local to the job: T1..Tn in the order of first use, without
  a limit by default. The limit is a setting of the job dialog («Tool numbers
  up to T», remembered; 0 -- no limit): it is needed only when the controller
  keeps the tool table itself (grblHAL N_TOOLS = 8, T9 is an error); a sender
  with its own tool library (an ncSender plugin) needs none. Beyond the limit
  the numbers wrap (T9 is T1 again): the dialog warns and the file gets a
  `WARNING` comment. The tool CODE identifies the tool for the sender, not
  the number.
- A tool change block comes before the first step and before every step
  whose tool differs from the previous step's (a tool used again later is
  changed in again under its number). The core writes `G0 Z<safeZ>` before
  and after the block itself: after a manual touch-off the tool may stand
  just above the board, and the next body starts with an XY rapid at the
  current height.
- The same physical tool with other cutting data: no tool change, only
  `spindleOn(ctx)` with the new speed when the speed differs.
- Laser jobs: one laser, no tool changes; another laser "tool" (spot size)
  only gets a comment with the message text.
- GGEasy never writes tool lengths, `G43`/`G43.1`/`G49`/`G10` or probing:
  the tool change procedure, the touch-off and the lengths are the sender's
  (ncSender, ioSender and plugins) and the controller's (grblHAL `$341`).

`toolChange(ctx)` returns the lines of the block. Besides the usual fields
`ctx` has:

| Field                | Value |
|----------------------|-------|
| `toolNumber`         | T of the tool to put in |
| `previousToolNumber` | T of the tool before; 0 for the first change |
| `first`              | `true` for the first tool change of the job |
| `tool`               | the tool snapshot; `tool.code` is always set (the tool code, or the automatic one) |
| `spindleSpeed`       | the speed of the step |
| `message`            | the operator message line, already in the post's dialect (`message`) |
| `messageText`        | the message text itself, ASCII: `T2 D1x10 DRILL D=1.00` |

An empty array writes no tool change commands at all. Without `toolChange`
the core writes `spindleOff`, the message, `T<n> M6` and `spindleOn`.
`message(text)` returns the operator message line for `text` (without it --
a comment). The highest T number is not a post property: it depends on how
the tools are kept (the controller's table or the sender's library) and is
set in the job dialog.

| Post | Tool change |
|------|-------------|
| grblHAL | `M61 Q0` (first change, and when a wrapped number repeats the previous one -- grblHAL skips `T1 M6` when tool 1 is current), `M5`, `(MSG, …)`, `T<n> M6`, `M3 S`, `G4 P<spinUp>` if `spinUp` > 0 |
| LinuxCNC | `M5`, `(MSG, …)`, `T<n> M6`, `M3 S`, `G4 P<spinUp>` if `spinUp` > 0 |
| Generic, Mach3, Mach4, UCCNC, PlanetCNC, Fanuc, Haas, Sinumerik, Heidenhain ISO | `M5`, comment, `T<n> M6`, spindle on |
| grbl 1.1, FluidNC (no M6) | `M5`, comment, `M0`, `M3 S`, `G4 P<spinUp>` if `spinUp` > 0 |
| Marlin / Klipper / RepRapFirmware / Smoothieware | `M5`, comment, `M0` / `PAUSE` / `M226` / `M600`, `M3 S` |

`spinUp` is a property of the grblHAL, LinuxCNC, grbl and FluidNC posts:
the dwell in seconds after the spindle start of a tool change, 0 by default.

### Job info and tool table

Every line is a comment of the post (`;text` or `(text)`). Strip the comment
delimiters and the surrounding whitespace, then the lines are, in this order:

```
JOB <name>                                    the job (the file name without extension)
SIDE TOP | SIDE BOTTOM
MATERIAL <name>                               the project material, "-" when not set
STEPS <count>
STEP <k> T<n> <operation> <program>           one per step, in the order of the file
TOOLS <count>                                 the number of physical tools
T<n> <code> D=<diameter> <type>[ HITS=<holes>]   one per tool, in the order of first use
WARNING <text>                                optional (tool numbers wrapped)
```

- `<k>`, `<n>`, `<count>`, `<holes>` -- decimal integers, `<n>` from 1.
- `<code>` -- `[A-Za-z0-9._-]{1,20}`, the tool code of the tool database
  (the automatic code such as `D0.8x9`, `E1`, `V0.1A30`, `L0.05`, `T1x6`
  when it is not set).
- `<diameter>` -- digits, a point (whatever the locale) and 2 or 3
  decimals: `0.80`, `1.00`, `0.125`.
- `<type>` -- `DRILL`, `ENDMILL`, `ENGRAVER`, `LASER` or `THREADMILL`.
- `<operation>` -- `DRILLING`, `MILLING`, `CUTOUT` (milling of the blank
  cutout), `LASER` or `THREADING`.
- `HITS` -- the holes drilled with the tool (all its drilling steps, all
  tiles); absent for a tool that only mills.
- `<name>`, `<program>`, the material -- free text to the end of the line,
  may be non-ASCII; in posts with parenthesised comments `(` `)` become
  `[` `]`.
- Two tools may have the same `T<n>` when the numbers wrapped; the code
  tells them apart.

A tool line matches
`^T(\d+) ([A-Za-z0-9._-]+) D=(\d+\.\d{2,3}) (DRILL|ENDMILL|ENGRAVER|LASER|THREADMILL)(?: HITS=(\d+))?$`.
The operator message before a tool change is
`T<n> <code> <type> D=<diameter>` (ASCII only; tool names, which may be
Cyrillic or have spaces, are in the plain `Tool:` comments of each body).

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
