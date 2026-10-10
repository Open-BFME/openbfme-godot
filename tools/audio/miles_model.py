"""Reference model of retail RotWK 2.01's stereo image (lane AUDIO-5), independent of the C++ port (engine/src/Common/Audio/MilesMix.cpp).

Read from the binaries RotWK ships (MSS 6.6g); see MilesMix.h for every address:
  * microphone: RW MilesAudioManager::recalculateMicrophone 0x45235B (AudioSettings.ini Microphone* fields of the view);
  * 3D samples: msssoft.m3d "Miles Fast 2D Positional Audio" 0x22401060 (pan = acos(n . right) / pi, volume ^ (5/3), x0.75 behind);
  * 2D samples / streams: mss32 AIL_set_sample_volume_pan 0x2112AD60 (centre: volume ^ (5/3) x 2^-0.3 per channel).
Also the port's model before AUDIO-5 (`old_gains`): the listener on the ground at the camera target, `forward` (cos a, sin a), the pan
the dot of the horizontal direction with (forward.y, -forward.x), played through Godot's AudioEffectPanner at the linear volume.
"""
import math

VOLUME_EXP = 1.6666666269302368
PAN_EXP = 0.30000001192092896
CENTRE = 0.8122522234916687
INV_PI = 0.31830987334251404

# AudioSettings.ini (tactical view): MicrophonePreferredFractionCameraToGround 86%, Min 100, Max 300, Pull 60%
TACTICAL_MIC = dict(fraction=0.86, min=100.0, max=300.0, pull=0.60, zoom_min=130.0, zoom_max=425.0, zoom_amount=0.20)


def microphone(cam, look, mic=TACTICAL_MIC, look_valid=True, face=(0.0, 1.0, 0.0)):
    d = [cam[i] - look[i] for i in range(3)]
    l2 = d[0] ** 2 + d[1] ** 2 + d[2] ** 2
    f2 = mic['fraction'] ** 2
    if f2 * l2 >= mic['max'] ** 2:
        s = mic['max'] / math.sqrt(l2)
    elif mic['min'] ** 2 >= l2:
        s = 1.0
    elif mic['min'] ** 2 >= f2 * l2:
        s = mic['min'] / math.sqrt(l2)
    else:
        s = mic['fraction']
    m = [cam[i] - d[i] * s for i in range(3)]
    if look_valid:
        m[0] += (look[0] - m[0]) * mic['pull']
        m[1] += (look[1] - m[1]) * mic['pull']
    h = math.hypot(d[0], d[1])
    if h > 0:
        face = (-d[0] / h, -d[1] / h, 0.0)
    return tuple(m), face


def zoom_volume(cam, mic_pos, mic=TACTICAL_MIC):
    """RW 0x451946: the multiplier of positional volume from the camera's distance to the microphone (ZoomMin/MaxDistance, amount)."""
    e2 = sum((cam[i] - mic_pos[i]) ** 2 for i in range(3))
    if mic['zoom_min'] ** 2 > e2:
        return 1.0
    if mic['zoom_max'] ** 2 > e2:
        return 1.0 - (math.sqrt(e2) - mic['zoom_min']) / (mic['zoom_max'] - mic['zoom_min']) * mic['zoom_amount']
    return 1.0 - mic['zoom_amount']


def fast2d_gains(volume, listener, face, sound, max_distance):
    dx, dy, dz = sound[0] - listener[0], sound[1] - listener[1], -(sound[2] - listener[2])
    dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    if dist > max_distance:
        return 0.0, 0.0
    v = volume ** VOLUME_EXP if volume > 0 else 0.0
    n = (dx / dist, dy / dist, dz / dist) if dist != 0 else (1.0, 0.0, 0.0)
    fx, fy, fz = face[0], face[1], -face[2]
    front = fx * n[0] + fy * n[1] + fz * n[2]
    p = 0.5
    if dist > 0.0001:
        side = max(-1.0, min(1.0, fy * n[0] - fx * n[1]))
        p = math.acos(side) * INV_PI
    if front < 0:
        v *= 0.75
    return p * v, (1 - p) * v


def sample_gains(volume, pan=0.5):
    v = volume ** VOLUME_EXP if volume > 0 else 0.0
    if pan == 0.5:
        return v * CENTRE, v * CENTRE
    return (1 - pan) ** PAN_EXP * v, pan ** PAN_EXP * v


def old_gains(volume, target, angle, sound):
    """The port before AUDIO-5: game.gd's listener on the ground at the camera target with forward (cos a, sin a), AudioManager::panFor
    (the horizontal direction dotted with (forward.y, -forward.x)) and Godot's AudioEffectPanner on a mono voice at the linear volume."""
    fx, fy = math.cos(angle), math.sin(angle)
    dx, dy = sound[0] - target[0], sound[1] - target[1]
    ln = math.hypot(dx, dy)
    pan = 0.0 if ln < 1e-3 else max(-1.0, min(1.0, (dx * fy - dy * fx) / ln))
    lvol, rvol = min(1.0, max(0.0, 1.0 - pan)), min(1.0, max(0.0, 1.0 + pan))
    return volume * (lvol + 1.0 - rvol), volume * (rvol + 1.0 - lvol)


def default_camera(target=(1000.0, 1000.0, 0.0), angle=0.0, height=300.0, pitch_deg=37.5):
    """RotWK's default tactical framing (camera offset z = 300, y = -z / tan(37.5 deg), RW 0x5011D5), heading `angle` (TacticalCamera::buildPose)."""
    back = height / math.tan(math.radians(pitch_deg))
    ex = target[0] + back * math.sin(angle)
    ey = target[1] - back * math.cos(angle)
    return (ex, ey, target[2] + height), target
