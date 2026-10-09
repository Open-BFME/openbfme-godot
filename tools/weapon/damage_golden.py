#!/usr/bin/env python3
"""Reference model of the RotWK deterministic damage core, with golden vectors (lane WEAPON-1 research).

Every function mirrors the instruction order of the cited RotWK game.dat routine (caveat S-001: community-patched
image; the armor-modifier stacking sites 0x5D8A64 / 0x5D8AF1 are patched, this model implements the CLEAN
behaviour, which is the call to 0x68C818 -> 0x804F39: an additive sum). float32 arithmetic is emulated by rounding
a double result to float32 after every operation, which is exact for + - * / (double rounding is harmless for a
24-bit significand computed in a 53-bit one). x87 sequences are modelled the same way because the game runs the
x87 at 24-bit precision (setFPMode, RW 0x440809) with a wide exponent; the only difference would be subnormals.

Nothing here reads game files. Run `python3 tools/weapon/damage_golden.py` to print the vectors and check them.
"""
from __future__ import annotations

import math
import struct


def f32(x: float) -> float:
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def add(a, b): return f32(a + b)
def sub(a, b): return f32(a - b)
def mul(a, b): return f32(a * b)
def div(a, b): return f32(a / b)


ONE = 1.0
HUNDREDTH = f32(0.01)  # [0xBE5600] = 0x3C23D70A


# ---- INI percent: scanPercentToReal 0x42EB18 = fld parsed float32 ; fmul 0.01f (one rounding at 24 bits) ----------
def percent(text: str) -> float:
    return mul(f32(float(text.rstrip("%"))), HUNDREDTH)


# ---- damage / death type indices (list 0xD9DA08 etc.) -------------------------------------------------------------
FORCE, CRUSH, SLASH, PIERCE, SIEGE, STRUCTURAL, FLAME, HEALING, UNRESISTABLE = range(9)
MAGIC, LOGICAL_FIRE = 15, 23


# ---- ArmorStore / ArmorTemplate: RW 0x5D893C (adjustDamage; args: armor name, DamageInfoInput*, victim Object*, noFlank) ---
def adjust_damage(dtype, amount, *, coef=None, scalar=None, flank_pen=0.0, flanked=False, invulnerable=False,
                  armor_mod_sum=None, armor_mod_cap=0.75):
    """coef: ArmorTemplate coefficient for dtype (None = no template found -> 1.0).
    scalar: DamageScalar (+0x74) of the template of the victim's current ArmorSet entry (None -> 1.0).
    flank_pen: ArmorTemplate +0x00 FlankedPenalty; applied only when `flanked` (victim isFlankedBy attacker, RW 0x68FB63).
    armor_mod_sum: sum of active ARMOR (type 1) attribute modifiers whose name list admits the damage type name.
    armor_mod_cap: GlobalData+0xAE8 AttributeModifierArmorMaxBonus."""
    if dtype == HEALING:  # 0x5D8950: cmp eax,7 ; returns the input amount unchanged
        return amount
    s = ONE if scalar is None else scalar
    f = flank_pen if flanked else 0.0
    omf = sub(ONE, f)                                   # xmm1 = 1 - flank           (0x5D8A1A)
    t = mul(sub(ONE, s), omf)                           # (1 - scalar) * (1 - flank) (0x5D8A21-0x5D8A26)
    a = mul(sub(ONE, t), amount)                        # (1 - t) * amount           (0x5D8A2A-0x5D8A2E)
    if dtype == UNRESISTABLE:                           # 0x5D8A3D: je 0x5D8B42 (returns a)
        return a
    if invulnerable:                                    # AttributeModifier INVULNERABLE (27) for this damage type name
        return 0.0
    c = ONE if coef is None else coef                   # 0x5D8A93 / 0x5D8A9B
    a = mul(sub(ONE, mul(sub(ONE, c), omf)), a)         # 0x5D8AA7-0x5D8AC2
    if dtype == FORCE:                                  # 0x5D8ACC: je 0x5D8B42
        return a
    out = 0.0 if armor_mod_sum is None else armor_mod_sum
    x = armor_mod_cap if armor_mod_cap <= out else out  # comiss cap,out ; jbe -> cap   (0x5D8B10-0x5D8B16)
    inner = sub(ONE, x)                                 # xmm1 = 1 - x
    t3 = mul(sub(ONE, inner), omf)                      # (1 - (1 - x)) * (1 - flank)
    return mul(sub(ONE, t3), a)                         # 0x5D8B2F-0x5D8B3D


