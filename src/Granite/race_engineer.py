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
import ctypes
import contextlib

# ── Suppress ALSA/JACK noise ──────────────────────────────────────────────────
@contextlib.contextmanager
def suppress_alsa_errors():
    """Redirect ALSA/JACK stderr noise to /dev/null."""
    devnull = os.open(os.devnull, os.O_WRONLY)
    old_stderr = os.dup(2)
    os.dup2(devnull, 2)
    os.close(devnull)
    try:
        yield
    finally:
        os.dup2(old_stderr, 2)
        os.close(old_stderr)

# ── Environment (must be set before any audio initialisation) ─────────────────
os.environ["PULSE_SERVER"] = "unix:/mnt/wslg/PulseServer"
os.environ["DISPLAY"]      = os.environ.get("DISPLAY", ":0")

# ── Paths ─────────────────────────────────────────────────────────────────────
DATA_DIR   = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData")
# STATS_PATH = os.path.join(DATA_DIR, "engineer_data.json")
# SPEED_PATH = os.path.join(DATA_DIR, "speed.json")
ENGINEER_PATH = os.path.join(DATA_DIR, "engineer_data.json")
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
            if key_event.char == key and not pressed.is_set():
                pressed.set()
        except AttributeError:
            pass

    def on_release(key_event):
        try:
            if key_event.char == key and pressed.is_set():
                released.set()
        except AttributeError:
            pass

    listener = kb.Listener(on_press=on_press, on_release=on_release)
    listener.start()

    print("[RaceEngineer] Waiting for R key...")
    pressed.wait()
    print("[RaceEngineer] Recording...")

    # Open stream AFTER key is pressed, not before
    with suppress_alsa_errors():
        pa         = pyaudio.PyAudio()
        device_idx = get_pulse_device_index(pa)
        stream     = pa.open(
            format=SAMPLE_FORMAT,
            channels=CHANNELS,
            rate=SAMPLE_RATE,
            input=True,
            input_device_index=device_idx,
            frames_per_buffer=CHUNK
        )

    frames = []
    while not released.is_set():
        frames.append(stream.read(CHUNK, exception_on_overflow=False))

    print("[RaceEngineer] Done recording.")
    listener.stop()
    stream.stop_stream()
    stream.close()

    with suppress_alsa_errors():
        pa.terminate()

    with wave.open(WAV_PATH, "wb") as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(b"".join(frames))

    return WAV_PATH

# ── Race data ─────────────────────────────────────────────────────────────────
def load_race_context():
    context = {}

    if os.path.exists(ENGINEER_PATH) and os.path.getsize(ENGINEER_PATH) > 0:
        try:
            with open(ENGINEER_PATH, "r") as f:
                lines = [l.strip().rstrip(",") for l in f if l.strip() and l.strip() != ","]
            if lines:
                last = json.loads(lines[-1])
                context["current speed in km/h"] = last.get("speed_kmh", 0)
                context["avg_speed_km/h"] = last.get("avg_speed_kmh", 0)
                context["distance raced"]  = last.get("dist_raced", "N/A")
                context["current lap"] = last.get("lap", "N/A")
                context["fuel"] = last.get("fuel", "N/A")
                context["current tire temperature (avg across all tyres)"] = last.get("avg_tyre_temp", "N/A")
                context["current tire condition (avg across all tyres)"] = last.get("avg_tyre_condition", "N/A")
                context["current brake temperature (avg across all brakes)"] = last.get("avg_brake_temp", "N/A")
                context["current car damage"] = last.get("damage", "N/A")

        except Exception:
            pass
    return context

def build_prompt(question, context):
    fuel  = context.get('fuel', None)
    fuel_str = f"{fuel:.1f}L" if isinstance(fuel, (int, float)) else "N/A"

    return f"""You are a Formula 1 race engineer giving concise real-time information to your driver. Answer in 1-2 sentences only.

LIVE TELEMETRY:
- Current Speed: {context.get('current speed in km/h', 0):.1f} km/h
- Brake Temperature: {context.get('brake temperature', 'N/A')} (0.0 = cool, 1.0 = hot)
- Tire Condition: {context.get('tire condition', 'N/A')}  (1.0 = new, 0.0 = destroyed)
- Tire Temperature: {context.get('tire temperature', 'N/A')} (0.0 = cool, 1.0 = hot)
- Car Damage: {context.get('damage', 0)} / 10000
- Fuel Remaining: {context.get('fuel', 0)}L

SESSION DATA:
- Current Lap: {context.get('current lap', 'N/A')}
- Distance raced: {context.get('distance raced', 0):.0f}m
- Average Speed: {context.get('avg_speed_km/h', 0):.1f} km/h


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