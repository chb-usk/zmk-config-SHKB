#!/usr/bin/env python3
"""Regenerate the per-layer diagrams (docs/layer_<n>_<name>.svg) from config/shkb.keymap.

layout.svg (the diagram at the top of the README) is a copy of the layer 0 diagram.

Usage (from the repository root):  python3 docs/generate_layer_svgs.py

Key positions follow the physical layout (1u = 42px, key = 42*u - 4 px).
Labels and colors are derived from each binding; add an entry to LABELS
when a new keycode/behavior appears in the keymap.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEYMAP = ROOT / "config" / "shkb.keymap"
OUT_DIR = ROOT / "docs"

# (x, width in u) per key, in keymap binding order. y is fixed per row.
ROWS = [
    (24, [(24.0, 1), (66.0, 1), (108.0, 1), (150.0, 1), (192.0, 1), (234.0, 1),
          (360.0, 1), (402.0, 1), (444.0, 1), (486.0, 1), (528.0, 1), (570.0, 1), (612.0, 1), (654.0, 1), (696.0, 1)]),
    (66, [(24.0, 1.5), (87.0, 1), (129.0, 1), (171.0, 1), (213.0, 1), (255.0, 1),
          (381.0, 1), (423.0, 1), (465.0, 1), (507.0, 1), (549.0, 1), (591.0, 1), (633.0, 1), (675.0, 1.5)]),
    (108, [(24.0, 1.75), (97.5, 1), (139.5, 1), (181.5, 1), (223.5, 1), (265.5, 1),
           (391.5, 1), (433.5, 1), (475.5, 1), (517.5, 1), (559.5, 1), (601.5, 1), (643.5, 2.25)]),
    (150, [(24.0, 2.25), (118.5, 1), (160.5, 1), (202.5, 1), (244.5, 1), (286.5, 1),
           (412.5, 1), (454.5, 1), (496.5, 1), (538.5, 1), (580.5, 1), (622.5, 1.75), (696.0, 1)]),
    (192, [(97.5, 1), (139.5, 1.5), (202.5, 1.5), (265.5, 1.5),
           (381.0, 1.5), (528.0, 1.5), (591.0, 1)]),
]

FILL = {
    "alpha": "#e0e0e3",
    "mod": "#c4c4c8",
    "trans": "#d0d0d4",
    "func": "#b0c8f0",
    "nav": "#b0d8b0",
    "bt": "#f5dbb0",
    "mouse": "#f0b8d8",
}

SYMBOLS = {
    "MINUS": "−", "EQUAL": "=", "BSLH": "\\", "GRAVE": "`", "LBKT": "[", "RBKT": "]",
    "SEMI": ";", "SQT": "'", "COMMA": ",", "DOT": ".", "FSLH": "/",
}

# binding (without leading '&') -> (label, category)
LABELS = {
    "kp ESC": ("ESC", "mod"), "kp TAB": ("TAB", "mod"), "kp BSPC": ("BSP", "mod"),
    "kp RET": ("ENT", "mod"), "kp SPACE": ("SPC", "mod"),
    "kp LEFT_CONTROL": ("CTL", "mod"),
    "kp LSHFT": ("SFT", "mod"), "kp LEFT_SHIFT": ("SFT", "mod"), "kp RSHFT": ("SFT", "mod"),
    "kp LEFT_COMMAND": ("⌘", "mod"), "kp RIGHT_COMMAND": ("⌘", "mod"),
    "kp LA(LEFT_ARROW)": ("A+←", "mod"), "kp LA(RIGHT_ARROW)": ("A+→", "mod"),
    "kp INS": ("INS", "func"), "kp DEL": ("DEL", "func"),
    "kp PRINTSCREEN": ("PrS", "func"), "kp PAUSE_BREAK": ("Brk", "func"),
    "kp LANGUAGE_1": ("IME1", "func"), "kp LANGUAGE_2": ("IME2", "func"),
    "kp K_CONTEXT_MENU": ("Menu", "func"),
    "studio_unlock": ("STU", "func"),
    "kp UP": ("↑", "nav"), "kp DOWN": ("↓", "nav"), "kp LEFT": ("←", "nav"), "kp RIGHT": ("→", "nav"),
    "kp HOME": ("HOM", "nav"), "kp END": ("END", "nav"),
    "kp PAGE_UP": ("PgU", "nav"), "kp PAGE_DOWN": ("PgD", "nav"),
    "BT0Mac": ("BT0", "bt"), "BT1Mac": ("BT1", "bt"), "BT2Win": ("BT2", "bt"),
    "BT3Win": ("BT3", "bt"), "BT4iPad": ("BT4", "bt"),
    "bt BT_CLR_ALL": ("CLRa", "bt"), "bt BT_CLR": ("CLR", "bt"),
    "mkp MB1": ("LClk", "mouse"), "mkp MB2": ("RClk", "mouse"), "mkp MB3": ("MClk", "mouse"),
}

# Alt keys: Option on the Apple layers, LALT/RALT elsewhere.
APPLE_LAYERS = {"mac", "iPad"}
ALT_KEYS = {"kp LEFT_ALT": "L", "kp LALT": "L", "kp RIGHT_ALT": "R", "kp RALT": "R"}


def parse_layers(text):
    layers = []
    for m in re.finditer(r'display-name\s*=\s*"([^"]+)"\s*;\s*bindings\s*=\s*<(.*?)>\s*;', text, re.S):
        body = re.sub(r"/\*.*?\*/", "", m.group(2), flags=re.S)
        bindings = [" ".join(b.split()) for b in body.split("&") if b.strip()]
        layers.append((m.group(1), bindings))
    return layers


def label_for(binding, layer_name):
    if binding == "trans":
        return "▽", "trans"
    if binding in LABELS:
        return LABELS[binding]
    if binding in ALT_KEYS:
        return ("OPT" if layer_name in APPLE_LAYERS else ALT_KEYS[binding] + "ALT"), "mod"
    if m := re.fullmatch(r"mo (\d+)", binding):
        return f"FN{m.group(1)}", "mod"
    if m := re.fullmatch(r"kp F(\d+)", binding):
        return f"F{m.group(1)}", "func"
    if m := re.fullmatch(r"kp N(\d)", binding):
        return m.group(1), "alpha"
    if m := re.fullmatch(r"kp ([A-Z])", binding):
        return m.group(1), "alpha"
    if m := re.fullmatch(r"kp (\w+)", binding):
        if m.group(1) in SYMBOLS:
            return SYMBOLS[m.group(1)], "alpha"
    raise SystemExit(f"No label for '&{binding}' on layer {layer_name}; add it to LABELS")


def num(v):
    return str(int(v)) if float(v).is_integer() else str(v)


def render(layer_name, bindings):
    keys = [(x, y, u) for y, row in ROWS for x, u in row]
    if len(bindings) != len(keys):
        raise SystemExit(f"Layer {layer_name}: {len(bindings)} bindings, expected {len(keys)}")

    out = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="760" height="256" viewBox="0 0 760 256">',
        "<defs>",
        '<radialGradient id="tbg" cx="38%" cy="32%" r="62%"><stop offset="0%" stop-color="#d8d8dc"/><stop offset="55%" stop-color="#909096"/><stop offset="100%" stop-color="#505056"/></radialGradient>',
        '<filter id="ks" x="-5%" y="-10%" width="115%" height="130%"><feDropShadow dx="0" dy="1.5" stdDeviation="1" flood-color="#00000055"/></filter>',
        '<filter id="ps" x="-2%" y="-2%" width="108%" height="110%"><feDropShadow dx="0" dy="3" stdDeviation="3" flood-color="#00000033"/></filter>',
        "</defs>",
        '<rect x="16" y="16" width="314.5" height="220.0" rx="10" fill="#242426" filter="url(#ps)"/>',
        '<rect x="354.0" y="16" width="386.0" height="220.0" rx="10" fill="#242426" filter="url(#ps)"/>',
    ]
    for (x, y, u), binding in zip(keys, bindings):
        label, cat = label_for(binding, layer_name)
        w = 42 * u - 4
        size = "11.5" if len(label) == 1 else "8.5" if len(label) >= 4 else "9.5"
        color = "#9a9aa0" if cat == "trans" else "#1a1a1c"
        text = label.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        out.append(f'<rect x="{x}" y="{y}" width="{num(w)}" height="38" rx="4" fill="{FILL[cat]}" filter="url(#ks)"/>')
        out.append(
            f'<text x="{x + w / 2:.1f}" y="{y + 19}" font-size="{size}" font-weight="600" '
            f"font-family=\"'SF Mono',Menlo,Consolas,monospace\" fill=\"{color}\" "
            f'text-anchor="middle" dominant-baseline="middle">{text}</text>'
        )
    # Trackball between the right thumb keys
    out += [
        '<rect x="444.0" y="192" width="80" height="38" rx="4" fill="#c4c4c8" filter="url(#ks)"/>',
        '<circle cx="484.0" cy="211.0" r="19" fill="url(#tbg)" stroke="#3a3a3e" stroke-width="1.5"/>',
        '<ellipse cx="477.0" cy="204.0" rx="6" ry="4" fill="white" fill-opacity="0.22" transform="rotate(-25,484.0,211.0)"/>',
        "</svg>",
    ]
    return "\n".join(out) + "\n"


def main():
    for i, (name, bindings) in enumerate(parse_layers(KEYMAP.read_text())):
        path = OUT_DIR / f"layer_{i}_{name.lower()}.svg"
        svg = render(name, bindings)
        path.write_text(svg)
        print(f"wrote {path.relative_to(ROOT)}")
        if i == 0:
            (ROOT / "layout.svg").write_text(svg)
            print("wrote layout.svg")


if __name__ == "__main__":
    main()
