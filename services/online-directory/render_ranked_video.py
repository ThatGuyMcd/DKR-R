#!/usr/bin/env python3
"""Render a 16:9, 60 fps video from a replay verified at its native cadence."""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time

PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))
from verify_time_trial import (  # noqa: E402
    ADVENTURE_SPLIT_RULES,
    CUSTOM_RULES,
    MAX_ADVENTURE_BYTES,
    file_digest,
    replay_metadata,
    runtime_binary,
    verification_environment,
    verification_result,
)

WIDTH, HEIGHT = 1920, 1080
REPLAY_HZ, VIDEO_FPS = 30, 60
BOOKEND_FRAMES = 2 * VIDEO_FPS
FADE_FRAMES = VIDEO_FPS // 2
BRAND_LOGO = PROJECT_ROOT / "assets/ui/Icons/diddy_kong_racing_recompiled_ico_by_pooterman.png"
SAMPLES_PER_UPDATE = 48000 // REPLAY_HZ
FRAME_TAG_SIZE = 4
MAX_MOVIE_BYTES = 512 * 1024 * 1024


def encoding(count, adventure):
    if not adventure:
        return ["-preset", "veryfast", "-crf", "23"], 128_000, HEIGHT
    # Leave room for container overhead and bitrate variation in the existing
    # 512 MiB archive. The game still draws the overlays at capture resolution.
    total_rate = int(MAX_MOVIE_BYTES * 0.88 * 8 * REPLAY_HZ / count)
    audio_rate = 80_000 if total_rate >= 500_000 else 48_000
    video_rate = min(3_000_000, total_rate - audio_rate)
    height = 720 if video_rate >= 1_000_000 else 540 if video_rate >= 450_000 else 360
    return (
        [
            "-preset",
            "veryfast",
            "-b:v",
            str(video_rate),
            "-maxrate",
            str(video_rate),
            "-bufsize",
            str(video_rate * 2),
        ],
        audio_rate,
        height,
    )


