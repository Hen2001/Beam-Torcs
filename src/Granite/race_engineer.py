#!/usr/bin/env python3

import json
import os
import time
import wave
import torch
import pyaudio
import whisper
import threading
from transformers import AutoTokenizer, AutoModelForCausalLM

os.environ.setdefault("PULSE_SERVER", "unix:/mnt/wslg/PulseServer")


# ── Paths ─────────────────────────────────────────────────────────────────────
DATA_DIR   = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData")
STATS_PATH = os.path.join(DATA_DIR, "end_statistics.json")
SPEED_PATH = os.path.join(DATA_DIR, "speed.json")
WAV_PATH   = "/tmp/torcs_question.wav"

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

# ── Audio recording ───────────────────────────────────────────────────────────
os.environ.setdefault("PULSE_SERVER", "unix:/mnt/wslg/PulseServer")

def get_rdp_source_index():
    pa = pyaudio.PyAudio()
    for i in range(pa.get_device_count()):
        d = pa.get_device_info_by_index(i)
        if d['name'] == 'pulse' and d['maxInputChannels'] > 0:
            pa.terminate()
            return i
    pa.terminate()
    raise RuntimeError("PulseAudio device not found. Ensure PULSE_SERVER is set and WSLg is running.")

# Replace: import keyboard
from pynput import keyboard as kb

# Replace record_question with this:
def record_question(key="r"):
    pa         = pyaudio.PyAudio()
    device_idx = get_rdp_source_index()

    stream = pa.open(
        format=pyaudio.paInt16,
        channels=1,
        rate=44100,
        input=True,
        input_device_index=device_idx,
        frames_per_buffer=1024
    )
    frames   = []
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

    while not released.is_set():
        frames.append(stream.read(1024, exception_on_overflow=False))

    print("[RaceEngineer] Done recording.")
    listener.stop()
    stream.stop_stream()
    stream.close()
    pa.terminate()

    with wave.open(WAV_PATH, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(pa.get_sample_size(pyaudio.paInt16))
        wf.setframerate(44100)
        wf.writeframes(b"".join(frames))

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
- Fuel remaining: {context.get('fuel', 'N/A'):.1f}L
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
        wav = record_question(key="r")
        result = whisper_model.transcribe(wav)
        question = result["text"].strip()
        os.remove(wav)

        if not question:
            print("[RaceEngineer] Nothing heard, try again.")
            continue

        print(f"[RaceEngineer] Driver: {question}")
        response = ask_granite(question)
        print(f"[RaceEngineer] Engineer: {response}")
        speak(response)