def sum_modifiers(values):
    """0x804F39: out = 0 ; for each active modifier: out = out + value (addss, vector order)."""
    out = 0.0
    for v in values:
        out = add(out, v)
    return out


def product_modifiers(values):
    """0x804FFF: out = 1.0 ; for each active modifier: out = out * value (mulss, vector order)."""
    out = ONE
    for v in values:
        out = mul(out, v)
    return out


# ---- ActiveBody::attemptDamage RW 0x8C3FA3 / internalChangeHealth 0x8C31A5 -----------------------------------------
def body_damage(health, max_health, amount_adjusted, dtype, *, kill=False, body_scalar=1.0):
    """amount_adjusted = adjust_damage(...). Returns (health_after, actualDamageDealt, actualDamageClipped) or None
    when the 'amount > 0 || kill' gate (0x8C40F0-0x8C40FD) skips the health change."""
    amount = amount_adjusted
    if dtype != UNRESISTABLE:                           # 0x8C4077-0x8C4086: mulss [esi+4]
        amount = mul(amount, body_scalar)
    if not (amount > 0.0 or kill):
        return None
    if kill:                                            # 0x8C4218-0x8C4229: amount = current health
        amount = health
    prev = health
    delta = sub(0.0, amount)                            # 0x8C4244: xorps ; subss
    h = add(delta, health)                              # 0x8C31B9: delta + health (addss)
    if h > max_health:                                  # comiss ; jbe
        h = max_health
    if 0.0 > h:                                         # 0x8C3264: comiss 0,health ; jbe -> else health = 0
        h = 0.0
    dealt = amount                                      # 0x8C427B: D+0x70 = amount (NOT clipped)
    clipped = sub(prev, h)                              # 0x8C4285: prev - current  (D+0x74)
    return h, dealt, clipped


# ---- DamageNugget::fillDamageInfo RW 0x90E28C -----------------------------------------------------------------------
def taper(damage, taper_off, radius, min_radius, dist):
    """DamageTaperOff != -1 and a centre point: x87 fld/fsub/fdiv/fmul/fsubr at 24-bit precision, one rounding per op."""
    d = min_radius if min_radius > dist else (radius if dist > radius else dist)   # 0x40524B clamp(min, dist, R)
    num = sub(d, min_radius)
    den = sub(radius, min_radius)
    q = div(num, den)
    return sub(damage, mul(q, sub(damage, taper_off)))


def passenger_scale(count, max_passengers):
    q = div(float(count), float(max_passengers))        # fild ; fidiv ; fstp dword
    return ONE if q > ONE else q                        # min(q, 1.0): fcompi ; ja -> 1.0 only when q > 1


def nugget_amount(damage, *, taper_args=None, passengers=None, scalar_match=None, damage_add=None, damage_mult=None,
                  flanking_bonus=None, flanked_scalar=None, victim_flanked=False, source_flanked=False):
    """Order of operations in 0x90E28C (A = D+0x20):
    1 taper-off, 2 PassengerProportionalAttack, 3 first matching DamageScalar filter, 4 + DAMAGE_ADD sum (if A != 0),
    5 * DAMAGE_MULT (SPELL_DAMAGE for MAGIC) product, 6 flank: * (FlankingBonus + 1) then * FlankedScalar (< 1.0 -> no damage).
    Returns None when step 6 drops the hit."""
    a = damage if taper_args is None else taper(damage, *taper_args)
    if passengers is not None:
        a = mul(a, passenger_scale(*passengers))
    if scalar_match is not None:
        a = mul(scalar_match, a)                        # mulss xmm0=scalar_entry ; [edi+0x20]
    if damage_add is not None and a != 0.0:
        a = add(a, damage_add)                          # 0x90E47F-0x90E489: A + out
    if damage_mult is not None:
        a = mul(a, damage_mult)                         # 0x90E4E9-0x90E4F3
    if victim_flanked and flanking_bonus is not None:
        a = mul(add(flanking_bonus, ONE), a)            # 0x90E586-0x90E59B
    if source_flanked and flanked_scalar is not None:
        a = mul(flanked_scalar, a)                      # 0x90E5AF-0x90E5B7
        if ONE > a:                                     # comiss 1.0, a ; jbe continue ; else return false
            return None
    return a


