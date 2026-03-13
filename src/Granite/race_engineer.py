#!/usr/bin/env python3

import json
import os
import wave
import torch
import whisper
import threading
import subprocess
from pynput import keyboard as kb
from transformers import AutoTokenizer, AutoModelForCausalLM

# ── Environment setup (must be before any audio initialisation) ───────────────
# os.environ["PULSE_SERVER"] = "unix:/mnt/wslg/PulseServer"
os.environ["DISPLAY"]      = os.environ.get("DISPLAY", ":0")

# ── Paths ─────────────────────────────────────────────────────────────────────
DATA_DIR   = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData")
STATS_PATH = os.path.join(DATA_DIR, "end_statistics.json")
SPEED_PATH = os.path.join(DATA_DIR, "speed.json")
RAW_PATH   = "/tmp/torcs_question.raw"
WAV_PATH   = "/tmp/torcs_question.wav"

SAMPLE_RATE = 16000
CHANNELS    = 1

# ── Load models ───────────────────────────────────────────────────────────────
print("Loading Whisper...")
whisper_model = whisper.load_model("base")

print("Loading Granite...")
model_name = "ibm-granite/granite-4.0-350m"
tokenizer  = AutoTokenizer.from_pretrained(model_name)
granite    = AutoModelForCausalLM.from_pretrained(
    model_name,
    dtype=torch.float32,
    device_map="cpu"
)
granite.eval()

# ── Recording ─────────────────────────────────────────────────────────────────
def raw_to_wav(raw_path, wav_path, sample_rate=SAMPLE_RATE, channels=CHANNELS):
    """Wrap raw PCM s16le data in a proper WAV header for Whisper."""
    with open(raw_path, "rb") as f:
        raw_data = f.read()
    with wave.open(wav_path, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)  # s16le = 2 bytes per sample
        wf.setframerate(sample_rate)
        wf.writeframes(raw_data)

def record_question(key="r"):
    pressed  = threading.Event()
    released = threading.Event()

    def on_press(key_event):
        try:
            if key_event.char == key:
                pressed.set()
        except AttributeError:
            pass

    def on_release(key_event):
        try:
            if key_event.char == key:
                released.set()
        except AttributeError:
            pass

    listener = kb.Listener(on_press=on_press, on_release=on_release)
    listener.start()

    print("[RaceEngineer] Waiting for R key...")
    pressed.wait()
    print("[RaceEngineer] Recording...")

    # Record raw PCM — parecord writes clean s16le with these flags
    proc = subprocess.Popen(
        [
            "parecord",
            "--channels=1",
            "--rate=16000",
            "--format=s16le",
            "--raw",
            RAW_PATH
        ],
        stdout=None,
        stderr=None
    )

    released.wait()
    proc.terminate()
    proc.wait()

    print("[RaceEngineer] Done recording.")
    listener.stop()

    # Convert raw PCM to WAV for Whisper
    raw_to_wav(RAW_PATH, WAV_PATH)
    os.remove(RAW_PATH)

    return WAV_PATH

# ── Race data ─────────────────────────────────────────────────────────────────
def load_race_context():
    context = {}
    if os.path.exists(STATS_PATH) and os.path.getsize(STATS_PATH) > 0:
        try:
            with open(STATS_PATH, "r") as f:
                context["stats"] = json.load(f)
        except json.JSONDecodeError:
            context["stats"] = {}

    if os.path.exists(SPEED_PATH) and os.path.getsize(SPEED_PATH) > 0:
        try:
            with open(SPEED_PATH, "r") as f:
                lines = [l.strip().rstrip(",") for l in f if l.strip() and l.strip() != ","]
            if lines:
                last = json.loads(lines[-1])
                context["current_speed_ms"] = last.get("speedx", 0)
                context["current_segment"]  = last.get("segment_id", "N/A")
        except Exception:
            pass
    return context

def build_prompt(question, context):
    stats = context.get("stats", {})
    return f"""You are a Formula 1 race engineer giving concise real-time information to your driver. Answer in 1-2 sentences only.

LIVE TELEMETRY:
- Speed: {context.get('speed_kmh', 0):.1f} km/h | Gear: {context.get('gear', 'N/A')} | RPM: {context.get('rpm', 0):.0f}
- Car damage: {context.get('damage', 0)} / 10000
- Nearest opponent: {context.get('opponent_gap', 200):.1f}m
- Distance raced: {context.get('dist_raced', 0):.0f}m

SESSION DATA:
- Laps completed: {stats.get('laps_completed', 'N/A')}
- Best lap: {stats.get('best_lap_time', 0) * 60:.2f}s
- Avg speed: {stats.get('avg_speed_kmh', 0):.1f} km/h

Driver: {question}
Engineer:"""

def ask_granite(question):
    context = load_race_context()
    prompt  = build_prompt(question, context)
    inputs  = tokenizer(prompt, return_tensors="pt")
    with torch.no_grad():
        output = granite.generate(
            **inputs,
            max_new_tokens=80,
            do_sample=False,
            repetition_penalty=1.1,
            pad_token_id=tokenizer.eos_token_id
        )
    response_tokens = output[0][inputs["input_ids"].shape[1]:]
    response = tokenizer.decode(response_tokens, skip_special_tokens=True).strip()
    sentences = response.split(".")
    return ". ".join(sentences[:2]).strip() + "."

# ── TTS ───────────────────────────────────────────────────────────────────────
def speak(text):
    os.system(f'echo "{text}" | festival --tts')

# ── Main loop ─────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    print("[RaceEngineer] Ready. Hold R to ask a question.")
    while True:
        wav      = record_question(key="r")
        result   = whisper_model.transcribe(wav)
        question = result["text"].strip()
        os.remove(wav)

        if not question:
            print("[RaceEngineer] Nothing heard, try again.")
            continue

        print(f"[RaceEngineer] Driver: {question}")
        response = ask_granite(question)
        print(f"[RaceEngineer] Engineer: {response}")
        speak(response)