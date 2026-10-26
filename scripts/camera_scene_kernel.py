"""Compile a closed draw-only routine set into NEW private pipeline output.

No routine in this output links to the live game, renderer globals or native
mod services. All guest accesses are checked against the supplied DECODE COPY.
The approved roots prepare/draw ONE view, never render_scene or a logic tick.
This is the Camera++ scene kernel (DKR_CAMERA_KERNEL_V77/V80), a supported
prepare step: scripts/prepare_payloads.py runs it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
from typing import cast

from kernel_inputs import CALL, DEF, HEADER_HASH, elf_symbols, strip

ROOT = Path(__file__).resolve().parents[1]
# The kernel's native bridge (DKRCameraScene compiles these beside it).
BRIDGE = ROOT / "runtime-recomp/src/game"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def new_output(path: Path, *inputs: Path) -> Path:
    path = path.resolve()
    if path.exists() or any(
        path == p.resolve() or p.resolve() in path.parents or path in p.resolve().parents for p in inputs
    ):
        raise ValueError("Choose a NEW Camera++ output, outside and not above all inputs")
    if any(part.lower() in ("extern", "recompiledfuncs", "recompiledpatches") for part in path.parts):
        raise ValueError("Camera++ output cannot enter a protected tree")
    if (ROOT / "build").resolve() not in path.parents:
        raise ValueError("Camera++ generated output must stay inside the workspace build directory")
    return path


ROOTS = (
    "initialise_player_viewport_vars",
    "skydome_render",
    "trackbg_render_gradient",
    "trackbg_render_flashy",
    "viewport_main",
    "mtx_world_origin",
    "func_8002A31C",
    "apply_fog",
    "set_active_camera",
    "rendermode_reset",
    "snow_vertices",
    "snow_render",
    "rain_render",
    "rain_render_splashes",
    "lensflare_render",
    "lensflare_override",
    "weather_clip_planes",
    "get_camera_matrix",
    "mtx_ortho",
    "viewport_reset",
    "mtx_perspective",
)
# The only transitive root set admitted by this generator. A code update
# adding another routine requires an explicit review, not a wider catch-all.
NAMES = set(
    """add_segment_to_order apply_fog arctan2_f atan2s block_visible
calc_dynamic_lighting_for_object_1 calc_dynamic_lighting_for_object_2 calc_env_mapping_for_object
cam_get_active_camera cam_get_viewport_layout check_if_in_draw_range check_if_showing_cutscene_camera
compute_grid_overlap_mask compute_scene_camera_transform_matrix coss_f coss_s16 directional_lighting_off
directional_lighting_on fb_size func_80012C30 func_80012C3C func_80012C98 func_80012CE8 func_800135B8
func_80026070 func_80026430 func_80026C14 func_80026E54 func_80027568 func_8002A31C func_80067D3C
func_800B92F4 func_800B97A8 func_800BA288 generate_collision_candidates get_current_viewport
get_distance_to_active_camera get_distance_to_camera get_filtered_cheats get_first_active_object get_misc_asset
get_object get_projection_matrix_f32 get_racer_objects get_settings gfx_init_basic_xlu
initialise_player_viewport_vars is_player_two_in_control is_time_trial_enabled level_header level_is_race
leveltable_type material_load_simple material_set material_set_blinking_lights material_set_no_tex_offset
mtx_cam_push mtx_head_push mtx_pop mtx_shear_push mtx_world_origin mtxf_billboard mtxf_from_inverse_transform
mtxf_from_transform mtxf_from_translation mtxf_mul mtxf_scale_y mtxf_to_mtx mtxf_to_mtxs
mtxf_transform_dir mtxf_transform_point mtxf_translate_y mtxs_transform_dir obj_animate obj_door_number
obj_shade_fancy obj_shade_fast obj_tex_animate object_do_player_tumble object_undo_player_tumble rand_range
regenerate_point_particles_mesh render_3d_billboard render_3d_misc render_3d_model render_bubble_trap
render_level_geometry_and_objects render_level_segment render_mesh render_misc_model render_object
render_object_parts render_particle render_racer_magnet render_racer_shield render_sprite_billboard
rendermode_reset set_active_camera set_animated_texture_header set_temp_model_transforms shadow_render sins_f
sins_s16 skydome_render sort_objects_by_dist sqrtf_recomp tex_animate_texture trackbg_render_flashy
trackbg_render_gradient traverse_segments_bsp_tree unset_temp_model_transforms update_envmap_position
update_object_stack_trace vec3f_rotate vec3f_rotate_ypr vec3s_reflect viewport_main viewport_rsp_set void_check
void_generate_primitive watereffect_render wave_load_material waves_block_hq waves_get_y waves_render
waves_visibility waves_visibility_reset func_800BB2F4 get_camera_matrix get_inside_segment_count_xz
get_level_segment_index_from_position get_level_segment_waves get_projection_matrix_s16 get_racer_object_by_port
lensflare_override lensflare_render mtx_ortho normalise_time rain_render rain_render_splashes
snow_render snow_vertices viewport_reset viewport_scissor weather_clip_planes mtx_perspective""".split()
)
HOOKS = set(
    """dkr_anchor_persistent_water_to_camera dkr_apply_maximum_racer_detail