def picture_filter(height, gameplay_frames, credit):
    filters = []
    if height != HEIGHT:
        filters.append(f"scale={height * 16 // 9}:{height}:flags=lanczos")
    # RT64 already produced the output cadence; FFmpeg only encodes it.
    filters.extend(
        [
            "null",
            f"trim=end_frame={gameplay_frames}",
            f"settb=1/{VIDEO_FPS}",
            "setpts=N",
        ]
    )
    hold = BOOKEND_FRAMES - FADE_FRAMES
    outro_start = BOOKEND_FRAMES + gameplay_frames
    margin = max(2, height // 50)
    mark_size = height // 11
    centre_size = height // 2
    credit_filter = (
        f",drawtext=font='DejaVu Sans':textfile=racer-credit.txt:expansion=none:"
        f"fontsize={round(height * 0.032)}:fontcolor=white@0.80:shadowcolor=black@0.8:"
        f"shadowx=1:shadowy=1:x={margin}:y=h-th-{margin}"
        if credit
        else ""
    )
    # The transitions fade over held pictures. The original gameplay pictures
    # and the corresponding audio start together after the full intro.
    return ";".join(
        [
            "[0:v]" + ",".join(filters) + "[game]",
            "[1:v]format=rgba,split=2[mark_source][title_source]",
            f"[mark_source]scale={mark_size}:{mark_size}:flags=lanczos,format=rgba,colorchannelmixer=aa=0.34[mark]",
            f"[game][mark]overlay=x=W-w-{margin}:y=H-h-{margin}{credit_filter}[marked]",
            f"[marked]tpad=start_mode=clone:start={FADE_FRAMES}:stop_mode=clone:stop={FADE_FRAMES},"
            f"fade=t=in:s=0:n={FADE_FRAMES},fade=t=out:s={gameplay_frames + FADE_FRAMES}:n={FADE_FRAMES - 1},"
            f"tpad=start={hold}:stop={hold}[stage]",
            f"[title_source]scale={centre_size}:{centre_size}:flags=lanczos,format=rgba,"
            f"loop=loop={BOOKEND_FRAMES - 1}:size=1:start=0,settb=1/{VIDEO_FPS},setpts=N,split=2[lead][tail]",
            f"[lead]fade=t=out:s={hold}:n={FADE_FRAMES - 1}:alpha=1[intro]",
            f"[tail]fade=t=in:s=0:n={FADE_FRAMES}:alpha=1,setpts=PTS+{outro_start}[outro]",
            "[stage][intro]overlay=x=(W-w)/2:y=(H-h)/2:eof_action=pass[opening]",
            "[opening][outro]overlay=x=(W-w)/2:y=(H-h)/2:eof_action=pass[branded]",
        ]
    )


def stop(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def size_capture_window(env: dict, game: subprocess.Popen) -> None:
    deadline = time.monotonic() + 20
    while game.poll() is None and time.monotonic() < deadline:
        match = subprocess.run(
            ["xdotool", "search", "--onlyvisible", "--name", "^DKR-R"],
            env=env,
            capture_output=True,
            text=True,
            timeout=3,
        )
        if match.returncode == 0 and match.stdout.split():
            window = match.stdout.split()[-1]
            subprocess.run(
                ["xdotool", "windowmove", window, "0", "0", "windowsize", window, str(WIDTH), str(HEIGHT)],
                env=env,
                check=True,
                timeout=3,
            )
            return
        time.sleep(0.15)
    raise ValueError("The verifier did not open its capture window.")


class X11Image(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_int),
        ("height", ctypes.c_int),
        ("offset", ctypes.c_int),
        ("format", ctypes.c_int),
        ("data", ctypes.c_void_p),
        ("byte_order", ctypes.c_int),
        ("bitmap_unit", ctypes.c_int),
        ("bitmap_bit_order", ctypes.c_int),
        ("bitmap_pad", ctypes.c_int),
        ("depth", ctypes.c_int),
        ("stride", ctypes.c_int),
        ("bits_per_pixel", ctypes.c_int),
        ("red_mask", ctypes.c_ulong),
        ("green_mask", ctypes.c_ulong),
        ("blue_mask", ctypes.c_ulong),
    ]


class Screen:
    def __enter__(self):
        lib = self.lib = ctypes.CDLL("libX11.so.6")
        lib.XOpenDisplay.argtypes = [ctypes.c_char_p]
        lib.XOpenDisplay.restype = ctypes.c_void_p
        lib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        lib.XDefaultRootWindow.restype = ctypes.c_ulong
        lib.XCloseDisplay.argtypes = [ctypes.c_void_p]
        lib.XDestroyImage.argtypes = [ctypes.POINTER(X11Image)]
        lib.XGetImage.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_ulong,
            ctypes.c_int,
        ]
        lib.XGetImage.restype = ctypes.POINTER(X11Image)
        self.display = lib.XOpenDisplay(None)
        if not self.display:
            raise ValueError("The isolated display could not open.")
        self.root = lib.XDefaultRootWindow(self.display)
        return self

    def __exit__(self, *_):
        self.lib.XCloseDisplay(self.display)

    def pixels(self, x: int, y: int, width: int, height: int) -> bytes:
        image = self.lib.XGetImage(self.display, self.root, x, y, width, height, ~0, 2)
        if not image:
            raise ValueError("The replay picture could not be read.")
        try:
            info = image.contents
            if (
                info.stride != width * 4
                or info.bits_per_pixel != 32
                or info.byte_order != 0
                or (info.red_mask, info.green_mask, info.blue_mask) != (0xFF0000, 0xFF00, 0xFF)
            ):
                raise ValueError("The replay display is not in the expected 32-bit pixel format.")
            return ctypes.string_at(info.data, info.stride * height)
        finally:
            self.lib.XDestroyImage(image)

    def picture(self, frame: int, deadline: float) -> bytes | bytearray:
        tag = bytes((0xA5 ^ (frame >> 16), (frame >> 8) & 255, frame & 255))
        # The marker changes for each swap, including the intermediate and blank VIs.
        while self.pixels(WIDTH - 2, HEIGHT - 2, 1, 1)[:3] != tag:
            if time.monotonic() >= deadline:
                raise ValueError(f"X11 never displayed replay picture {frame}.")
            time.sleep(0.002)
        picture = bytearray(self.pixels(0, 0, WIDTH, HEIGHT))
        for row in range(HEIGHT - FRAME_TAG_SIZE, HEIGHT):
            end = (row + 1) * WIDTH * 4
            start = end - FRAME_TAG_SIZE * 4
            picture[start:end] = picture[start - 4 : start] * FRAME_TAG_SIZE
        return picture


