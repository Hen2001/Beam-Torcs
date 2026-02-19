#!/usr/bin/env python3

import json
import os
import io
import time
import torch
import whisper
import threading
import numpy as np
from flask import Flask, request, jsonify, render_template_string
from transformers import AutoTokenizer, AutoModelForCausalLM
from flask_socketio import SocketIO, emit

# ── Paths ─────────────────────────────────────────────────────────────────────
DATA_DIR   = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData")
STATS_PATH = os.path.join(DATA_DIR, "end_statistics.json")
SPEED_PATH = os.path.join(DATA_DIR, "speed.json")

# ── Load models ───────────────────────────────────────────────────────────────
print("Loading Whisper...")
whisper_model = whisper.load_model("base")

print("Loading Granite...")
model_name = "ibm-granite/granite-4.0-350m"
tokenizer  = AutoTokenizer.from_pretrained(model_name)
granite    = AutoModelForCausalLM.from_pretrained(
    model_name,
    torch_dtype=torch.float32,
    device_map="cpu"
)
granite.eval()

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
    lap_times_str = ", ".join(
        f"Lap {l['lap']}: {l['time'] * 60.0:.2f}s"
        for l in stats.get("lap_times", [])
    ) or "N/A"

    return f"""You are a Formula racing engineer giving real-time advice to a driver. Answer the driver's question concisely in 1-2 sentences using the race data provided.

Current Race Data:
- Laps completed: {stats.get('laps_completed', 'N/A')}
- Lap times: {lap_times_str}
- Best lap: {stats.get('best_lap_time', 0) * 60.0:.2f}s (target: 70s)
- Average speed: {stats.get('avg_speed_kmh', 0):.1f} km/h (target: 176 km/h)
- Current speed: {context.get('current_speed_ms', 0) * 3.6:.1f} km/h
- Current segment: {context.get('current_segment', 'N/A')}
- Damage: {stats.get('damage', 0)}
- Fuel used: {stats.get('fuel_used', 0):.2f}L

Driver question: {question}

Engineer response:"""

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

# ── Flask app ─────────────────────────────────────────────────────────────────
app = Flask(__name__)

HTML = """
<!DOCTYPE html>
<html>
<head>
    <title>TORCS Race Engineer</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            background: #0a0a0a;
            color: #e0e0e0;
            font-family: 'Courier New', monospace;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            min-height: 100vh;
            padding: 20px;
        }
        h1 {
            color: #00d4ff;
            font-size: 1.5em;
            margin-bottom: 30px;
            letter-spacing: 3px;
            text-transform: uppercase;
        }
        #status {
            color: #888;
            font-size: 0.85em;
            margin-bottom: 20px;
            letter-spacing: 1px;
        }
        #mic-btn {
            width: 120px;
            height: 120px;
            border-radius: 50%;
            border: 3px solid #00d4ff;
            background: transparent;
            color: #00d4ff;
            font-size: 2.5em;
            cursor: pointer;
            transition: all 0.2s;
            margin-bottom: 30px;
        }
        #mic-btn.recording {
            background: #00d4ff22;
            border-color: #ff4444;
            color: #ff4444;
            animation: pulse 1s infinite;
        }
        @keyframes pulse {
            0%   { box-shadow: 0 0 0 0 #ff444444; }
            100% { box-shadow: 0 0 0 20px #ff444400; }
        }
        #transcript {
            color: #aaa;
            font-size: 0.9em;
            margin-bottom: 15px;
            min-height: 1.2em;
        }
        #response-box {
            background: #111;
            border: 1px solid #00d4ff33;
            border-left: 3px solid #00d4ff;
            padding: 20px;
            max-width: 600px;
            width: 100%;
            min-height: 80px;
            font-size: 1em;
            line-height: 1.6;
            color: #00d4ff;
        }
        #response-label {
            color: #555;
            font-size: 0.75em;
            letter-spacing: 2px;
            margin-bottom: 8px;
        }
    </style>
</head>
<body>
    <h1>⚑ TORCS Race Engineer</h1>
    <div id="status">READY</div>
    <button id="mic-btn" onclick="toggleRecording()">🎙</button>
    <div id="transcript"></div>
    <div id="response-label">ENGINEER</div>
    <div id="response-box">Awaiting driver input...</div>

    <script>
        let mediaRecorder = null;
        let audioChunks   = [];
        let recording     = false;

        async function toggleRecording() {
            if (!recording) {
                const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
                mediaRecorder = new MediaRecorder(stream);
                audioChunks   = [];

                mediaRecorder.ondataavailable = e => audioChunks.push(e.data);
                mediaRecorder.onstop = sendAudio;
                mediaRecorder.start();

                recording = true;
                document.getElementById("mic-btn").classList.add("recording");
                document.getElementById("status").textContent = "RECORDING...";
            } else {
                mediaRecorder.stop();
                mediaRecorder.stream.getTracks().forEach(t => t.stop());
                recording = false;
                document.getElementById("mic-btn").classList.remove("recording");
                document.getElementById("status").textContent = "PROCESSING...";
            }
        }

        async function sendAudio() {
            const blob = new Blob(audioChunks, { type: "audio/webm" });
            const form = new FormData();
            form.append("audio", blob, "question.webm");

            const res  = await fetch("/ask", { method: "POST", body: form });
            const data = await res.json();

            document.getElementById("transcript").textContent  = "You: " + data.question;
            document.getElementById("response-box").textContent = data.response;
            document.getElementById("status").textContent       = "READY";

            // Browser TTS
            const utterance = new SpeechSynthesisUtterance(data.response);
            utterance.rate  = 1.1;
            window.speechSynthesis.speak(utterance);
        }
    </script>
</body>
</html>
"""

@app.route("/")
def index():
    return render_template_string(HTML)

@app.route("/ask", methods=["POST"])
def ask():
    audio_file = request.files["audio"]
    tmp_path   = "/tmp/torcs_question.webm"
    audio_file.save(tmp_path)

    result   = whisper_model.transcribe(tmp_path)
    question = result["text"].strip()
    os.remove(tmp_path)

    response = ask_granite(question)
    return jsonify({"question": question, "response": response})

if __name__ == "__main__":
    print("Race Engineer running at http://localhost:5000")
    app.run(host="0.0.0.0", port=5000)