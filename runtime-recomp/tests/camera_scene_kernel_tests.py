#!/usr/bin/env python3
"""scripts/camera_scene_kernel.py: the road-wheel scope hooks the "camera" payload of
hooks.toml puts into render_3d_model and render_sprite_billboard (events 200 to
203), applied through scripts/guest_hooks.py, and nothing else."""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import camera_scene_kernel  # noqa: E402
import guest_hooks  # noqa: E402

SIGNATURE = "RECOMP_FUNC void {}(uint8_t* rdram, recomp_context* ctx) {{"
MODEL = SIGNATURE.format("render_3d_model") + "\n    ctx->r2 = 1;\n    return;\n}\n"
SPRITE = SIGNATURE.format("render_sprite_billboard") + "\n    ctx->r2 = 1;\n    return;\n}\n"
SCOPE = "dkr_native_road_wheel_scope(rdram,0x1000000U,&road_call);"


def hook(name: str, text: str, revision: int = 77) -> str:
    return camera_scene_kernel.hook_routine(text, camera_scene_kernel.camera_hooks(revision)[name])


def address(symbol: str, revision: int = 77) -> str:
    return f"0x{guest_hooks.symbols(str(revision))[symbol]:08X}U"


class RoadWheelHookTests(unittest.TestCase):
    def test_only_the_two_road_wheel_routines_are_hooked(self) -> None:
        for revision in (77, 80):
            self.assertEqual(
                set(camera_scene_kernel.camera_hooks(revision)), {"render_3d_model", "render_sprite_billboard"}
            )

    def test_model_hook_text(self) -> None:
        for revision in (77, 80):
            with self.subTest(revision=revision):
                self.assertEqual(
                    hook("render_3d_model", MODEL, revision),
                    SIGNATURE.format("render_3d_model") + "\n"
                    "    dkr_road_wheel_call road_call={0};\n"
                    "    road_call.event=200;\n"
                    "    road_call.object=(uint32_t)ctx->r4;\n"
                    f"    road_call.intensity={address('gCurrentLightIntensity', revision)};\n"
                    f"    road_call.inverse_view={address('gInverseViewMatrixF', revision)};\n"
                    f"    road_call.viewport_layout={address('gViewportLayout', revision)};\n"
                    f"    {SCOPE}\n"
                    "\n    ctx->r2 = 1;\n"
                    f"    road_call.event=201;{SCOPE}return;\n"
                    "}\n",
                )

    def test_sprite_hook_text(self) -> None:
        self.assertEqual(
            hook("render_sprite_billboard", SPRITE),
            SIGNATURE.format("render_sprite_billboard") + "\n"
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

    def test_enter_call_precedes_the_camera_step_insert(self) -> None:
        # generate() puts dkr_camera_step() after the same first brace afterwards.
        hooked = hook("render_3d_model", MODEL)
        self.assertEqual(hooked.count("{0}"), 1)
        self.assertLess(hooked.index("road_call.event=200"), hooked.index("ctx->r2 = 1"))

    def test_every_return_is_fenced_and_one_is_required(self) -> None:
        two = MODEL.replace("return;", "return;\n    return;")
        self.assertEqual(hook("render_3d_model", two).count("road_call.event=201;"), 2)
        with self.assertRaises(ValueError):
            hook("render_3d_model", MODEL.replace("return;", ""))

    def test_an_unbraced_return_is_refused(self) -> None:
        for name, text in (("render_3d_model", MODEL), ("render_sprite_billboard", SPRITE)):
            for guard in ("if (ctx->r2 == 1) return;", "else return;", "while (ctx->r2) return;"):
                with self.subTest(name=name, guard=guard), self.assertRaises(ValueError):
                    hook(name, text.replace("return;", guard))
        braced = MODEL.replace("return;", "if (ctx->r2 == 1) { return; }")
        self.assertEqual(hook("render_3d_model", braced).count("road_call.event=201;"), 1)

    def test_a_return_in_a_string_or_comment_is_not_a_return(self) -> None:
        text = MODEL.replace("    return;", '    // return;\n    /* return; */\n    puts("return;");\n    return;')
        hooked = hook("render_3d_model", text)
        self.assertEqual(hooked.count("road_call.event=201;"), 1)
        self.assertIn('puts("return;");', hooked)
        only_comment = MODEL.replace("return;", "// return;")
        with self.assertRaises(ValueError):
            hook("render_3d_model", only_comment)

    def test_the_scope_call_is_an_allowed_callee_and_the_new_globals_are_resolved(self) -> None:
        for symbol in ("gCurrentLightIntensity", "gInverseViewMatrixF", "gViewportLayout"):
            self.assertIn(symbol, camera_scene_kernel.GLOBALS)
        source = (ROOT / "scripts/camera_scene_kernel.py").read_text()
        self.assertIn('"dkr_native_road_wheel_scope",', source)

    def test_the_camera_hooks_stay_out_of_the_file_payloads(self) -> None:
        spec = camera_scene_kernel.HOOK_SPEC
        for payload in guest_hooks.FILE_PAYLOADS:
            ids = {h.id for h in guest_hooks.load(spec, "77", payload)}
            self.assertFalse({"camera-road-wheel-model", "camera-road-wheel-sprite"} & ids)


if __name__ == "__main__":
    unittest.main()