def read_packet(stream):
    prefix = stream.read(4)
    if len(prefix) != 4:
        raise ValueError("The replay renderer stopped before completing a capture packet.")
    return struct.unpack("<I", prefix)[0]


def capture_update(stream, frame, read_picture, write_picture, write_audio, acknowledge):
    """Read one or two RT64 swaps and the tick's exact PCM interval."""
    packet = frame * 4
    if read_packet(stream) != packet + 1:
        raise ValueError(f"Missing first presentation for replay update {frame}.")
    first = read_picture(frame * 2 - 1)
    write_picture(first)
    acknowledge()
    next_packet = read_packet(stream)
    paired = next_packet == packet + 2
    changed = False
    if paired:
        second = read_picture(frame * 2)
        changed = first != second
        write_picture(second)
        acknowledge()
        next_packet = read_packet(stream)
    else:
        # A blank VI or an un-interpolatable scene holds its original render
        # for both display intervals. Never blend it with another update.
        write_picture(first)
    if next_packet != packet:
        raise ValueError(f"Unexpected capture packet for replay update {frame}.")
    samples = stream.read(SAMPLES_PER_UPDATE * 4)
    if len(samples) != SAMPLES_PER_UPDATE * 4:
        raise ValueError(f"Replay update {frame} is missing audio.")
    write_audio(samples)
    acknowledge()
    return paired, changed


