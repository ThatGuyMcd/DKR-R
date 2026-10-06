"""Control-flow regression checks for the isolated Patch Pipeline probe."""
import importlib.util
from pathlib import Path
import unittest
import hashlib
import os

spec = importlib.util.spec_from_file_location(
    "probe_generator", Path(__file__).resolve().parents[3] / "scripts/generate_replay_probe.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class InstrumentationTests(unittest.TestCase):
    def test_private_runtime_capsules_do_not_share_bridge_or_factory_symbols(self):
        for revision in (77,80):
            emitted=generator.runtime_isolation_header({'dkr_probe_authored_main_cpu'},set(),revision)
            for name in ('make_owned_game','dkr_owned_mod_calls','dkr_probe_rom','dkr_probe_native_state','dkr_probe_bind_paks','dkr_probe_audio_rsp'):
                self.assertIn(f'#define {name} dkr_experimental_v{revision}_{name}',emitted)
            self.assertNotIn('#define dkr_probe_authored_main_cpu ',emitted)
            self.assertNotIn('#define OwnedGame ',emitted)
            self.assertNotIn('#define RuntimeState ',emitted)

    def test_owned_mod_callbacks_are_closed_and_fault_contained(self):
        emitted=generator.owned_mod_calls_header()
        self.assertNotIn('active_payload',emitted)
        for target in set(generator.OWNED_MOD_CALLS.values()) | {'rand_range','dkr_character_select_animation_fraction'}:
            self.assertIn(f'dkr_probe_run_native({target},ram,ctx)',emitted)
        self.assertIn('throw dkr::mods::Error',emitted)
        self.assertIn('#undef get_settings',emitted)

    def test_custom_track_observation_and_ai_callback_keep_exact_abi(self):
        sig=('int',['uint8_t*','recomp_context*','unsigned','const uint32_t*','unsigned'])
        emitted=generator.native_stub('dkr_legacy_track_menu',sig,True)
        self.assertIn('args[]={p4}',emitted)
        self.assertIn('args,1,p3,p2',emitted)
        emitted=generator.native_stub('dkr_legacy_character_ai_event',generator.PROJECT_IMPORTS['dkr_legacy_character_ai_event'],True)
        self.assertIn('p3!=rand_range',emitted)
        self.assertIn('args[]={p2}',emitted)
        with self.assertRaises(ValueError):
            generator.native_stub('dkr_legacy_character_ai_event',('void',['uint8_t*','recomp_context*','unsigned','void*']),True)
    def test_local_world_observations_preserve_exact_caller(self):
        for revision in (77,80):
            body=self.reviewed_bodies(revision)["render_level_geometry_and_objects"]
            emitted=generator.local_world_observation_body(body,revision)
            for phase,callee in ((0,"sort_objects_by_dist"),(1,"func_80012C3C")):
                observation=f"    dkr_probe_local_world_draw(rdram, ctx, {phase});\n"
                self.assertEqual(emitted.count(observation),1)
                self.assertIn(observation+f"    {callee}(rdram, ctx);\n",emitted)
                emitted=emitted.replace(observation,"")
            self.assertEqual(emitted,body)
            with self.assertRaises(ValueError):
                generator.local_world_observation_body(body+'\n',revision)

    def test_menu_indirect_callbacks_are_exact_and_pinned(self):
        for revision,address in ((77,0x8008F618),(80,0x8008FAD0)):
            bodies=self.reviewed_bodies(revision)
            self.assertEqual(generator.menu_dispatch(bodies,revision),{address:"func_8008F618"})
            for name in generator.MENU_INDIRECT_HASH[revision]:
                altered=dict(bodies);altered[name]+='\n'
                with self.assertRaises(ValueError):generator.menu_dispatch(altered,revision)

    def test_indirect_owners_are_separate_and_unknown_targets_stay_blocked(self):
        emitted=generator.indirect_stub({1:"audio_target"},{2:"menu_target"})
        self.assertIn('case 0x00000001U: if (!dkr_probe_audio_enabled())',emitted)
        self.assertIn('case 0x00000002U: if (!dkr_probe_menu_callback_enabled())',emitted)
        self.assertIn('default: dkr_probe_block("unreviewed-indirect-callback")',emitted)
        with self.assertRaises(ValueError):generator.indirect_stub({1:"audio_target"},{1:"menu_target"})

    def test_full_scene_native_participants_are_pinned(self):
        generator.full_scene_native_audit()
        for name in ("intro_tail_policy.hpp", "online_roster_policy.hpp",
                     "character_select_animation_policy.hpp", "character_select_music_policy.hpp"):
            self.assertIn("runtime-recomp/src/game/"+name,generator.FULL_SCENE_NATIVE_HASH)

    def test_full_scene_native_abis_reject_drift(self):
        for name in ("dkr_title_intro_audio_tail", "dkr_netplay_character_select_enter",
                     "dkr_netplay_character_select_lock", "dkr_character_select_animation_fraction"):
            self.assertIn("dkr_probe_native",generator.native_stub(name,('void',['uint8_t*','recomp_context*']),True))
            with self.assertRaises(ValueError):generator.native_stub(name,('int',['uint8_t*','recomp_context*']),True)

    def reviewed_bodies(self, revision):
        root=Path("G:/DiddykongWorkFolder" if os.name=="nt" else "/mnt/g/DiddykongWorkFolder")
        directory=root/f"water-performance-20260926/pipeline/generated-v{revision}"
        if not directory.exists():self.skipTest("Reviewed scene inputs unavailable")
        bodies={}
        for path in directory.glob("*.c"):
            source=path.read_text();matches=list(generator.DEF.finditer(source))
            for i,match in enumerate(matches):
                bodies[match[1]]=source[match.start():matches[i+1].start() if i+1<len(matches) else len(source)]
        return bodies

    def test_full_scene_menu_cuts_are_exact_and_pre_prologue(self):
        for revision in (77,80):
            bodies=self.reviewed_bodies(revision)
            emitted=generator.menu_scene_cuts(bodies["mode_menu"],revision)
            self.assertLess(emitted.index("switch (dkr_probe_scene_entry())"),emitted.index("ctx->r29 ="))
            for site in range(3,7):
                self.assertEqual(emitted.count(f"dkr_probe_scene_cut({site})"),1)
                self.assertIn(f"case {site}: goto dkr_probe_menu_unload_{site};",emitted)
            self.assertEqual(emitted.count("menu_loop(rdram, ctx);"),1)
            with self.assertRaises(ValueError):generator.menu_scene_cuts(bodies["mode_menu"]+'\n',revision)

    def test_full_scene_resumer_does_not_reenter_menu_input_or_prologue(self):
        for revision in (77,80):
            bodies=self.reviewed_bodies(revision)
            main,game,menu=generator.owned_scene_resume(bodies["main_game_loop"],bodies["mode_game"],revision,bodies["mode_menu"])
            self.assertIn('goto after_18;',main)
            self.assertIn('goto after_19;',main)
            self.assertEqual(main.count('input_update(rdram, ctx);'),1)
            self.assertIn('if (dkr_probe_scene_pending() >= 3)',main)
            for name in ('unload_level_menu','unload_level_game','load_level_game','load_menu_with_level_background','load_next_ingame_level'):
                self.assertNotIn(name+'(rdram, ctx);',menu)
            self.assertEqual(menu.count('dkr_probe_scene_transaction_cut('),4)
            self.assertEqual(game.count('dkr_probe_scene_transaction_cut('),2)
            self.assertIn('dkr_probe_scene_transaction_end();\n    return;',menu)

    def test_owned_menu_constructors_preserve_retail_operations(self):
        for revision in (77,80):
            bodies=self.reviewed_bodies(revision);emitted=generator.owned_menu_cpu(bodies,revision)
            unload=emitted['dkr_probe_menu_unload_cpu'];load=emitted['dkr_probe_menu_load_cpu']
            for name in ('level_free','transition_begin','reset_particles','hud_free','free_game_text_table'):
                self.assertEqual(unload.count(name+'(rdram, ctx);'),1)
            for name in ('cam_init','load_game_text_table','hud_init','init_particle_buffers','ainode_update','osSetTime_recomp'):
                self.assertEqual(load.count(name+'(rdram, ctx);'),1)
            self.assertNotIn('gfxtask_wait',unload)
            self.assertIn('dkr_probe_level_load_cpu(rdram, ctx);',load)
            for name in generator.MENU_CPU_HASH[revision]:
                changed=dict(bodies);changed[name]+='\n'
                with self.assertRaises(ValueError):generator.owned_menu_cpu(changed,revision)

    def test_menu_native_default_is_opt_in_not_general_fallback(self):
        sig=('int',['uint8_t*','recomp_context*','unsigned','const uint32_t*','unsigned'])
        self.assertIn('unowned-track-menu-observation',generator.native_stub('dkr_legacy_track_menu',sig))
        self.assertNotIn('unowned-track-menu-observation',generator.native_stub('dkr_legacy_track_menu',sig,True))
        self.assertIn('dkr_probe_block',generator.native_stub('unknown_native',('void',[]),True))

    def test_audio_isolation_includes_cpp_types_and_shared_dsp_storage(self):
        for revision in (77,80):
            header=generator.audio_isolation_header(revision)
            names=("dkrAspMain", "dmem", "rspReciprocals", "rspInverseSquareRoots",
                   "RSP", "RspContext", "RspExitReason", "RspUcodeFunc")
            self.assertEqual({line for line in header.splitlines() if line.startswith("#define ")},
                {f"#define {name} dkr_experimental_v{revision}_{name}" for name in names})
        with self.assertRaises(ValueError):generator.audio_isolation_header(81)

    def test_symbol_isolation_is_exact_and_revision_specific(self):
        guest=("main_game_loop", "obj_update", "dkr_probe_authored_main_cpu")
        imports=("osRecvMesg_recomp", "get_function", "pause_self", "__f_to_ll_recomp")
        for revision in (77,80):
            header=generator.isolation_header(guest,imports,revision)
            expected={f"#define {name} dkr_experimental_v{revision}_{name}" for name in (*guest,*imports)}
            actual={line for line in header.splitlines() if line.startswith("#define ")}
            self.assertEqual(actual,expected)
            self.assertEqual(header,generator.isolation_header(reversed(guest),reversed(imports),revision))
            self.assertNotIn("#define dkr_probe_block",header) # Owned service, not a retail import.
        for invalid in ("main_game_loop;", "osRecvMesg_recomp()", "bad name", "../../oops"):
            with self.assertRaises(ValueError):generator.isolation_header((invalid,),(),77)
        with self.assertRaises(ValueError):generator.isolation_header(guest,imports,81)

    def test_authored_guest_entry_c_linkage(self):
        emitted=generator.authored_declarations()
        opening=emitted.index('extern "C" {')
        closing=emitted.index('\n}\n')
        for name in ("dkr_probe_authored_main_cpu","dkr_probe_authored_video_cpu","dkr_probe_scene_unload_cpu",
                     "dkr_probe_scene_load_cpu","dkr_probe_level_load_cpu","dkr_probe_scene_resume_main_cpu",
                     "dkr_probe_scene_resume_mode_cpu"):
            self.assertLess(opening,emitted.index(name))
            self.assertLess(emitted.index(name),closing)

    def test_unload_requires_exact_retail_source(self):
        for revision in (77,80,81):
            with self.assertRaises(ValueError):generator.owned_unload_cpu("",revision)

    def test_load_requires_exact_retail_source(self):
        for revision in (77,80,81):
            with self.assertRaises(ValueError):generator.owned_load_cpu("","",revision)

    def test_confirmed_resume_requires_both_exact_sources(self):
        for revision in (77,80,81):
            with self.assertRaises(ValueError):generator.owned_scene_resume("","",revision)

    def test_confirmed_resume_preserves_continuation_not_second_input(self):
        root=Path("G:/DiddykongWorkFolder" if os.name=="nt" else "/mnt/g/DiddykongWorkFolder")
        for revision in (77,80):
            directory=root/f"water-performance-20260926/pipeline/generated-v{revision}"
            if not directory.exists():self.skipTest("Private reviewed scene inputs unavailable")
            bodies={}
            for path in directory.glob("*.c"):
                source=path.read_text();matches=list(generator.DEF.finditer(source))
                for i,match in enumerate(matches):
                    if match[1] in ("main_game_loop","mode_game"):
                        bodies[match[1]]=source[match.start():matches[i+1].start() if i+1<len(matches) else len(source)]
            main,mode=generator.owned_scene_resume(bodies["main_game_loop"],bodies["mode_game"],revision)
            self.assertLess(main.index('no-confirmed-scene-continuation'),main.index('ctx->r29 ='))
            self.assertIn('goto after_19;',main)
            self.assertEqual(mode.count('dkr_probe_scene_transaction_cut('),2)
            self.assertEqual(mode.count('dkr_probe_scene_unload_cpu(rdram, ctx);'),2)
            self.assertGreaterEqual(mode.count('dkr_probe_scene_load_cpu(rdram, ctx);'),1)
            self.assertIn('dkr_probe_scene_transaction_end();\n    return;',mode)
            for name in ("unload_level_game","load_level_game","dkr_probe_scene_cut"):
                self.assertNotIn(name+'(',mode)
            for changed in ((bodies["main_game_loop"]+'\n',bodies["mode_game"]),
                            (bodies["main_game_loop"],bodies["mode_game"]+'\n')):
                with self.assertRaises(ValueError):generator.owned_scene_resume(*changed,revision)

    def test_load_preserves_retail_constructor_and_owned_reset(self):
        root=Path("G:/DiddykongWorkFolder" if os.name=="nt" else "/mnt/g/DiddykongWorkFolder")
        for revision in (77,80):
            directory=root/f"water-performance-20260926/pipeline/generated-v{revision}"
            if not directory.exists():self.skipTest("Private reviewed scene inputs unavailable")
            bodies={}
            for path in directory.glob("*.c"):
                source=path.read_text();matches=list(generator.DEF.finditer(source))
                for i,match in enumerate(matches):
                    if match[1] in ("load_level_game","level_load"):
                        bodies[match[1]]=source[match.start():matches[i+1].start() if i+1<len(matches) else len(source)]
            game,level=generator.owned_load_cpu(bodies["load_level_game"],bodies["level_load"],revision)
            self.assertLess(game.index('dkr_probe_scene_load_begin('),game.index('ctx->r29 ='))
            self.assertLess(level.index('dkr_probe_scene_load_reset('),level.index('ctx->r29 ='))
            for name in ("alloc_displaylist_heap","mempool_free_timer","cam_init","load_game_text_table",
                         "hud_init","init_particle_buffers","ainode_update","osSetTime_recomp","rumble_init"):
                self.assertEqual(game.count(name+'(rdram, ctx);'),bodies["load_level_game"].count(name+'(rdram, ctx);'))
            self.assertIn('dkr_probe_level_load_cpu(rdram, ctx);',game)
            self.assertIn('dkr_probe_scene_load_ready(rdram, ctx);',game)
            for native in ("dkr_netplay_gameplay_level_begin","dkr_netplay_gameplay_level_ready"):
                self.assertNotIn(native,game)
            for native in ("dkr_runtime_scene_reset","dkr_presentation_scene_begin","dkr_legacy_scene_begin"):
                self.assertNotIn(native,level)
            for changed in ((bodies["load_level_game"]+'\n',bodies["level_load"]),
                            (bodies["load_level_game"],bodies["level_load"]+'\n')):
                with self.assertRaises(ValueError):generator.owned_load_cpu(*changed,revision)

    def test_unload_retains_free_sequence_not_live_scheduler(self):
        root=Path("G:/DiddykongWorkFolder" if os.name=="nt" else "/mnt/g/DiddykongWorkFolder")
        for revision in (77,80):
            directory=root/f"water-performance-20260926/pipeline/generated-v{revision}"
            if not directory.exists():self.skipTest("Private reviewed scene inputs unavailable")
            body=None
            for path in directory.glob("*.c"):
                source=path.read_text();matches=list(generator.DEF.finditer(source))
                for i,match in enumerate(matches):
                    if match[1]=="unload_level_game":
                        body=source[match.start():matches[i+1].start() if i+1<len(matches) else len(source)]
            self.assertIsNotNone(body)
            emitted=generator.owned_unload_cpu(body,revision)
            self.assertLess(emitted.index('dkr_probe_scene_unload_begin();'),emitted.index('ctx->r29 ='))
            for name in ("level_free","transition_begin","reset_particles","hud_free","free_game_text_table"):
                self.assertEqual(emitted.count(name+'(rdram, ctx);'),1)
            self.assertEqual(emitted.count('dkr_probe_scene_unload_render_drained();'),1)
            self.assertIn('dkr_probe_scene_unload_end();\n    return;',emitted)
            for name in ("dkr_netplay_gameplay_level_end","gfxtask_wait","osRecvMesg_recomp"):
                self.assertNotIn(name,emitted)
            with self.assertRaises(ValueError):generator.owned_unload_cpu(body+"\n",revision)

    def test_video_cpu_refuses_unreviewed_inputs(self):
        for revision in (77,80,81):
            with self.assertRaises(ValueError):generator.authored_video_cpu("","",revision)

    def test_video_cpu_retains_retail_blackout_and_roles(self):
        root=Path("G:/DiddykongWorkFolder" if os.name=="nt" else "/mnt/g/DiddykongWorkFolder")
        for revision in (77,80):
            source_dir=root/f"water-performance-20260926/pipeline/generated-v{revision}"
            if not source_dir.exists():self.skipTest("Private reviewed video inputs unavailable")
            bodies={}
            for path in source_dir.glob("*.c"):
                source=path.read_text(encoding="utf-8");matches=list(generator.DEF.finditer(source))
                for i,m in enumerate(matches):
                    if m[1] in ("fb_update","fb_swap"):
                        bodies[m[1]]=source[m.start():matches[i+1].start() if i+1<len(matches) else len(source)]
            emitted=generator.authored_video_cpu(bodies["fb_update"],bodies["fb_swap"],revision)
            for name in ("osViBlack_recomp","fb_swap"):self.assertEqual(emitted.count(name+"(rdram, ctx);"),1)
            for name in ("osRecvMesg_recomp","osViSwapBuffer_recomp"):self.assertNotIn(name,emitted)
            self.assertIn("ctx->r17 == ctx->r1",emitted) # Real skip-buffer branch retained.
            self.assertIn("MEM_W(0X28, ctx->r29) = 2;",emitted)
            self.assertIn("ctx->r29 = ADD32(ctx->r29, -0X30);",emitted)
            self.assertIn("ctx->r29 = ADD32(ctx->r29, 0X30);",emitted)
            import re
            labels=set(re.findall(r"^\s*(\w+):",emitted,re.M))
            for target in re.findall(r"\bgoto\s+(\w+)",emitted):self.assertIn(target,labels)
            for which in ("fb_update","fb_swap"):
                changed=dict(bodies);changed[which]+="\n"
                with self.assertRaises(ValueError):generator.authored_video_cpu(changed["fb_update"],changed["fb_swap"],revision)

    def test_video_black_abi_is_exact(self):
        name="osViBlack_recomp"
        self.assertIn("dkr_probe_native",generator.native_stub(name,("void",["uint8_t*","recomp_context*"])))
        with self.assertRaises(ValueError):generator.native_stub(name,("int",["uint8_t*","recomp_context*"]))

    def test_authored_cpu_refuses_unreviewed_main_body(self):
        for revision in (77,80,81):
            with self.assertRaises(ValueError):
                generator.authored_main_cpu("RECOMP_FUNC void main_game_loop() {}",revision)

    def test_authored_cpu_exact_readonly_phases(self):
        root=Path("G:/DiddykongWorkFolder" if os.name=="nt" else "/mnt/g/DiddykongWorkFolder")
        for revision in (77,80):
            path=root/f"water-performance-20260926/pipeline/generated-v{revision}/funcs_14.c"
            if not path.exists():self.skipTest("Private reviewed main-loop inputs unavailable")
            source=path.read_text(encoding="utf-8")
            matches=list(generator.DEF.finditer(source))
            i=next(i for i,m in enumerate(matches) if m[1]=="main_game_loop")
            body=source[matches[i].start():matches[i+1].start()]
            emitted=generator.authored_main_cpu(body,revision)
            for name in ("rsp_segment", "rsp_init", "rdp_init", "bgdraw_render", "input_update",
                         "mode_intro", "mode_menu", "mode_game", "sound_update_queue", "menu_missing_controller",
                         "copy_viewports_to_stack", "mempool_free_queue_clear", "disable_cutscene_camera"):
                self.assertIn(name+"(rdram, ctx);",emitted)
            for external in ("gfxtask_run_xbus", "gfxtask_wait", "fb_update",
                             "dkr_netplay_authoritative_frame_commit", "dkr_presentation_frame_begin",
                             "dkr_telemetry_simulation_tick"):
                self.assertNotIn(external+"(rdram, ctx);",emitted)
            self.assertIn("ctx->r29 = ADD32(ctx->r29, -0X18);",emitted)
            self.assertIn("ctx->r29 = ADD32(ctx->r29, 0X18);",emitted)
            self.assertIn("0XE900 << 16",emitted)
            self.assertIn("0XB800 << 16",emitted)
            self.assertEqual(emitted.count("input_update(rdram, ctx);"),1)
            self.assertEqual(emitted.count("mempool_free_queue_clear(rdram, ctx);"),1)
            self.assertEqual(emitted.count("dmacopy_doubleword(rdram, ctx);"),1)
            labels=set(__import__('re').findall(r"^\s*(\w+):",emitted,__import__('re').M))
            for target in __import__('re').findall(r"\bgoto\s+(\w+)",emitted):
                self.assertIn(target,labels,"CPU extraction retained a jump to an external phase")
            with self.assertRaises(ValueError):generator.authored_main_cpu(body+"\n",revision)

    def test_owned_pak_abis_are_not_inherited_from_dependency_stubs(self):
        for name,ret in (("osPfsInit_recomp","void"),("osPfsReadWriteFile_recomp","void"),
                         ("osPfsAllocateFile_recomp","void"),("dkr_virtual_pak_preferred_status","int"),
                         ("dkr_virtual_pak_reformat","int")):
            self.assertIn("dkr_probe_native",generator.native_stub(name,(ret,["uint8_t*","recomp_context*"])))
            with self.assertRaises(ValueError):generator.native_stub(name,("int" if ret=="void" else "void",["uint8_t*","recomp_context*"]))

    def test_native_controller_abis_are_not_guessed(self):
        for name in ("osContGetReadData_recomp", "osContStartReadData_recomp",
                     "osPfsIsPlug_recomp", "osMotorInit_recomp", "osMotorStart_recomp",
                     "osMotorStop_recomp", "__osMotorAccess_recomp",
                     "dkr_netplay_resolve_authored_input_frame"):
            emitted = generator.native_stub(name, ("void", ["uint8_t*", "recomp_context*"]))
            self.assertIn(f'dkr_probe_native("{name}"', emitted)
            for bad in (("int", ["uint8_t*", "recomp_context*"]),
                        ("void", ["uint8_t*", "recomp_context*", "unsigned"])):
                with self.assertRaises(ValueError):
                    generator.native_stub(name, bad)

    def test_native_input_boundary_source_pins(self):
        generator.input_native_audit()

    def test_conditional_return_remains_one_statement(self):
        emitted = generator.instrument_function("sample", "void sample(void) {\nif (hook()) return;\nwork();\n}")
        self.assertIn("if (hook()) { dkr_probe_leave(); return; }", emitted)
        self.assertIn("work();", emitted)
        self.assertEqual(emitted.count("dkr_probe_enter"), 1)

    def test_else_remains_attached(self):
        emitted = generator.instrument_function("sample", "void sample(void) { if (hook()) return; else work(); }")
        self.assertIn("if (hook()) { dkr_probe_leave(); return; } else work();", emitted)

    def test_implicit_return_and_loop_budget(self):
        emitted = generator.instrument_function("sample", "void sample(void) {\nL_80000000:\nwork();\n}")
        self.assertIn("L_80000000: dkr_probe_checkpoint();", emitted)
        self.assertTrue(emitted.endswith("dkr_probe_leave();\n}"))

    def test_native_abis_are_not_guessed(self):
        parsed = generator.declarations("unsigned sample(uint8_t* ram, recomp_context* ctx, unsigned id);")
        self.assertEqual(parsed["sample"], ("unsigned", ["uint8_t*", "recomp_context*", "unsigned"]))
        with self.assertRaises(ValueError):
            generator.declarations("void bad(uint8_t* ram); int bad(uint8_t* ram);")

    def test_specific_pointer_abi_and_unsigned_result(self):
        signature = ("int", ["uint8_t*", "recomp_context*", "unsigned", "const uint32_t*"])
        self.assertIn("return dkr_probe_native_fields", generator.native_stub("dkr_legacy_character_menu", signature))
        track_signature=("int",signature[1]+["unsigned"])
        self.assertIn("return dkr_probe_native_fields",generator.native_stub("dkr_legacy_track_menu",track_signature))
        with self.assertRaises(ValueError):generator.native_stub("dkr_legacy_track_menu",signature)
        with self.assertRaises(ValueError):
            generator.native_stub("dkr_legacy_character_menu", ("int", signature[1][:-1]))
        # Another pointer-taking import must not inherit this audited exception.
        self.assertIn("dkr_probe_block", generator.native_stub("unknown", signature))
        self.assertIn("return dkr_probe_native", generator.native_stub("sample", ("unsigned", ["uint8_t*", "recomp_context*"])))

    def test_status_register_services_have_exact_abis(self):
        self.assertIn("return dkr_probe_cop0_read", generator.native_stub("cop0_status_read", ("gpr", ["recomp_context*"])))
        self.assertIn("dkr_probe_cop0_write", generator.native_stub("cop0_status_write", ("void", ["recomp_context*", "gpr"])))
        with self.assertRaises(ValueError):
            generator.native_stub("cop0_status_read", ("int", ["recomp_context*"]))

    def test_only_audited_disabled_profiler_signatures(self):
        self.assertIn("dkr_probe_profile_disabled", generator.native_stub("dkr_water_profile_begin", ("void", ["uint32_t"])))
        self.assertIn("dkr_probe_profile_disabled", generator.native_stub("dkr_water_profile_end", ("void", ["uint8_t*", "uint32_t"])))
        with self.assertRaises(ValueError):
            generator.native_stub("dkr_water_profile_end", ("void", ["uint8_t*", "recomp_context*"]))
        self.assertIn("dkr_probe_block", generator.native_stub("unreviewed_profiler", ("void", ["uint32_t"])))

    def test_owned_asset_abis_remain_exact(self):
        self.assertIn("dkr_probe_native",generator.native_stub("dkr_legacy_pi_start_dma",("void",["uint8_t*","recomp_context*"])))
        for name in ("osRecvMesg_recomp","dkr_v11_asset_mutex_release","dkr_custom_tracks_asset_load_end"):
            with self.assertRaises(ValueError):
                generator.native_stub(name,("int",["uint8_t*","recomp_context*"]))
        with self.assertRaises(ValueError):
            generator.native_stub("dkr_legacy_asset_api",("int",["uint8_t*","recomp_context*","int"]))

    def test_owned_eeprom_abis_remain_exact(self):
        for name in ("osEepromProbe_recomp","osEepromRead_recomp","osEepromWrite_recomp","osEepromLongRead_recomp","osEepromLongWrite_recomp"):
            self.assertIn("dkr_probe_native",generator.native_stub(name,("void",["uint8_t*","recomp_context*"])))
            with self.assertRaises(ValueError):
                generator.native_stub(name,("int",["uint8_t*","recomp_context*"]))

    def test_math_and_owned_clock_abis_remain_exact(self):
        for name in ("__f_to_ll_recomp","__ll_to_f_recomp","__ull_rshift_recomp",
                     "__ll_lshift_recomp","__ull_div_recomp","__ull_rem_recomp",
                     "osGetCount_recomp","osGetTime_recomp","osSetTime_recomp"):
            self.assertIn("dkr_probe_native",generator.native_stub(name,("void",["uint8_t*","recomp_context*"])))
            with self.assertRaises(ValueError):
                generator.native_stub(name,("int",["uint8_t*","recomp_context*"]))

    def test_owned_audio_guard_abis_remain_exact(self):
        for name,ret in (("dkr_audio_event_queue_next","uint32_t"),
                         ("dkr_audio_voice_guard","int"),("dkr_audio_bus_guard","int")):
            self.assertIn("return dkr_probe_native",generator.native_stub(name,(ret,["uint8_t*","recomp_context*"])))
            for bad in (("void",["uint8_t*","recomp_context*"]),
                        (ret,["uint8_t*","recomp_context*","unsigned"])):
                with self.assertRaises(ValueError): generator.native_stub(name,bad)

    def test_owned_magic_code_abis_remain_exact(self):
        for name in ("dkr_apply_launch_magic_codes","dkr_magic_code_credits_started",
                     "dkr_magic_code_balloon_awarded","dkr_magic_codes_frame_complete"):
            self.assertIn("dkr_probe_native",generator.native_stub(name,("void",["uint8_t*","recomp_context*"])))
            with self.assertRaises(ValueError):generator.native_stub(name,("int",["uint8_t*","recomp_context*"]))

    def test_full_scene_presentation_abis_remain_exact(self):
        for name in ("dkr_track_select_fullscreen_preview", "dkr_track_select_background_cover",
                     "dkr_track_select_lens_flare_tint_begin", "dkr_track_select_lens_flare_tint_end",
                     "dkr_custom_tracks_track_id_override"):
            self.assertIn("dkr_probe_native", generator.native_stub(name,("void",["uint8_t*","recomp_context*"])))
            with self.assertRaises(ValueError):
                generator.native_stub(name,("int",["uint8_t*","recomp_context*"]))

    def test_scene_cut_refuses_unreviewed_payloads(self):
        for revision in (77,80,81):
            with self.assertRaises(ValueError):
                generator.mode_scene_cuts("RECOMP_FUNC void mode_game(uint8_t* rdram, recomp_context* ctx) {}",revision)

    def test_scene_cut_actual_readonly_payloads(self):
        # Optional private payloads: never download/generate/modify them here.
        root=Path("G:/DiddykongWorkFolder" if os.name=="nt" else "/mnt/g/DiddykongWorkFolder")
        for revision,index in ((77,26),(80,27)):
            path=root/f"water-performance-20260926/pipeline/generated-v{revision}/funcs_{index}.c"
            if not path.exists(): self.skipTest("Private reviewed pipeline inputs are unavailable")
            source=path.read_text(encoding="utf-8")
            matches=list(generator.DEF.finditer(source))
            selected=next(i for i,m in enumerate(matches) if m[1]=="mode_game")
            body=source[matches[selected].start():matches[selected+1].start() if selected+1<len(matches) else len(source)]
            result=generator.mode_scene_cuts(body,revision)
            self.assertEqual(result.count("unload_level_game(rdram, ctx);"),2)
            self.assertEqual(result.count("dkr_probe_scene_cut("),2)
            self.assertLess(result.index("case 1: goto dkr_probe_unload_1"),result.index("ctx->r29 = ADD32"))
            self.assertIn("if (dkr_probe_scene_cut(2)) { dkr_probe_leave(); return; }",
                          generator.instrument_function("mode_game",result))
            with self.assertRaises(ValueError): generator.mode_scene_cuts(body+"\n",revision)

    def test_private_audio_callbacks_have_exact_retained_guest_addresses(self):
        root=Path("G:/DiddykongWorkFolder" if os.name=="nt" else "/mnt/g/DiddykongWorkFolder")
        for revision in (77,80):
            source_dir=root/f"water-performance-20260926/pipeline/generated-v{revision}"
            if not source_dir.exists(): self.skipTest("Private reviewed pipeline inputs are unavailable")
            bodies={}
            for path in source_dir.glob("*.c"):
                source=path.read_text(encoding="utf-8")
                matches=list(generator.DEF.finditer(source))
                for i,match in enumerate(matches):
                    if match[1] in generator.AUDIO_CALLBACKS:
                        bodies[match[1]]=source[match.start():matches[i+1].start() if i+1<len(matches) else len(source)]
            dispatch=generator.audio_dispatch(bodies,revision)
            self.assertEqual(set(dispatch.values()),set(generator.AUDIO_CALLBACKS))
            self.assertEqual(len(dispatch),19)
            self.assertEqual(dispatch[0x80002E38],"__amDMA")
            corrupt=dict(bodies); corrupt["__amDMA"]=corrupt["__amDMA"].replace("0x80002E38:","0x80002E39:",1)
            with self.assertRaises(ValueError): generator.audio_dispatch(corrupt,revision)
            del corrupt["alSavePull"]
            with self.assertRaises(ValueError): generator.audio_dispatch(corrupt,revision)

    def test_private_rsp_copy_is_checked_and_budgeted(self):
        headers,source=generator.audio_rsp_payload()
        self.assertIn("static_assert(std::is_trivially_destructible_v<RSP>)",source)
        self.assertIn("do_indirect_jump: dkr_probe_checkpoint();",source)
        for name,write in (("rd_len",0),("wr_len",1)):
            self.assertIn(f'dkr_probe_audio_dma(rdram, dmem, dmem_addr, dram_addr & 0xFFFFF8U, {name}, {write}, __FILE__, __LINE__)',headers["rsp.hpp"])
        # The C fault boundary owns the inclusive-length check, before any
        # copy or +1. The generated RSP may not duplicate/evade that boundary.
        boundary=(Path(__file__).parent/"probe_bridge.c").read_text()
        self.assertLess(boundary.index('inclusive_length >= 4096 - dmem_address'),boundary.index('const unsigned bytes = inclusive_length + 1'))
        self.assertLess(boundary.index('inclusive_length >= probe.bytes - dram_address'),boundary.index('if (write) ram[dram_lane]'))
        self.assertNotIn('#include "ultramodern/ultra64.h"',headers["rsp.hpp"])

    def test_audited_native_source_contracts_do_not_drift(self):
        root=Path(__file__).resolve().parents[3]
        pins={
            "runtime-recomp/src/game/runtime_stubs.cpp":"2552788db2decd11c30ae8d6609c42d51d3072578118e4a1d1f3bbd7cec34f31",
            "runtime-recomp/src/game/runtime_enhancements.cpp":"7a868375b1a8e4e0f3e5e1c60036795fa2630e7d4b3cd6e954308495743918de",
            "runtime-recomp/src/game/runtime_hud_layout.cpp":"9ec1d648e835dfb6b61495f888a6685d10ce75daf3137c7308e368709ab4d7ad",
            # Audited camera-clearance metadata: read-only guest observations
            # and local matrix bindings; no simulation/checkpoint writes.
            "runtime-recomp/src/game/presentation_identity.cpp":"b145359db534a8ac4aa2d2c3a39a2dde91a0ef409b1b77e6e19fc10679297a42",
            "runtime-recomp/src/game/widescreen_policy.hpp":"b926703163fa542f8fbd7af1aee297595c1a58a74c61a82d457c8fbae4ccbb62",
            "extern/dkr-decomp/src/save_data.c":"898df1a185c17e1d6ba11302fc5c140a1d7bd10ff3d8b86ff937c834bb13a607",
            "extern/dkr-decomp/src/thread3_main.c":"437434837706ee88e91578ce9e1aa2cd960e65e1e3a6f1aa47ae3b6289892d8e",
            "extern/dkr-decomp/src/audiomgr.c":"ba7b39838f59ede6eceb7406ce6f92518175458398bddd52cdef55b62a2d8055",
            "extern/dkr-decomp/libultra/src/audio/mips1/drvrnew.c":"adccd2a880176419d68edf400f4ebd3d39c40096a8ea34de2eecaa3932a84987",
            "extern/dkr-decomp/libultra/src/audio/mips1/synthesizer.c":"36ed1d2307122eb7b8664d73c97f349e9daf661d06ed5654bdd78ee6247d8b1d",
            # Pipeline patches 0027/0028 freeze live save ownership and flush
            # joined producers. The owned EEPROM uses its own memory backend;
            # ROM DMA/PIO semantics in this source remain unchanged.
            "extern/n64-modern-runtime/librecomp/src/pi.cpp":"658f386eaa8ec1e40b15c7c653a4c8399f11176f06d861a1efcfdcd80cb4fe7e",
            "extern/n64-modern-runtime/ultramodern/src/misc_ultra.cpp":"61786922d547210c4d974845bb88776f06e935f2daa1e0d112364c63e0a96eec",
            "extern/n64-modern-runtime/librecomp/src/math_routines.cpp":"c8a64f63870418e80eb8e3f5d4beb5bfca426768c404a3a67c2706a6a79bda42",
            "extern/n64-modern-runtime/librecomp/src/ultra_translation.cpp":"e42a884a40253863ce9acfa57756af8c43e9114609afaf28c4883360b06c5324",
            "extern/n64-modern-runtime/ultramodern/src/timer.cpp":"07bec0f6617c0d9965a263ae6f41d8785cd380f5d584df8a844bc83b2cc89211",
        }
        for path,expected in pins.items():
            self.assertEqual(hashlib.sha256((root/path).read_bytes()).hexdigest(),expected,
                             f"Re-audit the private native contract after source changes: {path}")


if __name__ == "__main__":
    unittest.main()
