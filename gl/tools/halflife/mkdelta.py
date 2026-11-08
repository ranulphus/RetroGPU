#!/usr/bin/env python3
"""Write tools/halflife/valve/delta.lst: the network field encodings the
engine needs (Delta_InitFields) and Half-Life's WON 1.0.0.9 data predates.

  mkdelta.py [--engine PATH/TO/engine/common/net_encode.c]

The field names come from the engine's own tables (net_encode.c, default in
~/xash3d-fwgs-dos); every one must have a rule below, or nothing is written.
The encodings are ours, sized from each field's C type and range: positions
to 1/8 or 1/32 unit, angles to 16 bits, flag words and generic user fields
at full width. Out-of-range values clamp (Delta_ClampIntegerField), and a
local game sends everything through the loopback, so width only costs
bandwidth. Encoders: the ones hlsdk's server registers (Entity_Encode,
Player_Encode, Custom_Encode clear the fields an entity does not use);
none elsewhere (every changed field is sent).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "valve", "delta.lst")

# The engine's struct names and the field tables they use (dt_info in net_encode.c).
STRUCTS = [("event_t", "ev_fields"), ("movevars_t", "pm_fields"), ("usercmd_t", "cmd_fields"),
           ("clientdata_t", "cd_fields"), ("weapon_data_t", "wd_fields"), ("entity_state_t", "ent_fields"),
           ("entity_state_player_t", "ent_fields"), ("custom_entity_state_t", "ent_fields")]
# The encoders hlsdk's server registers (dlls/client.cpp, RegisterEncoders).
ENCODERS = {"entity_state_t": "gamedll Entity_Encode", "entity_state_player_t": "gamedll Player_Encode",
            "custom_entity_state_t": "gamedll Custom_Encode"}

POS = ("DT_SIGNED | DT_FLOAT", 21, 8.0)          # map coordinates, +-131072/8 = +-16384 units at 1/8
POS_FINE = ("DT_SIGNED | DT_FLOAT", 24, 32.0)    # the local player: prediction wants finer steps
VEL = ("DT_SIGNED | DT_FLOAT", 18, 8.0)          # +-16384 units/s at 1/8
ANG = ("DT_ANGLE", 16, 1.0)
UNIT = ("DT_SIGNED | DT_FLOAT", 16, 32767.0)     # -1..1 (normalised vectors)
FRAC = ("DT_FLOAT", 16, 32767.0)                 # 0..1
IUSER = ("DT_SIGNED | DT_INTEGER", 32, 1.0)
FUSER = ("DT_SIGNED | DT_FLOAT", 32, 256.0)
VUSER = POS
TIME_REL = ("DT_SIGNED | DT_FLOAT", 32, 1000.0)  # weapon timers: seconds, often small or negative, to the ms
ENT = ("DT_INTEGER", 16, 1.0)                    # entity numbers
BIT = ("DT_INTEGER", 1, 1.0)
BYTE8 = ("DT_INTEGER", 8, 1.0)
FLAGS = ("DT_INTEGER", 32, 1.0)


def rules():
    r = {}

    def xyz(prefix, spec, fmt="%s[%d]"):
        for i in range(3):
            r[fmt % (prefix, i)] = spec

    # event_t (event_args_t)
    r["event_t"] = e = {"flags": BYTE8, "entindex": ENT, "ducking": BIT, "fparam1": ("DT_SIGNED | DT_FLOAT", 24, 1000.0),
                        "fparam2": ("DT_SIGNED | DT_FLOAT", 24, 1000.0), "iparam1": IUSER, "iparam2": IUSER,
                        "bparam1": BIT, "bparam2": BIT}
    for i in range(3):
        e["origin[%d]" % i] = POS
        e["angles[%d]" % i] = ANG
        e["velocity[%d]" % i] = VEL

    # movevars_t
    r["movevars_t"] = m = {"gravity": ("DT_SIGNED | DT_FLOAT", 16, 1.0), "stopspeed": ("DT_FLOAT", 16, 1.0),
                           "maxspeed": ("DT_FLOAT", 16, 1.0), "spectatormaxspeed": ("DT_FLOAT", 16, 1.0),
                           "accelerate": ("DT_FLOAT", 16, 100.0), "airaccelerate": ("DT_FLOAT", 16, 100.0),
                           "wateraccelerate": ("DT_FLOAT", 16, 100.0), "friction": ("DT_FLOAT", 16, 100.0),
                           "edgefriction": ("DT_FLOAT", 16, 100.0), "waterfriction": ("DT_FLOAT", 16, 100.0),
                           "bounce": ("DT_FLOAT", 16, 100.0), "stepsize": ("DT_FLOAT", 16, 16.0),
                           "maxvelocity": ("DT_FLOAT", 16, 1.0), "zmax": ("DT_FLOAT", 18, 1.0),
                           "waveHeight": ("DT_FLOAT", 16, 16.0), "footsteps": BIT, "skyName": ("DT_STRING", 1, 1.0),
                           "rollangle": ("DT_SIGNED | DT_FLOAT", 16, 256.0), "rollspeed": ("DT_FLOAT", 16, 1.0),
                           "fog_settings": FLAGS, "wateralpha": FRAC, "skyangle": ("DT_SIGNED | DT_FLOAT", 16, 64.0)}
    for c in "rgb":
        m["skycolor_" + c] = ("DT_FLOAT", 12, 1.0)
    for c in "xyz":
        m["skyvec_" + c] = UNIT
        m["skydir_" + c] = UNIT

    # usercmd_t
    r["usercmd_t"] = u = {"lerp_msec": ("DT_SHORT", 9, 1.0), "msec": ("DT_BYTE", 8, 1.0),
                          "forwardmove": ("DT_SIGNED | DT_FLOAT", 12, 1.0), "sidemove": ("DT_SIGNED | DT_FLOAT", 12, 1.0),
                          "upmove": ("DT_SIGNED | DT_FLOAT", 12, 1.0), "lightlevel": ("DT_BYTE", 8, 1.0),
                          "buttons": ("DT_SHORT", 16, 1.0), "impulse": ("DT_BYTE", 8, 1.0),
                          "weaponselect": ("DT_BYTE", 8, 1.0), "impact_index": ("DT_INTEGER", 6, 1.0)}
    for i in range(3):
        u["viewangles[%d]" % i] = ANG
        u["impact_position[%d]" % i] = POS

    # clientdata_t
    r["clientdata_t"] = c = {"viewmodel": ("DT_INTEGER", 12, 1.0), "flags": FLAGS, "waterlevel": ("DT_INTEGER", 2, 1.0),
                             "watertype": ("DT_SIGNED | DT_INTEGER", 8, 1.0), "health": ("DT_SIGNED | DT_FLOAT", 16, 1.0),
                             "bInDuck": BIT, "weapons": FLAGS, "flTimeStepSound": ("DT_SIGNED | DT_INTEGER", 16, 1.0),
                             "flDuckTime": ("DT_SIGNED | DT_INTEGER", 16, 1.0), "flSwimTime": ("DT_SIGNED | DT_INTEGER", 16, 1.0),
                             "waterjumptime": ("DT_SIGNED | DT_INTEGER", 16, 1.0), "maxspeed": ("DT_FLOAT", 16, 1.0),
                             "fov": ("DT_FLOAT", 8, 1.0), "weaponanim": BYTE8, "m_iId": BYTE8,
                             "ammo_shells": ("DT_SIGNED | DT_INTEGER", 16, 1.0), "ammo_nails": ("DT_SIGNED | DT_INTEGER", 16, 1.0),
                             "ammo_cells": ("DT_SIGNED | DT_INTEGER", 16, 1.0), "ammo_rockets": ("DT_SIGNED | DT_INTEGER", 16, 1.0),
                             "m_flNextAttack": TIME_REL, "tfstate": BYTE8, "pushmsec": ("DT_INTEGER", 16, 1.0),
                             "deadflag": ("DT_INTEGER", 3, 1.0), "physinfo": ("DT_STRING", 1, 1.0)}
    for i in range(3):
        c["origin[%d]" % i] = POS_FINE
        c["velocity[%d]" % i] = ("DT_SIGNED | DT_FLOAT", 24, 32.0)
        c["punchangle[%d]" % i] = ("DT_SIGNED | DT_FLOAT", 16, 256.0)
        c["view_ofs[%d]" % i] = ("DT_SIGNED | DT_FLOAT", 16, 256.0)
    user_fields(c)

    # weapon_data_t
    r["weapon_data_t"] = w = {"m_iId": BYTE8, "m_iClip": ("DT_SIGNED | DT_INTEGER", 16, 1.0),
                              "m_fInReload": BIT, "m_fInSpecialReload": ("DT_INTEGER", 2, 1.0), "m_fInZoom": BYTE8,
                              "m_iWeaponState": BYTE8}
    for f in ("m_flNextPrimaryAttack", "m_flNextSecondaryAttack", "m_flTimeWeaponIdle", "m_flNextReload",
              "m_flPumpTime", "m_fReloadTime", "m_fAimedDamage", "m_fNextAimBonus"):
        w[f] = TIME_REL
    for i in range(1, 5):
        w["iuser%d" % i] = IUSER
        w["fuser%d" % i] = FUSER

    # entity_state_t, and the same fields for players and custom entities (beams)
    ent = {"entityType": ("DT_INTEGER", 2, 1.0), "modelindex": ("DT_INTEGER", 12, 1.0), "sequence": ("DT_INTEGER", 10, 1.0),
           "frame": ("DT_FLOAT", 20, 256.0), "colormap": ("DT_INTEGER", 16, 1.0), "skin": ("DT_SHORT | DT_SIGNED", 16, 1.0),
           "solid": ("DT_SHORT", 4, 1.0), "effects": FLAGS, "scale": ("DT_FLOAT", 16, 256.0), "eflags": ("DT_BYTE", 8, 1.0),
           "rendermode": BYTE8, "renderamt": BYTE8, "renderfx": BYTE8, "movetype": ("DT_INTEGER", 8, 1.0),
           "animtime": ("DT_TIMEWINDOW_8", 8, 1.0), "framerate": ("DT_SIGNED | DT_FLOAT", 16, 256.0),
           "body": ("DT_INTEGER", 16, 1.0), "aiment": ENT, "owner": ENT, "friction": ("DT_SIGNED | DT_FLOAT", 16, 256.0),
           "gravity": ("DT_SIGNED | DT_FLOAT", 16, 256.0), "team": ("DT_INTEGER", 8, 1.0), "playerclass": ("DT_INTEGER", 8, 1.0),
           "health": ("DT_SIGNED | DT_INTEGER", 16, 1.0), "spectator": BIT, "weaponmodel": ("DT_INTEGER", 12, 1.0),
           "gaitsequence": ("DT_INTEGER", 10, 1.0), "usehull": BIT, "oldbuttons": ("DT_INTEGER", 16, 1.0),
           "onground": BIT, "iStepLeft": BIT, "flFallVelocity": ("DT_SIGNED | DT_FLOAT", 16, 1.0),
           "fov": ("DT_FLOAT", 8, 1.0), "weaponanim": BYTE8, "impacttime": ("DT_TIMEWINDOW_BIG", 16, 100.0),
           "starttime": ("DT_TIMEWINDOW_BIG", 16, 100.0)}
    for c3 in "rgb":
        ent["rendercolor." + c3] = ("DT_BYTE", 8, 1.0)
    for i in range(4):
        ent["controller[%d]" % i] = ("DT_BYTE", 8, 1.0)
        ent["blending[%d]" % i] = ("DT_BYTE", 8, 1.0)
    for i in range(3):
        ent["origin[%d]" % i] = POS
        ent["angles[%d]" % i] = ANG
        ent["velocity[%d]" % i] = VEL
        ent["basevelocity[%d]" % i] = VEL
        ent["mins[%d]" % i] = POS
        ent["maxs[%d]" % i] = POS
        ent["startpos[%d]" % i] = POS
        ent["endpos[%d]" % i] = POS
    user_fields(ent)
    for s in ("entity_state_t", "entity_state_player_t", "custom_entity_state_t"):
        r[s] = ent
    return r


def user_fields(d):
    for i in range(1, 5):
        d["iuser%d" % i] = IUSER
        d["fuser%d" % i] = FUSER
        for j in range(3):
            d["vuser%d[%d]" % (i, j)] = VUSER


def engine_fields(src):
    """{table name: [field names]} from the *_DEF( name ) and *_DEF_( name, member ) entries."""
    text = open(src).read()
    out = {}
    for m in re.finditer(r"static const delta_field_t (\w+)\[\]\s*=\s*\{(.*?)\n\};", text, re.S):
        names = []
        for f in re.finditer(r"\{\s*\w+_DEF_?\(\s*([^,)]+?)\s*[,)]", m.group(2)):
            names.append(f.group(1).strip())
        out[m.group(1)] = names
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", default=os.path.expanduser("~/xash3d-fwgs-dos/engine/common/net_encode.c"))
    a = ap.parse_args()
    fields = engine_fields(a.engine)
    r = rules()
    missing = []
    lines = ["// delta.lst for Xash3D FWGS on DOS-GL, written by tools/halflife/mkdelta.py (DOS-GL's own",
             "// encodings; see there). Half-Life's WON 1.0.0.9 data has no delta.lst; later versions' one is",
             "// used instead when the data carries it.", ""]
    for struct, table in STRUCTS:
        names = fields.get(table)
        if not names:
            raise SystemExit("mkdelta: no %s table in %s" % (table, a.engine))
        lines += ["%s %s" % (struct, ENCODERS.get(struct, "none")), "{"]
        for n in names:
            spec = r[struct].get(n)
            if not spec:
                missing.append("%s.%s" % (struct, n))
                continue
            flags, bits, mul = spec
            lines.append("\tDEFINE_DELTA( %s, %s, %d, %s )," % (n, flags, bits, repr(float(mul))))
        lines[-1] = lines[-1].rstrip(",")
        lines += ["}", ""]
    if missing:
        raise SystemExit("mkdelta: no rule for %s" % ", ".join(missing))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\r\n") as f:
        f.write("\n".join(lines))
    print("mkdelta: %s (%d structs)" % (OUT, len(STRUCTS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