dkr_billboard_interpolation_begin dkr_extend_hub_segment_bitfield dkr_extend_object_draw_distance
dkr_extended_frustum_begin dkr_extended_frustum_end dkr_fix_car_steering_wheel_material
dkr_keep_hub_segment_visible dkr_legacy_texture_animation dkr_level_segment_interpolation_begin
dkr_level_segment_interpolation_end dkr_netplay_presentation_random_begin dkr_netplay_presentation_random_end
dkr_netplay_presentation_random_range dkr_presentation_object_begin dkr_presentation_object_end
dkr_presentation_wave_begin dkr_presentation_wave_block dkr_presentation_wave_end dkr_presentation_wave_matrix
dkr_presentation_wave_selection dkr_presentation_world_origin_matrix dkr_shadow_interpolation_begin
dkr_shadow_interpolation_end dkr_skybox_cover_begin dkr_skybox_cover_end dkr_stabilise_persistent_water_transition
dkr_surface_interpolation_begin dkr_surface_interpolation_end dkr_track_select_draw
dkr_vehicle_part_interpolation_end dkr_vehicle_part_matrix_identity dkr_widen_gradient_sky
dkr_widen_void_primitive dkr_track_select_lens_flare_tint_begin dkr_track_select_lens_flare_tint_end
dkr_presentation_perspective_matrix""".split()
)
MATH = {"__f_to_ll_recomp", "__ll_to_f_recomp"}
GLOBALS = """gTrackDL gTrackMtxPtr gTrackVtxPtr gTrackTriPtr gSceneActiveCamera gSceneCurrentPlayerID
gSceneRenderSkyDome gDrawLevelSegments gDisableShadows gCurrentLevelHeader2 gCurrentLevelModel
gCameras gActiveCameraID gPerspectiveMatrixF gPerspectiveMatrix gViewportLayout gScenePlayerViewports
gCutsceneCameraActive gModelMatrixStackPos gMtxOriginID gObjectCurrDisplayList gObjectCurrMatrix perspNorm
gViewProjMatrixF gWaveController gCurrWeatherDisplayList gCurrWeatherMatrix gCurrWeatherVertexList
gCurrWeatherTriList gWeatherCamera gWeatherCameraMatrix gWeatherType gSnowParticleCount gSnowVerts
gSnowVertexData gSnowVertexFlip gRainSplashDelay gRainGfx gLightningFrequency D_800DC8AC
D_8011D4A0 D_8011D4A4 D_8011D4AC D_8011D4B0""".split()


def generate(source: Path, header: Path, elf: Path, revision: int, output: Path) -> None:
    source = source.resolve(strict=True)
    output = new_output(output, source, header.parent)
    raw = header.read_text()
    if digest(header) != HEADER_HASH:
        raise ValueError("Unreviewed scene-kernel register ABI")
    functions: dict[str, str] = {}
    for path in sorted(source.glob("*.c")):
        text = path.read_text()
        matches = list(DEF.finditer(text))
        for i, m in enumerate(matches):
            if m[1] in functions:
                raise ValueError("Duplicate scene routine")
            functions[m[1]] = text[m.start() : matches[i + 1].start() if i + 1 < len(matches) else len(text)]
    work = list(ROOTS)
    selected: dict[str, str] = {}
    while work:
        name = work.pop()
        if name in selected:
            continue
        if name not in NAMES or name not in functions:
            raise ValueError("Unreviewed scene dependency: " + name)
        body = functions[name]
        selected[name] = body
        work.extend((set(CALL.findall(strip(body))) & set(functions)) - {name})
    if set(selected) != NAMES:
        raise ValueError("Draw-only closure changed: " + str(set(selected) ^ NAMES))
    prefix = f"dkr_camera_v{revision}_"
    header_text = raw.replace("// Compiler definition", '#include "camera_kernel_guest.h"\n// Compiler definition', 1)
    for name, typ, width in (
        ("W", "int32_t", 4),
        ("H", "int16_t", 2),
        ("B", "int8_t", 1),
        ("HU", "uint16_t", 2),
        ("BU", "uint8_t", 1),
    ):
        header_text, count = re.subn(
            r"#define MEM_" + name + r"\(offset, reg\) \\\n[^\n]*",
            f"#define MEM_{name}(offset, reg) (*({typ}*)dkr_camera_access((uint64_t)(reg)+(uint64_t)(offset),{width}))",
            header_text,
        )
        if count != 1:
            raise ValueError("Scene memory macro changed")
    header_text, count = re.subn(
        r"#define SD\(val, offset, reg\) \{.*?\n\}",
        r"""#define SD(val, offset, reg) { \\
    *(uint32_t*)dkr_camera_access((uint64_t)(reg)+(uint64_t)(offset)+4,4)=(uint32_t)(val); \\
    *(uint32_t*)dkr_camera_access((uint64_t)(reg)+(uint64_t)(offset),4)=(uint32_t)((uint64_t)(val)>>32); \\
}""",
        header_text,
        flags=re.S,
    )
    if count != 1:
        raise ValueError("Scene doubleword store changed")
    header_text = header_text.replace(
        "assert(val == val)", '((val)==(val)?(void)0:dkr_camera_fault("non-finite scene value"))'
    )
    header_text = header_text.replace(
        "assert(((idx) & 1) == 0 || (ctx)->mips3_float_mode)",
        '((((idx)&1)==0||(ctx)->mips3_float_mode)?(void)0:dkr_camera_fault("scene floating ABI"))',
    )
    macros = set(re.findall(r"^#define\s+(\w+)\s*\(", raw, re.M))
    macros |= set(re.findall(r"static\s+inline\s+\w+\s+(\w+)\s*\(", raw))
    allowed = (
        NAMES
        | HOOKS
        | MATH
        | macros
        | {"if", "while", "for", "switch", "return", "sizeof", "void", "sqrtf", "do_break", "switch_error"}
    )
    symbols = elf_symbols(elf, (0, 1, 2))
    addresses: dict[str, int] = {}
    for name in GLOBALS:
        entries = symbols.get(name, set())
        if len(entries) != 1:
            raise ValueError("Unknown scene global: " + name)
        addresses[name] = next(iter(entries))[0]
    emitted: list[str] = []
    for name, body in sorted(selected.items()):
        extra = set(CALL.findall(strip(body))) - allowed
        if extra or re.search(r"\brdram\s*(?:\[|\+)", strip(body)):
            raise ValueError("Scene escaped its closed boundary: " + name + " " + str(extra))
        body = body.replace("{", "{\n    dkr_camera_step();", 1)
        body = re.sub(r"^(L_[0-9A-Fa-f]+:)\s*$", r"\1 dkr_camera_step();", body, flags=re.M)
        # Uniform C prototypes for observation-only injected hooks. No process-
        # wide observer may be called from this private renderer.
        body = re.sub(
            r"extern\s+(?:unsigned|int|void)\s+(dkr_\w+)\([^;]*\);", lambda m: "" if m[1] in HOOKS else m[0], body
        )
        for n in sorted(NAMES | HOOKS | MATH | {"switch_error", "do_break"}, key=len, reverse=True):
            body = re.sub(r"\b" + n + r"\b", prefix + n, body)
        emitted.append(body)
    output.mkdir(parents=True)
    (output / "recomp.h").write_text(header_text)
    prototypes = "".join(f"void {prefix}{n}(uint8_t*,recomp_context*);\n" for n in sorted(NAMES | MATH))
    prototypes += "".join(f"unsigned {prefix}{n}(uint8_t*,recomp_context*,...);\n" for n in sorted(HOOKS))
    prototypes += f"void {prefix}do_break(uint32_t);\nvoid {prefix}switch_error(const char*,uint32_t,uint32_t);\n"
    (output / "funcs.h").write_text('#pragma once\n#include "recomp.h"\n' + prototypes)
    (output / "camera_scene_functions.c").write_text('#include "funcs.h"\n' + "".join(emitted))
    special = {
        "dkr_presentation_world_origin_matrix",
        "dkr_presentation_object_begin",
        "dkr_presentation_object_end",
        "dkr_extend_hub_segment_bitfield",
        "dkr_legacy_texture_animation",
        "dkr_fix_car_steering_wheel_material",
        "dkr_skybox_cover_begin",
        "dkr_skybox_cover_end",
        "dkr_presentation_wave_begin",
        "dkr_presentation_wave_end",
        "dkr_presentation_wave_block",
        "dkr_presentation_wave_selection",
        "dkr_presentation_wave_matrix",
        "dkr_widen_gradient_sky",
        "dkr_widen_void_primitive",
    }
    # Ignored hooks are renderer observation/quality overrides only. Their
    # live host implementations must never be reached from the private scene.
    stubs = "".join(
        f"unsigned {prefix}{n}(uint8_t* ram,recomp_context* ctx,...){{(void)ram;(void)ctx;return 0;}}\n"
        for n in sorted(HOOKS - special)
    )
    (output / "camera_scene_hooks.c").write_text('#include "funcs.h"\n' + stubs)
    definitions = "\n".join(
        f"#define {n} {prefix}{n}" for n in sorted(NAMES | HOOKS | MATH | {"do_break", "switch_error"})
    )
    definitions += "\n" + "\n".join(f"#define CAMERA_{n} 0x{a:08X}U" for n, a in addresses.items())
    (output / "camera_scene_addresses.h").write_text(
        "#pragma once\n" + definitions + f"\n#define CAMERA_ENTRY {prefix}draw\n"
    )
    (output / "manifest.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "revision": revision,
                "roots": ROOTS,
                "functions": {n: hashlib.sha256(body.encode()).hexdigest() for n, body in sorted(selected.items())},
                "source": source.relative_to(ROOT).as_posix(),
                "recipe": digest(Path(__file__)),
                "bridge": {
                    name: digest(BRIDGE / name)
                    for name in ("camera_kernel.c", "camera_kernel.h", "camera_kernel_guest.h")
                },
                "files": {p.name: digest(p) for p in sorted(output.iterdir()) if p.is_file()},
                "guest_writes": False,
                "logic_ticks": False,
                "device_calls": False,
            },
            indent=2,
        )
        + "\n"
    )
    print("Private draw-only closure staged:", revision, len(selected), output)


def verify(output: Path) -> None:
    manifest = cast(dict[str, object], json.loads((output / "manifest.json").read_text()))
    if manifest.get("schema") != 1 or manifest.get("recipe") != digest(Path(__file__)):
        raise ValueError("Regenerate the private camera draw closure after recipe changes")
    if {p.name: digest(p) for p in output.iterdir() if p.is_file() and p.name != "manifest.json"} != manifest["files"]:
        raise ValueError("Camera draw output contains unreviewed edits")
    for name, sha in cast(dict[str, str], manifest["bridge"]).items():
        if digest(BRIDGE / name) != sha:
            raise ValueError("Camera draw bridge changed")
    source = (ROOT / cast(str, manifest["source"])).resolve(strict=True)
    if ROOT / "build" not in source.parents:
        raise ValueError("Camera draw input escaped the pipeline")
    found: dict[str, str] = {}
    for path in sorted(source.glob("*.c")):
        text = path.read_text()
        matches = list(DEF.finditer(text))
        for i, m in enumerate(matches):
            if m[1] in NAMES:
                if m[1] in found:
                    raise ValueError("Duplicate camera draw input")
                body = text[m.start() : matches[i + 1].start() if i + 1 < len(matches) else len(text)]
                found[m[1]] = hashlib.sha256(body.encode()).hexdigest()
    if found != manifest["functions"] or set(found) != NAMES:
        raise ValueError("Camera draw source provenance changed")
    print("Private camera draw composition verified:", manifest["revision"])


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--verify", action="store_true")
    p.add_argument("--source", type=Path)
    p.add_argument("--header", type=Path)
    p.add_argument("--elf", type=Path)
    p.add_argument("--revision", type=int, choices=(77, 80))
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    if a.verify:
        verify(a.output.resolve(strict=True))
    elif not all((a.source, a.header, a.elf, a.revision)):
        p.error("generation requires source, header, ELF and revision")
    else:
        generate(a.source, a.header, a.elf, a.revision, a.output)
