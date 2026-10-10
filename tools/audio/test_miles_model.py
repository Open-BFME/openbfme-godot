"""Lane AUDIO-5: the Python reference model (miles_model.py) gives the numbers engine/tests/test_audio5.cpp pins for the C++ port."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import miles_model as mm  # noqa: E402

TEST_CPP = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'engine', 'tests', 'test_audio5.cpp')


def test_microphone_of_the_default_camera():
    cam, look = mm.default_camera()
    mic, face = mm.microphone(cam, look)
    assert abs(mic[1] - 938.815356) < 1e-5 and abs(mic[2] - 117.371571) < 1e-5
    assert face[0] == 0 and face[1] == 1.0


def test_the_cpp_pins_are_the_models_numbers():
    cam, look = mm.default_camera()
    mic, face = mm.microphone(cam, look)
    src = open(TEST_CPP).read()
    pins = re.findall(r'checkGains\(milesFast2DGains\(([0-9.]+)f, kMic, kNorth, Coord3D\{ ([0-9.]+), ([0-9.]+), 0 \}, 1000\.0f\), ([0-9.]+), ([0-9.]+)\);', src)
    assert len(pins) >= 8
    for vol, x, y, left, right in pins:
        gl, gr = mm.fast2d_gains(float(vol), mic, face, (float(x), float(y), 0.0), 1000.0)
        assert abs(gl - float(left)) < 1e-5 and abs(gr - float(right)) < 1e-5


def test_the_old_model_rotated_the_stereo_image():
    cam, look = mm.default_camera()
    # before AUDIO-5: screen left / right came out centred, up / down the screen hard left / right
    assert mm.old_gains(1.0, look, 0.0, (750, 1000, 0)) == (1.0, 1.0)
    assert mm.old_gains(1.0, look, 0.0, (1000, 1300, 0)) == (2.0, 0.0)
    assert mm.old_gains(1.0, look, 0.0, (1000, 800, 0)) == (0.0, 2.0)


def test_the_cpp_zoom_pins_are_the_models_numbers():
    src = open(TEST_CPP).read()
    rows = re.findall(r'\{ ([0-9.]+)f, ([0-9.]+)f, ([0-9.]+) \},', src)
    assert len(rows) == 6
    for eye_y, eye_z, zoom in rows:
        cam, look = mm.default_camera(height=float(eye_z))
        assert abs(cam[1] - float(eye_y)) < 1e-5
        mic, _ = mm.microphone(cam, look)
        assert abs(mm.zoom_volume(cam, mic) - float(zoom)) < 1e-5
