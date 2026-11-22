#!/usr/bin/env python3
"""Generate the DSI Tetra program definition (Tetra.syn) from Edisyn.

Edisyn (https://github.com/eclab/edisyn, Apache License 2.0, by Sean Luke)
has a tested Prophet '08 / Tetra / Mopho editor. This reads its
DSIProphet08.java for:
  - the order of its parameters list, which is each parameter's NRPN number
    (layer A from 0, layer B from 200);
  - each control's label, range and choice list, matched to the parameters
    by their names (Edisyn builds its controls in loops over the layers,
    oscillators, envelopes, LFOs and so on).
Edits go out as NRPN (CC 99, 98, 6, 38) on the Device's MIDI channel; set
the Tetra's MParamRec to NRPN or All. The sequencer's note grids and the
name aren't included, and nor is getting the program: a Tetra's program
dumps are packed and reordered (to do).

Usage: edisyn_tetra.py path/to/edisyn/synth/dsiprophet08/DSIProphet08.java > Tetra.syn
"""

import re
import sys


def arrays(src):
    out = {}
    for m in re.finditer(r"String\[\]\s+(\w+)\s*=\s*(?:new String\[\])?\s*\{(.*?)\};", src, re.S):
        out[m.group(1)] = re.findall(r'"([^"]*)"', re.sub(r"//[^\n]*", "", m.group(2)))
    return out


def controls(src):
    """A regex for each control's key (its literal parts, with numbers for the
    loop variables) -> (label, min, max, subtract, list)."""
    out = []
    last_list = None
    # Some constructors run over several lines: join them first.
    src = re.sub(r",\s*\n\s*", ", ", src)
    for line in src.split("\n"):
        pm = re.search(r"params\s*=\s*(\w+)\s*;", line)
        if pm:
            last_list = pm.group(1)
        m = re.search(r'new (LabelledDial|Chooser|CheckBox)\("([^"]*)", this, (.*?)(?:, color, (-?\d+), (-?\d+)(?:, (-?\d+))?|, params\)|\))', line)
        if not m:
            continue
        kind, label, keyexpr, lo, hi, sub = m.groups()
        parts = re.findall(r'"([^"]*)"|(\w+)', keyexpr)
        regex = ""
        for lit, var in parts:
            if lit:
                regex += re.escape(lit)
            elif var in ("layer", "osc", "env", "lfo", "mod", "track", "i", "inst"):
                regex += r"\d"
            else:
                regex += r"[a-z]+"
        label = re.sub(r"\s*\[[^\]]*\]", "", label).strip()
        if kind == "LabelledDial" and lo is None:
            continue  # a range Edisyn works out: not readable here
        if kind == "LabelledDial":
            out.append((re.compile("^" + regex + "$"), label, int(lo), int(hi), int(sub) if sub else 0, None))
        elif kind == "CheckBox":
            out.append((re.compile("^" + regex + "$"), label, 0, 1, 0, "OFFON"))
        else:
            out.append((re.compile("^" + regex + "$"), label, 0, None, 0, last_list))
    return out


# Names on the Tracker, by the parameter's name without its layer.
NAMES = {
    "dco1frequency": "Osc 1 freq", "dco1finetune": "Osc 1 fine", "dco1shape": "Osc 1 shape",
    "dco1glide": "Osc 1 glide", "dco1key": "Osc 1 key", "dco2frequency": "Osc 2 freq",
    "dco2finetune": "Osc 2 fine", "dco2shape": "Osc 2 shape", "dco2glide": "Osc 2 glide",
    "dco2key": "Osc 2 key", "sync": "Sync", "glidemode": "Glide mode", "slop": "Slop", "mix": "Osc mix",
    "noise": "Noise", "vcffrequency": "Cutoff", "vcfresonance": "Resonance", "vcfkeyboardamount": "Key amount",
    "vcfaudiomodulation": "Audio mod", "vcfpoles": "4-pole", "vcainitiallevel": "VCA level",
    "vcaoutputspread": "Spread", "vcavoicevolume": "Voice volume", "tempo": "BPM", "clockdivide": "Clock divide",
    "pitchbendrange": "Bend range", "sequencertrigger": "Seq trigger", "unisonmode": "Unison mode",
    "unisonkeymode": "Key assign", "arpeggiatormode": "Arp mode", "unison": "Unison", "arpeggiator": "Arp",
    "sequencer": "Sequencer", "tetrafeedbackgain": "Feedback", "tetrafeedbackvolume": "Feedback vol",
    "tetrapushitnote": "Push-it note", "tetrapushitvelocity": "Push-it vel", "tetrapushitmode": "Push-it mode",
    "tetrasuboscillator1level": "Sub 1 level", "env1amount": "Filter env amount",
    "env2amount": "Amp env amount", "env3amount": "Env 3 amount", "tetrasuboscillator2level": "Sub 2 level",
}


def short(name):
    """A row name for parameters without an entry above."""
    for pre, title in (("env1", "Filter env "), ("env2", "Amp env "), ("env3", "Env 3 "), ("lfo", "LFO "),
                       ("mod", "Mod "), ("track", "Track ")):
        if name.startswith(pre):
            rest = name[len(pre):]
            if pre in ("lfo", "mod", "track"):
                title += rest[0] + " "
                rest = rest[1:]
            rest = rest.replace("moddestination", "dest").replace("destination", "dest").replace(
                "velocityamount", "velocity").replace("keysync", "key sync").replace("frequency", "rate")
            return (title + rest).strip()
    for pre in ("wheel", "pressure", "breath", "velocity", "foot"):
        if name.startswith(pre):
            return pre.capitalize() + " " + name[len(pre):].replace("destination", "dest")
    return name


