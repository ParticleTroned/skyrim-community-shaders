"""Check production history-address arithmetic against projected scene motion.

This evaluates the producer's NDC-to-motion conversion and the consumer's
history-address expressions. It is not shader compilation or GPU validation;
matrix uploads, texture sampling and history rejection are not simulated.
"""

import argparse
import ast
import math
from pathlib import Path
import re
import unittest

from extract_adaptive_balance_toggle import function


ROOT = Path(__file__).resolve().parents[1]
SHADER = ROOT / "features/Upscaling/Shaders/Upscaling/PeripheryTAACS.hlsl"
MOTION_PRODUCER = ROOT / "package/Shaders/Common/MotionBlur.hlsli"


def shader_text(path):
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8-sig"), flags=re.S)


def scalar_expression(text, axis):
    text = re.sub(r"float2\(([^,()]+),([^()]+)\)", lambda match: match[axis + 1], text)
    tree = ast.parse(text.replace(".xy", "").strip(), mode="eval")
    allowed = (ast.Expression, ast.BinOp, ast.UnaryOp, ast.Name, ast.Load,
               ast.Constant, ast.Add, ast.Sub, ast.Mult, ast.Div, ast.USub)
    if any(not isinstance(node, allowed) for node in ast.walk(tree)):
        raise ValueError(f"Unsupported shader arithmetic: {text}")
    return compile(tree, "<shader expression>", "eval")


def expression(source, variable):
    assignments = re.findall(rf"\bfloat2\s+{variable}\s*=\s*([^;]+);", source)
    if len(assignments) != 1:
        raise ValueError(f"Expected one production assignment for {variable}")
    return tuple(scalar_expression(assignments[0], axis) for axis in (0, 1))


def evaluate(expressions, axis, values):
    return eval(expressions[axis], {"__builtins__": {}}, values)


def project(point, camera_origin=(0.0, 0.0, 0.0), yaw=0.0,
            projection=(1.0, 1.0, 0.0, 0.0)):
    x, y, z = (p - origin for p, origin in zip(point, camera_origin))
    angle = math.radians(yaw)
    view_x = math.cos(angle) * x + math.sin(angle) * z
    view_z = -math.sin(angle) * x + math.cos(angle) * z
    return (view_x / view_z * projection[0] + projection[2],
            y / view_z * projection[1] + projection[3])


def to_uv(ndc):
    return (0.5 + ndc[0] * 0.5, 0.5 - ndc[1] * 0.5)


class PeripheryTAAMotionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = function(shader_text(SHADER), "void main(")
        cls.history_velocity = expression(source, "historyVelocity")
        cls.history_uv = expression(source, "historyUV")
        producer = function(shader_text(MOTION_PRODUCER), "float2 GetSSMotionVector(")
        returns = re.findall(r"\breturn\s+([^;]+);", producer)
        if len(returns) != 1:
            raise ValueError("Expected one motion producer return expression")
        cls.motion = tuple(scalar_expression(returns[0], axis) for axis in (0, 1))

    def history_address(self, axis, output_uv, motion, camera_motion):
        values = {"outputUV": output_uv, "currentVelocity": motion,
                  "hmdHistoryDeltaLookup": camera_motion}
        values["historyVelocity"] = evaluate(self.history_velocity, axis, values)
        return evaluate(self.history_uv, axis, values)

    def check_scene(self, previous_origin=(0.0, 0.0, 0.0), previous_yaw=0.0,
                    object_displacement=(0.0, 0.0, 0.0), output_offset=(0.0, 0.0),
                    camera_depth_valid=True, asymmetric_projection=False):
        for eye_offset in (-0.032, 0.032):
            for point_x in (-2.5, 2.5):
                with self.subTest(eye=eye_offset, x=point_x):
                    point = (point_x, 0.4, 4.0)
                    previous_point = tuple(p - d for p, d in zip(point, object_displacement))
                    current_eye = (eye_offset, 0.0, 0.0)
                    previous_eye = (previous_origin[0] + eye_offset, *previous_origin[1:])
                    projection = ((1.2, 0.8, eye_offset * 2, -0.03)
                                  if asymmetric_projection else (1.0, 1.0, 0.0, 0.0))
                    current_ndc = project(point, current_eye, projection=projection)
                    previous_ndc = project(previous_point, previous_eye, previous_yaw, projection)
                    current_uv, previous_uv = to_uv(current_ndc), to_uv(previous_ndc)
                    camera_only_uv = to_uv(project(point, previous_eye, previous_yaw, projection))
                    for axis in (0, 1):
                        motion = evaluate(self.motion, axis, {
                            "screenPosition": current_ndc[axis],
                            "previousScreenPosition": previous_ndc[axis],
                        })
                        camera_motion = (camera_only_uv[axis] - current_uv[axis]
                                         if camera_depth_valid else 0.0)
                        actual = self.history_address(axis, current_uv[axis] + output_offset[axis],
                                                      motion, camera_motion)
                        self.assertAlmostEqual(actual, previous_uv[axis] + output_offset[axis], places=12)

    def test_head_rotation_counts_once(self):
        self.check_scene(previous_yaw=1.0)
        self.check_scene(previous_yaw=-1.0)

    def test_camera_translation_counts_once(self):
        for origin in ((0.05, 0.0, 0.0), (0.0, 0.05, 0.0), (0.0, 0.0, 0.05)):
            with self.subTest(origin=origin):
                self.check_scene(previous_origin=origin)

    def test_object_motion_with_still_camera_is_preserved(self):
        self.check_scene(object_displacement=(0.2, -0.1, 0.05))

    def test_combined_camera_and_object_motion_is_preserved(self):
        self.check_scene(previous_origin=(0.05, 0.0, 0.0), previous_yaw=1.0,
                         object_displacement=(0.2, -0.1, 0.05))

    def test_object_following_camera_has_no_residual_motion(self):
        self.check_scene(previous_origin=(-0.2, 0.1, 0.0),
                         object_displacement=(0.2, -0.1, 0.0))

    def test_stationary_scene_stays_stationary(self):
        self.check_scene()

    def test_subpixel_output_offsets_preserve_motion(self):
        for offset in ((0.25 / 2048, -0.5 / 2048), (-0.25 / 2048, 0.5 / 2048)):
            with self.subTest(offset=offset):
                self.check_scene(previous_yaw=1.0, output_offset=offset)

    def test_unavailable_camera_depth_preserves_geometry_motion(self):
        self.check_scene(previous_yaw=1.0, object_displacement=(0.2, 0.0, 0.0),
                         camera_depth_valid=False)

    def test_asymmetric_eye_projections_preserve_motion(self):
        self.check_scene(previous_origin=(0.05, 0.01, 0.02), previous_yaw=-1.0,
                         object_displacement=(0.1, -0.2, 0.0), asymmetric_projection=True)

    def test_motion_is_in_full_eye_uv_at_different_render_scales(self):
        for width, height, render_scale in ((2048, 2048, 1.0), (2448, 2448, 0.67), (2160, 1920, 0.5)):
            for axis, dimension in enumerate((width, height)):
                current_ndc, previous_ndc = 0.2, 0.24
                motion = evaluate(self.motion, axis, {
                    "screenPosition": current_ndc, "previousScreenPosition": previous_ndc,
                })
                expected_pixels = (previous_ndc - current_ndc) * dimension * (0.5 if axis == 0 else -0.5)
                for eye_uv in (0.2, 0.8):
                    actual_uv = self.history_address(axis, eye_uv, motion, motion)
                    self.assertAlmostEqual((actual_uv - eye_uv) * dimension, expected_pixels, places=10)
                    input_pixels = motion * dimension * render_scale
                    self.assertAlmostEqual((actual_uv - eye_uv) * dimension * render_scale,
                                           input_pixels, places=10)

    def test_encoder_attenuation_does_not_reintroduce_camera_motion(self):
        for attenuation in (0.0, 0.25, 1.0):
            for axis, camera_motion in enumerate((0.01, -0.02)):
                encoded_motion = attenuation * camera_motion
                actual = self.history_address(axis, 0.2, encoded_motion, camera_motion)
                self.assertAlmostEqual(actual, 0.2 + encoded_motion, places=12)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--shader", type=Path, default=SHADER)
    parser.add_argument("--motion-producer", type=Path, default=MOTION_PRODUCER)
    args, remaining = parser.parse_known_args()
    SHADER = args.shader
    MOTION_PRODUCER = args.motion_producer
    unittest.main(argv=[__file__, *remaining])