# ---- WeaponBonus: RW 0x6CA434 (append), 0x6CA7AC (computeBonus) -----------------------------------------------------
BONUS_FIELDS = ["DAMAGE", "RADIUS", "RANGE", "RATE_OF_FIRE", "PRE_ATTACK", "FIRING"]
BONUS_CONDITIONS = ["GARRISONED", "HORDE", "CONTINUOUS_FIRE_MEAN", "CONTINUOUS_FIRE_FAST", "NATIONALISM",
                    "PLAYER_UPGRADE", "DRONE_SPOTTING", "DEMORALIZED_OBSOLETE", "ENTHUSIASTIC", "VETERAN", "ELITE",
                    "HERO", "BATTLEPLAN_BOMBARDMENT", "BATTLEPLAN_HOLDTHELINE", "BATTLEPLAN_SEARCHANDDESTROY",
                    "SUBLIMINAL", "SOLO_HUMAN_EASY", "SOLO_HUMAN_NORMAL", "SOLO_HUMAN_HARD", "SOLO_AI_EASY",
                    "SOLO_AI_NORMAL", "SOLO_AI_HARD"]


def new_set():
    return [[ONE] * 6 for _ in range(22)]  # WeaponBonusSet: 22 x {6 floats}, all 1.0 (ctor 0x64209F)


def compute_bonus(object_mask, extra_mask, global_set, template_set):
    out = [ONE] * 6
    mask = (object_mask | extra_mask) & 0xFFFFFFFF
    for st in (global_set, template_set):               # global (GlobalData+0xAD0) first, then the template's (+0xE0)
        if st is None:
            continue
        for i in range(22):                             # 0x6CA4B5: ascending bit order, 22 conditions
            if mask & (1 << i):
                for k in range(6):                      # 0x6CA434: (in - 1.0) + out, subss then addss
                    out[k] = add(sub(st[i][k], ONE), out[k])
    return out


def delay_between_shots(delay, bonus_rof, attr_rof=ONE):
    """0x6CA066: x87: q = delay / (rof * attr) (24-bit), floor (CRT, via double), store float32, fistp to int."""
    q = div(float(delay), mul(bonus_rof, attr_rof))
    return int(math.floor(q))


def clip_reload_time(reload, bonus_rof):
    """0x6CA0C0: reload -= reload % 3 ; floor(reload / rof)."""
    reload -= reload % 3
    return int(math.floor(div(float(reload), bonus_rof)))


def pre_attack_delay(frames, bonus_pre):
    """0x6CDD10 / 0x6CA123: cvtsi2ss ; mulss ; cvttss2si (truncate toward zero)."""
    return int(mul(float(frames), bonus_pre))


def attack_range(attack_range_value, bonus_range, dz=0.0, min_height=0.0, range_bonus=0.0, per_foot=0.0,
                 restricted=0.0):
    """0x6C9F5B (before the attribute RANGE factor 0x6CA61F)."""
    r = sub(mul(bonus_range, attack_range_value), 2.5)
    if not (sub(0.0, dz) < min_height):
        t = mul(add(min_height, dz), per_foot)
        r = add(sub(range_bonus, t), r)
    if restricted > 0.0 and not (abs(dz) <= restricted):
        r = 0.0
    if 0.0 > r:
        r = 0.0
    return r


