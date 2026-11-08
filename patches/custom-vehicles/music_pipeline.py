"""Bounded selected-course PCM, composed over admitted source copies only."""
from pathlib import Path
from pipeline import once

AUTH=Path(__file__).resolve().parent


def runtime_resources(text):
    text='#include "resident_music.hpp"\n'+text
    start=text.index('    auto music=std::make_shared<MusicLibrary>();std::size_t music_budget=128*MiB;')
    end=text.index('    out->music_=std::move(music);',start)+len('    out->music_=std::move(music);')
    text=text[:start]+(AUTH/'native/resident_music_prepare.inl').read_text()+text[end:]
    text=once(text,'prepared->prepared_bytes()+128*MiB-music_budget','prepared->prepared_bytes()+music_encoded_bytes')
    # Do not let an old representation reuse this process's retained resource
    # cache. The exact compressed/decoded proofs remain in peer fingerprints.
    text=once(text,'const auto key=out->profile_->digest+":"+sha256(bytes);',
        'const auto key=out->profile_->digest+":"+sha256(bytes)+":selected-music-v1";')
    return text


def runtime_binding(text):
    text='#include "resident_music.hpp"\n'+text
    text=once(text,'    mods::online::MusicState state;', '    mods::online::resident_music::State state;')
    text=once(text,'explicit MusicBinding(mods::online::MusicState value)',
        'explicit MusicBinding(mods::online::resident_music::State value)')
    text=once(text,'    if(name=="prepare_memory") {',
        '    if(name=="prepare_memory") {\n        mods::online::resident_music::PreparationScope ordinary_load(!world.private_owner);')
    # ModWorld::dispatch has a separate private hook, which delegates to this
    # same frozen_course_hook. No tick/restore/audio path grants preparation.
    return text


def driver(text):
    text='#include "resident_music.hpp"\n'+text
    anchor='        const auto result=run_private(dkr_probe_scene_load_cpu,'
    text=once(text,anchor,'        dkr::mods::online::resident_music::PreparationScope music_loading;\n'+anchor)
    anchor='        const auto result=run_private(background ? dkr_probe_menu_background_resume : dkr_probe_authored_scene_resume,'
    text=once(text,anchor,'        dkr::mods::online::resident_music::PreparationScope music_loading(true,!background);\n'+anchor)
    return text