def capture_frames(game_args, env, job, count, timeout, adventure, racer_credit, race_updates):
    """Capture each RT64 swap before letting the renderer replace it."""
    raw_audio = job / "audio.m4a"
    raw_movie = job / "picture.mp4"
    bookend_seconds = BOOKEND_FRAMES / VIDEO_FPS
    options, audio_rate, height = encoding(count + 2 * BOOKEND_FRAMES * REPLAY_HZ // VIDEO_FPS, adventure)
    gameplay_frames = count * VIDEO_FPS // REPLAY_HZ
    video_frames = gameplay_frames + 2 * BOOKEND_FRAMES
    if racer_credit:
        (job / "racer-credit.txt").write_text(racer_credit, encoding="utf-8")
    encoder = [
        "ffmpeg",
        "-nostdin",
        "-v",
        "error",
        "-f",
        "rawvideo",
        "-pixel_format",
        "bgr0",
        "-video_size",
        f"{WIDTH}x{HEIGHT}",
        "-framerate",
        str(VIDEO_FPS),
        "-i",
        "pipe:0",
        "-framerate",
        str(VIDEO_FPS),
        "-i",
        str(BRAND_LOGO),
        "-filter_complex_threads",
        "1",
        "-filter_complex",
        picture_filter(height, gameplay_frames, bool(racer_credit)),
        "-map",
        "[branded]",
        "-frames:v",
        str(video_frames),
        "-c:v",
        "libx264",
        "-threads",
        "3",
        *options,
        "-pix_fmt",
        "yuv420p",
        str(raw_movie),
    ]
    mixer = [
        "ffmpeg",
        "-nostdin",
        "-v",
        "error",
        "-f",
        "s16le",
        "-ar",
        "48000",
        "-ac",
        "2",
        "-i",
        "pipe:0",
        "-af",
        f"adelay={int(bookend_seconds * 1000)}:all=1,apad=pad_dur={bookend_seconds}",
        "-c:a",
        "aac",
        "-b:a",
        str(audio_rate),
        str(raw_audio),
    ]
    parent, child = socket.socketpair()
    with (
        parent,
        child,
        Screen() as screen,
        (job / "game.log").open("wb") as game_log,
        (job / "capture.log").open("wb") as errors,
    ):
        env["DKR_REPLAY_CAPTURE_SOCKET"] = str(child.fileno())
        deadline = time.monotonic() + timeout + 15
        game = subprocess.Popen(
            game_args, cwd=job, env=env, stdout=game_log, stderr=subprocess.STDOUT, pass_fds=(child.fileno(),)
        )
        child.close()
        try:
            size_capture_window(env, game)
            with (
                subprocess.Popen(encoder, cwd=job, stdin=subprocess.PIPE, stderr=errors) as video,
                subprocess.Popen(mixer, stdin=subprocess.PIPE, stderr=errors) as audio,
            ):
                try:
                    with parent.makefile("rb") as stream:
                        parent.settimeout(min(90, max(1, deadline - time.monotonic())))
                        if stream.read(4) != b"DKR3":
                            raise ValueError("The runtime does not support native 60 Hz replay capture.")
                        pairs = moving_pairs = race_pairs = race_moving_pairs = 0
                        for frame in range(1, count + 1):
                            parent.settimeout(min(90, max(1, deadline - time.monotonic())))
                            paired, changed = capture_update(
                                stream,
                                frame,
                                lambda tag: screen.picture(tag, min(deadline, time.monotonic() + 10)),
                                video.stdin.write,
                                audio.stdin.write,
                                lambda: parent.sendall(b"K"),
                            )
                            pairs += paired
                            moving_pairs += changed
                            if frame in race_updates:
                                race_pairs += paired
                                race_moving_pairs += changed
                    # A stationary scene can legitimately produce identical pixels in two RT64 swaps.
                    if not pairs or (race_updates and race_pairs * 100 < len(race_updates) * 95):
                        raise ValueError(
                            f"RT64 did not sustain native replay cadence ({race_pairs}/{len(race_updates)} "
                            f"paired race updates, {pairs}/{count} paired updates overall)."
                        )
                    video.stdin.close()
                    audio.stdin.close()
                    if video.wait(timeout=25) or audio.wait(timeout=25):
                        raise ValueError("A replay encoder failed.")
                finally:
                    stop(video)
                    stop(audio)
            if game.wait(timeout=25):
                raise ValueError(f"The replay verifier exited with status {game.returncode}.")
        finally:
            stop(game)
    return raw_movie, raw_audio, pairs, moving_pairs, race_pairs, race_moving_pairs


def render(options: argparse.Namespace) -> dict:
    runtime, rom, replay = (file.resolve(strict=True) for file in (options.runtime, options.rom, options.replay))
    if not BRAND_LOGO.is_file():
        raise ValueError("The DKR-R video logo is missing.")
    binary = runtime_binary(runtime)
    expected_binary = file_digest(binary)
    with replay.open("rb") as source:
        evidence = source.read(MAX_ADVENTURE_BYTES + 1)
    if len(evidence) > MAX_ADVENTURE_BYTES:
        raise ValueError("Evidence exceeds the supported size.")
    replay_sha = hashlib.sha256(evidence).hexdigest()
    metadata = replay_metadata(evidence)
    count = metadata.get("ticks")
    adventure = metadata.get("ruleset") in ADVENTURE_SPLIT_RULES
    if type(count) is not int or not 1 <= count <= (648000 if adventure else 54210):
        raise ValueError("This video renderer needs a completed ranked replay of supported length.")
    if (metadata.get("ruleset") in CUSTOM_RULES) != bool(options.track_package):
        raise ValueError("Custom tracks require their exact track package.")

    job = options.output.resolve()
    job.mkdir(mode=0o700, parents=True, exist_ok=False)
    attempt = job / "input.dkrreplay"
    attempt.write_bytes(evidence)
    profile = job / "profile"
    env = verification_environment(job, xvfb=True)
    env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    temporary = job / "tmp"
    temporary.mkdir(mode=0o700)
    env["TMPDIR"] = str(temporary)
    env.update(SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="x11")
    game_args = [
        str(runtime),
        "--rom",
        str(rom),
        "--config",
        str(profile),
        "--watch-time-trial",
        str(attempt),
        "--timeout",
        str(options.timeout),
    ]
    if options.track_package:
        game_args.extend(("--time-trial-map", str(options.track_package.resolve(strict=True))))

    race_updates = range(0)
    if not adventure:
        finish = metadata.get("finishTick", count)
        # Ranked clock ticks run at 60 Hz. The recorder's updates run at 30 Hz.
        countdown = max(0, finish - metadata["clockTicks"] // (VIDEO_FPS // REPLAY_HZ))
        race_updates = range(countdown + 1, finish + 1)
    raw_movie, raw_audio, pairs, moving_pairs, race_pairs, race_moving_pairs = capture_frames(
        game_args,
        env,
        job,
        count,
        options.timeout,
        adventure,
        options.racer_credit,
        race_updates,
    )

    if file_digest(binary) != expected_binary:
        raise ValueError("The runtime changed during the render.")
    game_log = (profile / "logs/runtime.log").read_text(encoding="utf-8", errors="replace")
    proof = verification_result(game_log, metadata, {"replaySha256": replay_sha, "runtimeSha256": expected_binary})
    if (
        proof["clockTicks"] != metadata["clockTicks"]
        or proof["checkpoints"] != metadata["checkpoints"]
        or proof["updates"] != metadata.get("finishTick", metadata["ticks"])
    ):
        raise ValueError("The captured evidence differs from the supplied replay.")

    movie = job / "verified.mp4"
    subprocess.run(
        [
            "ffmpeg",
            "-nostdin",
            "-v",
            "error",
            "-i",
            str(raw_movie),
            "-i",
            str(raw_audio),
            "-c",
            "copy",
            "-movflags",
            "+faststart",
            str(movie),
        ],
        check=True,
    )
    probe = subprocess.check_output(
        [
            "ffprobe",
            "-v",
            "error",
            "-show_entries",
            "format=duration:stream=codec_name,nb_frames,duration,start_time,avg_frame_rate,width,height",
            "-of",
            "json",
            str(movie),
        ],
        text=True,
    )
    media = json.loads(probe)
    if [stream["codec_name"] for stream in media["streams"]] != ["h264", "aac"]:
        raise ValueError("The video needs H.264 picture and AAC game audio.")
    picture, sound = media["streams"]
    expected_duration = count / REPLAY_HZ + 2 * BOOKEND_FRAMES / VIDEO_FPS
    if (
        int(picture["nb_frames"]) != count * VIDEO_FPS // REPLAY_HZ + 2 * BOOKEND_FRAMES
        or picture["avg_frame_rate"] != f"{VIDEO_FPS}/1"
        or picture["width"] * 9 != picture["height"] * 16
        or any(
            abs(float(stream["duration"]) - expected_duration) > 0.03 or abs(float(stream["start_time"])) > 0.002
            for stream in (picture, sound)
        )
    ):
        raise ValueError("The video and audio did not stay on the authored replay clock.")
    proof["videoSha256"] = file_digest(movie)
    proof["videoDuration"] = float(media["format"]["duration"])
    proof["videoFps"] = VIDEO_FPS
    proof["videoCadence"] = "rt64-present-v1"
    proof["videoPairedUpdates"] = pairs
    proof["videoDistinctPairs"] = moving_pairs
    if race_updates:
        proof["videoRacePairedUpdates"] = race_pairs
        proof["videoRaceDistinctPairs"] = race_moving_pairs
    proof["videoWidth"], proof["videoHeight"] = picture["width"], picture["height"]
    proof["videoInputOverlay"] = "n64-v1"
    proof["videoBranding"] = "dkr-r-logo-v2" if options.racer_credit else "dkr-r-logo-v1"
    if options.racer_credit:
        proof["videoRacerCredit"] = options.racer_credit
    if adventure:
        proof["videoSplitsOverlay"] = "adventure-live-v1"
    (job / "proof.json").write_text(json.dumps(proof, indent=2) + "\n", encoding="utf-8")
    raw_movie.unlink()
    raw_audio.unlink()
    return proof


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "rom", "replay", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--track-package", type=Path)
    parser.add_argument("--racer-credit", default="")
    parser.add_argument("--timeout", type=int, default=400)
    parser.add_argument("--isolated-display", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.timeout < 1:
        parser.error("--timeout must be positive")
    if len(args.racer_credit) > 60 or any(character in args.racer_credit for character in "\r\n\0"):
        parser.error("--racer-credit must be a single line of at most 60 characters.")
    if os.geteuid() == 0:
        parser.error("Run as an unprivileged user.")
    if not args.isolated_display:
        # Give FFmpeg and the guest the same private X11 socket. Isolating the
        # guest alone blocks access to Xvfb's abstract Unix socket.
        return subprocess.run(
            [
                "unshare",
                "--map-current-user",
                "--net",
                "--",
                "xvfb-run",
                "-a",
                "-s",
                f"-screen 0 {WIDTH}x{HEIGHT}x24",
                sys.executable,
                str(Path(__file__).resolve()),
                *sys.argv[1:],
                "--isolated-display",
            ]
        ).returncode
    try:
        print(json.dumps(render(args), indent=2))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"{error}. See capture logs in {args.output}.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