def vectors():
    v = []
    # name, got, expected-bits-or-value (expected derived by hand, see damage.md)
    v.append(("normal PIERCE hit 100 vs coefficient 100%", adjust_damage(PIERCE, 100.0, coef=percent("100%")), 100.0))
    c75 = percent("75%")
    v.append(("coefficient 75% (pct parse)", c75, 0.75))
    v.append(("coefficient 75% hit", adjust_damage(PIERCE, 100.0, coef=c75), 75.0))
    c120 = percent("120%")
    v.append(("coefficient 120% parses to 0x3F999999", c120, struct.unpack("<f", struct.pack("<I", 0x3F999999))[0]))
    v.append(("coefficient 120% hit (119.99999, not 120)", adjust_damage(PIERCE, 100.0, coef=c120),
              struct.unpack("<f", struct.pack("<I", 0x42EFFFFF))[0]))
    v.append(("UNRESISTABLE ignores coefficient 0%", adjust_damage(UNRESISTABLE, 100.0, coef=0.0), 100.0))
    v.append(("UNRESISTABLE honours DamageScalar 0.5", adjust_damage(UNRESISTABLE, 100.0, coef=0.0, scalar=0.5), 50.0))
    v.append(("HEALING ignores everything", adjust_damage(HEALING, 40.0, coef=0.0, scalar=0.5, invulnerable=True), 40.0))
    v.append(("FORCE skips the ARMOR modifier stage", adjust_damage(FORCE, 100.0, coef=0.5, armor_mod_sum=0.5), 50.0))
    v.append(("PIERCE with ARMOR +50% modifier (cap 75%)", adjust_damage(PIERCE, 100.0, coef=1.0, armor_mod_sum=0.5), 50.0))
    v.append(("ARMOR modifiers 0.5+0.5 capped at 75%", adjust_damage(PIERCE, 100.0, coef=1.0,
              armor_mod_sum=sum_modifiers([0.5, 0.5])), 25.0))
    v.append(("negative ARMOR modifier (debuff -25%)", adjust_damage(PIERCE, 100.0, coef=1.0, armor_mod_sum=-0.25), 125.0))
    v.append(("invulnerable to the damage type", adjust_damage(PIERCE, 100.0, coef=1.0, invulnerable=True), 0.0))
    v.append(("flanked victim, penalty 0.5, coef 50%", adjust_damage(PIERCE, 100.0, coef=0.5, flank_pen=0.5, flanked=True), 75.0))
    v.append(("very small damage 0.001 vs 50%", adjust_damage(SLASH, 0.001, coef=0.5), f32(0.001) * 0.5))
    v.append(("negative coefficient yields negative damage (no clamp)", adjust_damage(PIERCE, 10.0, coef=-1.0), -10.0))
    # body path
    r = body_damage(500.0, 500.0, 75.0, PIERCE)
    v.append(("body 500 - 75", r, (425.0, 75.0, 75.0)))
    r = body_damage(30.0, 500.0, 75.0, PIERCE)
    v.append(("overkill: dealt 75, clipped 30, health 0", r, (0.0, 75.0, 30.0)))
    v.append(("zero damage skipped", body_damage(30.0, 500.0, 0.0, PIERCE), None))
    v.append(("body scalar 0.5 applies to PIERCE", body_damage(500.0, 500.0, 100.0, PIERCE, body_scalar=0.5), (450.0, 50.0, 50.0)))
    v.append(("body scalar skipped for UNRESISTABLE", body_damage(500.0, 500.0, 100.0, UNRESISTABLE, body_scalar=0.5), (400.0, 100.0, 100.0)))
    # nugget
    v.append(("taper 100->20 over [10,50] at 30", taper(100.0, 20.0, 50.0, 10.0, 30.0), 60.0))
    v.append(("passengers 3/4", nugget_amount(100.0, passengers=(3, 4)), 75.0))
    v.append(("DAMAGE_ADD 10 then DAMAGE_MULT 1.5", nugget_amount(50.0, damage_add=10.0, damage_mult=1.5), 90.0))
    v.append(("DAMAGE_ADD skipped when A == 0", nugget_amount(0.0, damage_add=10.0), 0.0))
    v.append(("flanking bonus 0.5", nugget_amount(100.0, flanking_bonus=0.5, victim_flanked=True), 150.0))
    v.append(("flanked scalar 0.005 drops the hit", nugget_amount(100.0, flanked_scalar=0.005, source_flanked=True), None))
    # weapon bonus
    gs = new_set()
    for cond, field, pc in (("HORDE", "RATE_OF_FIRE", "150%"), ("NATIONALISM", "RATE_OF_FIRE", "125%")):
        gs[BONUS_CONDITIONS.index(cond)][BONUS_FIELDS.index(field)] = percent(pc)
    b = compute_bonus(1 << 1 | 1 << 4, 0, gs, None)
    v.append(("HORDE+NATIONALISM ROF = 1 + 0.5 + 0.25", b[3], 1.75))
    v.append(("delay 30 / 1.75 floors to 17", delay_between_shots(30, b[3]), 17))
    v.append(("clip reload 100 -> 99 / 1.75 floors to 56", clip_reload_time(100, b[3]), 56))
    v.append(("pre-attack 15 * 0.9 truncates to 13", pre_attack_delay(15, f32(0.9)), 13))
    v.append(("attack range 100 * 1.0 - 2.5", attack_range(100.0, 1.0), 97.5))
    return v


if __name__ == "__main__":
    bad = 0
    for name, got, want in vectors():
        ok = got == want
        bad += not ok
        gs = f"{got:.9g} ({bits(got):#010x})" if isinstance(got, float) else repr(got)
        ws = f"{want:.9g} ({bits(want):#010x})" if isinstance(want, float) else repr(want)
        print(("ok   " if ok else "FAIL ") + f"{name}: got {gs} want {ws}")
    raise SystemExit(1 if bad else 0)
