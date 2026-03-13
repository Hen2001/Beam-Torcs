#!/usr/bin/env python3

import json
import os
import time
import wave
import subprocess
import threading
import torch
import pyaudio
import whisper
from pynput import keyboard as kb
from transformers import AutoTokenizer, AutoModelForCausalLM

# ── Environment (must be set before any audio initialisation) ─────────────────
os.environ["PULSE_SERVER"] = "unix:/mnt/wslg/PulseServer"
os.environ["DISPLAY"]      = os.environ.get("DISPLAY", ":0")

# ── Paths ─────────────────────────────────────────────────────────────────────
DATA_DIR   = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData")
STATS_PATH = os.path.join(DATA_DIR, "end_statistics.json")
SPEED_PATH = os.path.join(DATA_DIR, "speed.json")
WAV_PATH   = "/tmp/torcs_question.wav"

SAMPLE_RATE   = 16000
CHANNELS      = 1
CHUNK         = 1024
SAMPLE_FORMAT = pyaudio.paInt16

# ── PulseAudio keepalive ──────────────────────────────────────────────────────
def ensure_pulse():
    """Check PulseAudio is alive, attempt restart if not."""
    result = subprocess.run(
        ["pactl", "--server=unix:/mnt/wslg/PulseServer", "info"],
        capture_output=True
    )
    if result.returncode == 0:
        print("[RaceEngineer] Audio OK.")
        return

    print("[RaceEngineer] PulseAudio not responding, attempting restart...")
    subprocess.run(
        ["pulseaudio", "--start", "--daemonize=true", "--exit-idle-time=-1"],
        capture_output=True
    )
    time.sleep(2)

    result = subprocess.run(
        ["pactl", "--server=unix:/mnt/wslg/PulseServer", "info"],
        capture_output=True
    )
    if result.returncode != 0:
        raise RuntimeError(
            "[RaceEngineer] Audio unavailable.\n"
            "Run 'wsl --shutdown' in Windows PowerShell, reopen WSL, then start TORCS again."
        )
    print("[RaceEngineer] Audio restarted successfully.")

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

# ── Audio device ──────────────────────────────────────────────────────────────
def get_pulse_device_index(pa):
    """Find the PulseAudio input device index."""
    for i in range(pa.get_device_count()):
        d = pa.get_device_info_by_index(i)
        if d['name'] == 'pulse' and d['maxInputChannels'] > 0:
            return i
    raise RuntimeError(
        "PulseAudio input device not found.\n"
        "Ensure PULSE_SERVER is set and WSLg is running.\n"
        "If this keeps failing, run 'wsl --shutdown' from Windows PowerShell and restart."
    )

# ── Recording ─────────────────────────────────────────────────────────────────
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

    pa         = pyaudio.PyAudio()
    device_idx = get_pulse_device_index(pa)

    stream = pa.open(
        format=SAMPLE_FORMAT,
        channels=CHANNELS,
        rate=SAMPLE_RATE,
        input=True,
        input_device_index=device_idx,
        frames_per_buffer=CHUNK
    )

    print("[RaceEngineer] Waiting for R key...")
    pressed.wait()
    print("[RaceEngineer] Recording...")

    frames = []
    while not released.is_set():
        frames.append(stream.read(CHUNK, exception_on_overflow=False))

    print("[RaceEngineer] Done recording.")
    listener.stop()
    stream.stop_stream()
    stream.close()
    pa.terminate()

    with wave.open(WAV_PATH, "wb") as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(2)  # paInt16 = 2 bytes
        wf.setframerate(SAMPLE_RATE)
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
    fuel  = context.get('fuel', None)
    fuel_str = f"{fuel:.1f}L" if isinstance(fuel, (int, float)) else "N/A"

    return f"""You are a Formula 1 race engineer giving concise real-time information to your driver. Answer in 1-2 sentences only.

LIVE TELEMETRY:
- Speed: {context.get('speed_kmh', 0):.1f} km/h | Gear: {context.get('gear', 'N/A')} | RPM: {context.get('rpm', 0):.0f}
- Fuel remaining: {fuel_str}
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
    ensure_pulse()
    print("[RaceEngineer] Ready. Hold R to ask a question.")
    while True:
        wav      = record_question(key="r")
        
        # DEBUG: keep a copy to inspect
        import shutil
        shutil.copy(wav, "/tmp/debug_last.wav")
        
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