def main():
    src = open(sys.argv[1]).read()
    lists = arrays(src)
    params = re.search(r"final static String\[\] parameters\s*=\s*new String\[\]\s*\{(.*?)\};", src, re.S)
    params = re.findall(r'"([^"]*)"', re.sub(r"//[^\n]*", "", params.group(1)))
    ctl = controls(src)

    out = []
    w = out.append
    w("# DSI Tetra program editor for the EUCLID edition of the Polyend Tracker.")
    w("# Generated by euclid-synths/tools/edisyn_tetra.py from Edisyn's DSIProphet08.java")
    w("# (https://github.com/eclab/edisyn, Apache License 2.0, Copyright Sean Luke).")
    w("# Edits the program playing on the Device's MIDI channel with NRPN; set the")
    w("# Tetra's MParamRec to NRPN or All. Getting the program isn't supported yet.")
    w('synth "DSI Tetra"')
    w('hint "Pick programs on the Tetra; edits go to it"')
    w("pause 1")
    w("device 1")
    w("port din")
    w("template nrpn d+B0 63 ph | d+B0 62 pl | d+B0 06 vh | d+B0 26 vl")
    w('list offon "Off" "On"')
    used = {}

    def list_for(name):
        if name == "OFFON":
            return "offon", 2
        if name not in lists:
            return None, None
        # Short (11 letters at most) and unique: two Edisyn lists can share a start.
        for short_name, full in used.items():
            if full == name:
                return short_name, len(lists[name])
        short_name = re.sub(r"[^a-z0-9]", "", name.lower())[:9] + "%02d" % len(used)
        used[short_name] = name
        w("list %s " % short_name + " ".join('"%s"' % s for s in lists[name]))
        return short_name, len(lists[name])

    rows = []  # (group, line)
    # The two settings for the whole program (not a layer's).
    for key, name in (("keyboardmode", "Keyboard mode"), ("splitpoint", "Split point")):
        found = next((c for c in ctl if c[0].match(key)), None)
        if found is not None and key in params:
            _, label, lo, hi, sub, lst = found
            opts = ""
            if lst is not None:
                lname, count = list_for(lst)
                hi = count - 1
                opts = " list=" + lname
            rows.append(("Program", 'param nrpn %d "%s" %d %d%s' % (params.index(key), name, lo, hi, opts)))
    for n, key in enumerate(params):
        m = re.match(r"layer([12])(.*)", key)
        if not m or re.match(r"track\dnote", m.group(2)) or m.group(2) in ("tetraeditorbyte",) or \
           m.group(2).startswith("tetraassignableparameter"):
            continue
        layer, base = m.group(1), m.group(2)
        found = next((c for c in ctl if c[0].match(key)), None)
        # Controls whose ranges or labels Edisyn works out, as it sets them.
        extra = {
            "env1amount": ("Envelope", 0, 254, 127, None), "env2amount": ("Envelope", 0, 127, 0, None),
            "env3amount": ("Envelope", 0, 254, 127, None), "tetrapushitnote": ("Push-It Note", 0, 127, 0, None),
            "tetrapushitmode": ("Push-It Mode", 0, None, 0, "PUSH_IT_MODES"),
            "track1destination": ("Track", 0, None, 0, "MODULATION_DESTINATIONS"),
            "track2destination": ("Track", 0, None, 0, "MODULATION_DESTINATIONS_SEQ24"),
            "track3destination": ("Track", 0, None, 0, "MODULATION_DESTINATIONS"),
            "track4destination": ("Track", 0, None, 0, "MODULATION_DESTINATIONS_SEQ24"),
        }
        if base in extra:
            found = (None,) + extra[base]
        if found is None:
            continue
        _, label, lo, hi, sub, lst = found
        opts = []
        if lst is not None:
            lname, count = list_for(lst)
            if lname is None:
                continue
            hi = count - 1
            opts.append("list=" + lname)
        if sub:
            opts.append("show=%d" % -sub)
            opts.append("init=%d" % sub)
        name = NAMES.get(base, short(base))
        group = base
        for pre, title in (("dco", "Osc"), ("sync", "Osc"), ("glidemode", "Osc"), ("slop", "Osc"), ("mix", "Osc"),
                           ("noise", "Osc"), ("tetrasub", "Osc"), ("tetrafeedback", "Osc"), ("vcf", "Filter"),
                           ("env1", "Filter"), ("vca", "Amp"), ("env2", "Amp"), ("lfo", "LFOs"), ("env3", "Mod"),
                           ("mod", "Mod"), ("wheel", "Mod"), ("pressure", "Mod"), ("breath", "Mod"),
                           ("velocity", "Mod"), ("foot", "Mod"), ("track", "Mod")):
            if base.startswith(pre):
                group = title
                break
        else:
            group = "Play"
        rows.append(("%s %s" % ("A" if layer == "1" else "B", group),
                     'param nrpn %d "%s" %d %d%s' % (n, name, lo, hi, (" " + " ".join(opts)) if opts else "")))

    # Grouped, each group whole (some run past a screenful: the list scrolls).
    order = []
    for g, _ in rows:
        if g not in order:
            order.append(g)
    for g in order:
        lines = [l for gg, l in rows if gg == g]
        for k in range(0, len(lines), 24):
            w('group "%s%s"' % (g, "" if k == 0 else " %d" % (k // 24 + 1)))
            for l in lines[k:k + 24]:
                w(l)
    print("\n".join(out))


if __name__ == "__main__":
    main()
