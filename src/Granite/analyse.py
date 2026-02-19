#!/usr/bin/env python3

import json
import sys
import os
from transformers import AutoTokenizer, AutoModelForCausalLM
import torch

# ── Paths ─────────────────────────────────────────────────────────────────────
# Script lives at src/drivers/human/analyse.py (or similar)
# Runtime data always goes to ~/.torcs/DrivingData
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR   = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData")
STATS_PATH = os.path.join(DATA_DIR, "end_statistics.json")

# ── Formatting ────────────────────────────────────────────────────────────────
BOLD  = "\033[1m"
CYAN  = "\033[96m"
GREEN = "\033[92m"
RESET = "\033[0m"

def print_header(text):
    print(f"\n{BOLD}{CYAN}── {text} ──{RESET}")

# ── Load data ─────────────────────────────────────────────────────────────────
if not os.path.exists(STATS_PATH):
    print(f"No end_statistics.json found at {STATS_PATH}, exiting.")
    sys.exit(1)

with open(STATS_PATH, "r") as f:
    stats = json.load(f)

# ── Build prompt ──────────────────────────────────────────────────────────────
lap_times_str = ", ".join(
    f"Lap {l['lap']}: {l['time'] * 60.0:.2f}s"
    for l in stats.get("lap_times", [])
)

prompt = f"""You are a motorsport performance coach. Analyse this TORCS race session and give direct, specific coaching feedback.

Race Statistics:
- Laps completed: {stats.get('laps_completed', 'N/A')}
- Lap times: {lap_times_str if lap_times_str else 'N/A'}
- Best lap: {stats.get('best_lap_time', 0) * 60.0:.2f}s (target: 70s)
- Average speed: {stats.get('avg_speed_kmh', 0):.1f} km/h (target: 176 km/h)
- Total distance: {stats.get('total_distance', 0):.0f}m
- Damage taken: {stats.get('damage', 0)}
- Fuel used: {stats.get('fuel_used', 0):.2f}L
- Finish position: {stats.get('finish_pos', 'N/A')}

Give 4-5 sentences of direct coaching feedback covering: consistency, speed, damage/incidents, and one specific thing to focus on next session.
"""

# ── Load Granite ──────────────────────────────────────────────────────────────
print_header("Loading Granite model")

model_name = "ibm-granite/granite-4.0-350m"

tokenizer = AutoTokenizer.from_pretrained(model_name)
model = AutoModelForCausalLM.from_pretrained(
    model_name,
    torch_dtype=torch.float32,
    device_map="cpu"
)
model.eval()

# ── Run inference ─────────────────────────────────────────────────────────────
inputs = tokenizer(prompt, return_tensors="pt")

with torch.no_grad():
    output = model.generate(
        **inputs,
        max_new_tokens=200,
        do_sample=False,
        temperature=1.0,
        repetition_penalty=1.1,
        pad_token_id=tokenizer.eos_token_id
    )

response_tokens = output[0][inputs["input_ids"].shape[1]:]
response = tokenizer.decode(response_tokens, skip_special_tokens=True).strip()
# with open("response.txt", "w") as f:
#     f.write(str(response))

# ── Print result ──────────────────────────────────────────────────────────────
OUTPUT_PATH = os.path.join(DATA_DIR, "granite_analysis.txt")

with open(OUTPUT_PATH, "w") as f:
    f.write("── Granite AI Coach Analysis ──\n\n")
    f.write(response + "\n")
    f.write("═" * 50 + "\n")

# print(response)

# Also print in case someone runs it manually in a terminal
print_header("Granite AI Coach Analysis")
print(f"{response}\n")
print(f"{BOLD}{CYAN}{'═' * 50}{RESET}\n")
print(f"Analysis saved to {OUTPUT_PATH}")
