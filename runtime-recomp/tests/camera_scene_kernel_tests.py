#!/usr/bin/env python3
"""scripts/camera_scene_kernel.py: the road-wheel scope hooks the generator adds to
render_3d_model and render_sprite_billboard (events 200 to 203), and nothing else."""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import camera_scene_kernel  # noqa: E402

ADDRESSES = {
    "gCurrentLightIntensity": 0x80123456,
    "gInverseViewMatrixF": 0x80ABCDEF,
    "gViewportLayout": 0x8000000A,
}
MODEL = "RECOMP_FUNC void render_3d_model(uint8_t* rdram, recomp_context* ctx) {\n    ctx->r2 = 1;\n    return;\n}\n"
SPRITE = (
    "RECOMP_FUNC void render_sprite_billboard(uint8_t* rdram, recomp_context* ctx) {\n"
    "    ctx->r2 = 1;\n    return;\n}\n"
)
SCOPE = "dkr_native_road_wheel_scope(rdram,0x1000000U,&road_call);"


def hook(name: str, body: str) -> str:
    return camera_scene_kernel.add_road_wheel_hooks(name, body, ADDRESSES)


class RoadWheelHookTests(unittest.TestCase):
    def test_model_hook_text(self) -> None:
        self.assertEqual(
            hook("render_3d_model", MODEL),
            "RECOMP_FUNC void render_3d_model(uint8_t* rdram, recomp_context* ctx) {\n"
            "    dkr_road_wheel_call road_call={0};\n"
            "    road_call.event=200;\n"
            "    road_call.object=(uint32_t)ctx->r4;\n"
            "    road_call.intensity=0x80123456U;\n"
            "    road_call.inverse_view=0x80ABCDEFU;\n"
            "    road_call.viewport_layout=0x8000000AU;\n"
            f"    {SCOPE}\n"
            "\n    ctx->r2 = 1;\n"
            f"    road_call.event=201;{SCOPE}return;\n"
            "}\n",
        )

    def test_sprite_hook_text(self) -> None:
        self.assertEqual(
            hook("render_sprite_billboard", SPRITE),
            "RECOMP_FUNC void render_sprite_billboard(uint8_t* rdram, recomp_context* ctx) {\n"
            "    dkr_road_wheel_call road_call={0};\n"
            "    road_call.event=202;\n"
            "    road_call.object=(uint32_t)ctx->r7;\n"
            "    road_call.holder=(uint32_t)ctx->r4;road_call.sprite=MEM_W(16,ctx->r29);"
            "road_call.flags=MEM_W(20,ctx->r29);\n"
            f"    {SCOPE}\n"
            "\n    ctx->r2 = 1;\n"
            f"    road_call.event=203;{SCOPE}return;\n"
            "}\n",
        )

    def test_other_functions_are_untouched(self) -> None:
        body = "RECOMP_FUNC void render_scene(uint8_t* rdram, recomp_context* ctx) {\n    return;\n    return;\n}\n"
        self.assertIs(hook("render_scene", body), body)

    def test_enter_call_precedes_the_camera_step_insert(self) -> None:
        # generate() puts dkr_camera_step() after the same first brace afterwards.
        hooked = hook("render_3d_model", MODEL)
        self.assertEqual(hooked.count("{0}"), 1)
        self.assertLess(hooked.index("road_call.event=200"), hooked.index("ctx->r2 = 1"))

    def test_exactly_one_return_is_required(self) -> None:
        for name, body in (("render_3d_model", MODEL), ("render_sprite_billboard", SPRITE)):
            with self.subTest(name=name, returns=0), self.assertRaises(ValueError):
                hook(name, body.replace("return;", ""))
            with self.subTest(name=name, returns=2), self.assertRaises(ValueError):
                hook(name, body.replace("return;", "return;\n    return;"))

    def test_the_scope_call_is_an_allowed_callee_and_the_new_globals_are_resolved(self) -> None:
        for symbol in ("gCurrentLightIntensity", "gInverseViewMatrixF", "gViewportLayout"):
            self.assertIn(symbol, camera_scene_kernel.GLOBALS)
        source = (ROOT / "scripts/camera_scene_kernel.py").read_text()
        self.assertIn('"dkr_native_road_wheel_scope",', source)


if __name__ == "__main__":
    unittest.main()
