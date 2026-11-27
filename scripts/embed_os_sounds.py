#!/usr/bin/env python3
"""Embed the retained 8BIT COMPOSER WAV exports as bounded PCM16 arrays."""
import array
import hashlib
from pathlib import Path
import wave

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "sounds"
OUTPUT = ROOT / "platform" / "violet" / "ui" / "sounds"
EVENTS = ("tap", "drop", "navigate", "notify", "capture", "record", "warning", "charge")


def read_clip(name):
    path = SOURCE / f"qeaf-{name}.wav"
    with wave.open(str(path), "rb") as wav:
        assert wav.getnchannels() == 1, path
        assert wav.getsampwidth() == 2, path
        assert wav.getframerate() == 22050, path
        assert wav.getnframes() <= 22050, path
        samples = array.array("h", wav.readframes(wav.getnframes()))
    if len(samples) < 2 or max(abs(value) for value in samples) < 100:
        raise ValueError(f"empty or silent clip: {path}")
    return samples, hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    clips = {name: read_clip(name) for name in EVENTS}
    header = [
        "/* SPDX-License-Identifier: MIT */",
        "#ifndef VQEAF_SOUND_DATA_H",
        "#define VQEAF_SOUND_DATA_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    const int16_t *samples;",
        "    size_t count;",
        "} vqeaf_sound_clip_t;",
        "",
    ]
    source = [
        "/* SPDX-License-Identifier: MIT */",
        '#include "sounds/vqeaf_sound_data.h"',
        "",
    ]
    for name in EVENTS:
        samples, _ = clips[name]
        header.append(f"extern const int16_t vqeaf_sound_{name}[{len(samples)}];")
        source.append(f"const int16_t vqeaf_sound_{name}[{len(samples)}] = {{")
        for start in range(0, len(samples), 12):
            values = ", ".join(str(value) for value in samples[start:start + 12])
            source.append(f"    {values},")
        source.extend(["};", ""])
    source.append("static const vqeaf_sound_clip_t clips[VQEAF_SOUND_COUNT] = {")
    for name in EVENTS:
        source.append(f"    {{ vqeaf_sound_{name}, {len(clips[name][0])} }},")
    source.extend([
        "};",
        "",
        "const vqeaf_sound_clip_t *vqeaf_sound_clip(vqeaf_sound_event_t event)",
        "{",
        "    if((unsigned int)event >= VQEAF_SOUND_COUNT) return NULL;",
        "    return &clips[event];",
        "}",
        "",
    ])
    header.extend([
        "const vqeaf_sound_clip_t *vqeaf_sound_clip(vqeaf_sound_event_t event);",
        "",
        "#endif",
        "",
    ])
    # The C implementation includes the enum through vqeaf_sound.h before this header.
    header.insert(4, '#include "vqeaf_sound.h"')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "vqeaf_sound_data.h").write_text("\n".join(header), encoding="utf-8")
    (OUTPUT / "vqeaf_sound_data.c").write_text("\n".join(source), encoding="utf-8")
    for name in EVENTS:
        samples, digest = clips[name]
        print(f"{name}: {len(samples)} frames, {digest}")


if __name__ == "__main__":
    main()
