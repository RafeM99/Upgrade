#!/usr/bin/env python3
"""generate_vo.py (akshat): character voiceover for Upgrade, made once with ElevenLabs.

Collects every spoken line in the game:
  * the boss cutscenes        assets/cutscenes.txt   ("speaker: text")
  * the story memory cards    assets/strings.txt     (story_*_N, end_N; "GRAN:" / "SAM:" or narration)
and writes one MP3 per line to assets/vo/<key>.mp3, where key = FNV-1a 64 of "speaker|text"
(the game computes the same key in src/audio/Voice.cpp to find the file). Lines that already
have a file are skipped, so only new or changed lines ever cost credits.

FREE PLAN SAFE: before generating it asks ElevenLabs how many characters are left this month and
never spends more than SAFETY of them. If everything doesn't fit it voices the most important
lines first (boss scenes, then Gran and Sam on the cards, narration last) and stops.

The key is read from the environment (ELEVENLABS_API_KEY), loaded from the project's .env.local.
It is never printed or written anywhere.

  python3 tools/voice/generate_vo.py            # dry run: counts, budget, voice picks; no speech made
  python3 tools/voice/generate_vo.py --go       # generate what fits in the budget
  python3 tools/voice/generate_vo.py --go --no-narration   # Gran, Sam and bosses only
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets"
OUT = ASSETS / "vo"
VOICES_FILE = Path(__file__).with_name("voices.json")
SAFETY = 0.85          # never use more than 85% of what is left this month
MODEL_COST = {"eleven_v4": 1.0, "eleven_multilingual_v2": 1.0, "eleven_v3": 1.0,
              "eleven_flash_v2_5": 0.5, "eleven_flash_v2": 0.5}

# ------------------------------------------------------------------ the key the game uses
def vo_key(speaker: str, text: str) -> str:
    h = 0xcbf29ce484222325
    for b in f"{speaker}|{text}".encode("utf-8"):
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


# ------------------------------------------------------------------ collect lines
def read_strings():
    out = {}
    for line in (ASSETS / "strings.txt").read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        out[k.strip()] = v.strip().replace("\\n", " ")
    return out


def card_lines(strings):
    """(priority, speaker, text) for every memory-card / ending line, in game order."""
    lines = []
    prefixes = ["story_prologue"] + [f"story_{i}" for i in range(7)] + ["end"]
    for pre in prefixes:
        i = 1
        while f"{pre}_{i}" in strings:
            s = strings[f"{pre}_{i}"]
            if s.startswith("GRAN:"):
                lines.append((1, "gran", s[5:].strip()))
            elif s.startswith("SAM:"):
                lines.append((1, "sam", s[4:].strip()))
            else:
                lines.append((2, "narrator", s.strip()))
            i += 1
    return lines


def cutscene_lines():
    lines = []
    for raw in (ASSETS / "cutscenes.txt").read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if not s or s.startswith("#") or s.startswith("["):
            continue
        m = re.match(r"^([a-z_]+)\s*:\s*(.+)$", s)
        speaker, text = (m.group(1), m.group(2).strip()) if m else ("narrator", s)
        lines.append((0, speaker, text))
    return lines


# ------------------------------------------------------------------ voices
def pick_voices(client, wanted):
    """Resolve each character to a voice_id: an explicit id in voices.json wins, else the first
    premade voice whose labels match the character's description."""
    voices = client.voices.get_all().voices
    picks = {}
    for who, spec in wanted.items():
        if spec.get("voice_id"):
            picks[who] = (spec["voice_id"], spec.get("voice_name", "(set in voices.json)"))
            continue
        best, best_score = None, -1
        for v in voices:
            labels = {k: str(val).lower() for k, val in (v.labels or {}).items()}
            score = 0
            for k, want in spec.get("labels", {}).items():
                got = labels.get(k, "")
                if want.lower() in got:
                    score += 2
            if v.name and v.name.lower() in [n.lower() for n in spec.get("prefer", [])]:
                score += 5
            if score > best_score:
                best, best_score = v, score
        picks[who] = (best.voice_id, best.name) if best else (None, None)
    return picks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--go", action="store_true", help="actually generate speech (uses credits)")
    ap.add_argument("--no-narration", action="store_true", help="skip narration lines")
    ap.add_argument("--model", default=None, help="override the model in voices.json")
    args = ap.parse_args()

    try:
        from dotenv import load_dotenv
        from elevenlabs import ElevenLabs, VoiceSettings
    except ImportError:
        sys.exit("Missing packages: pip install -r tools/voice/requirements.txt")
    load_dotenv(ROOT / ".env.local")
    if not os.environ.get("ELEVENLABS_API_KEY"):
        sys.exit("ELEVENLABS_API_KEY is not set (run the ElevenLabs onboarding in the project folder).")
    client = ElevenLabs(api_key=os.environ["ELEVENLABS_API_KEY"])   # the SDK reads the env at import, before .env.local loads

    cfg = json.loads(VOICES_FILE.read_text(encoding="utf-8"))
    model = args.model or cfg.get("model", "eleven_v4")
    fmt = cfg.get("output_format", "mp3_44100_64")

    # every line, most important first, de-duplicated by key
    lines = cutscene_lines() + card_lines(read_strings())
    if args.no_narration:
        lines = [l for l in lines if l[1] != "narrator"]
    seen, todo, have = set(), [], 0
    OUT.mkdir(parents=True, exist_ok=True)
    for pri, who, text in sorted(lines, key=lambda l: l[0]):
        k = vo_key(who, text)
        if k in seen:
            continue
        seen.add(k)
        if (OUT / f"{k}.mp3").exists():
            have += 1
            continue
        todo.append((pri, who, text, k))

    # budget
    sub = client.user.subscription.get()
    left = max(0, sub.character_limit - sub.character_count)
    budget = int(left * SAFETY)
    cost_k = MODEL_COST.get(model, 1.0)
    need = int(sum(len(t) for _, _, t, _ in todo) * cost_k)
    print(f"Plan: {sub.tier} . used {sub.character_count:,} of {sub.character_limit:,} this month . left {left:,}")
    print(f"Model {model} (x{cost_k} per character). Budget for this run: {budget:,} ({int(SAFETY*100)}% of what's left)")
    print(f"Lines: {len(seen)} total, {have} already voiced, {len(todo)} to make, about {need:,} credits")
    by = {}
    for _, who, t, _ in todo:
        by[who] = by.get(who, 0) + len(t)
    for who, n in sorted(by.items(), key=lambda kv: -kv[1]):
        print(f"   {who:12s} {n:6,} characters")

    picks = pick_voices(client, cfg["voices"])
    print("Voices:")
    for who, (vid, name) in picks.items():
        print(f"   {who:12s} -> {name} ({vid})")

    if not args.go:
        print("\nDry run only: nothing was generated and no credits were used. Add --go to make the speech.")
        return

    spent, made, skipped = 0, 0, 0
    manifest = OUT / "manifest.txt"
    with manifest.open("a", encoding="utf-8") as mf:
        for pri, who, text, k in todo:
            cost = int(len(text) * cost_k) + 1
            if spent + cost > budget:
                skipped += 1
                continue
            vid = picks.get(who, (None,))[0] or picks.get("narrator", (None,))[0]
            spec = cfg["voices"].get(who, {})
            settings = VoiceSettings(stability=spec.get("stability", 0.5),
                                     similarity_boost=spec.get("similarity_boost", 0.75))
            try:
                audio = client.text_to_speech.convert(text=text, voice_id=vid, model_id=model,
                                                      output_format=fmt, voice_settings=settings)
                data = b"".join(audio)
            except Exception as e:   # one bad line never stops the rest
                print(f"   ! {who}: {text[:40]}... failed: {e}")
                continue
            (OUT / f"{k}.mp3").write_bytes(data)
            mf.write(f"{k}\t{who}\t{text}\n")
            spent += cost
            made += 1
            print(f"   + {who:10s} {text[:60]}")
    print(f"\nMade {made} lines, about {spent:,} credits. {skipped} skipped to stay inside the budget.")


if __name__ == "__main__":
    main()
