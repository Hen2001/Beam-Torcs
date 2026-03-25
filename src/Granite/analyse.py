#!/usr/bin/env python3

import json
import sys
import os
from transformers import AutoTokenizer, AutoModelForCausalLM
import torch

# ── Paths ─────────────────────────────────────────────────────────────────────
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

# ── Load Data ─────────────────────────────────────────────────────────────────
if not os.path.exists(STATS_PATH):
    print(f"No end_statistics.json found at {STATS_PATH}, exiting.")
    sys.exit(1)

with open(STATS_PATH, "r") as f:
    stats = json.load(f)

# Analytics Logic (Hardcoded Calculations) 
lap_times = stats.get("lap_times", [])

if lap_times:
    # Get the full dictionary objects
    best_lap_obj = min(lap_times, key=lambda x: x["time"])
    worst_lap_obj = max(lap_times, key=lambda x: x["time"])
    
    stats["best_lap_time"] = best_lap_obj["time"]
    stats["best_lap_num"] = best_lap_obj["lap"]
    stats["worst_lap_time"] = worst_lap_obj["time"]
    stats["worst_lap_num"] = worst_lap_obj["lap"]
    
    total_time = sum(l["time"] for l in lap_times)
    stats["avg_lap_time"] = total_time / len(lap_times)
    
    # Trend analysis
    first_lap = lap_times[0].get("time", 0)
    last_lap = lap_times[-1].get("time", 0)
    trend = "improving" if last_lap < first_lap else "declining"
else:
    stats["best_lap_time"] = 0
    stats["best_lap_num"] = 0
    stats["worst_lap_time"] = 0
    stats["worst_lap_num"] = 0
    trend = "stable"

speed_val = stats.get('avg_speed_kmh', 0)
speed_target = 176
speed_status = "PASSED" if speed_val >= speed_target else "FAILED"

lap_target = 70
lap_status = "PASSED" if (stats["best_lap_time"] > 0 and stats["best_lap_time"] <= lap_target) else "FAILED"
worst_status = "PASSED" if (stats["worst_lap_time"] > 0 and stats["worst_lap_time"] <= lap_target) else "FAILED"

# ── Build the Structured Prompt ───────────────────────────────────────────────
analysis_header = f"""ANALYTICS REPORT:
- Average Speed: {speed_val:.1f} km/h (Target: {speed_target} km/h) -> {speed_status}
- Best Lap: Lap {stats['best_lap_num']} at {stats['best_lap_time']:.2f}s (Target: {lap_target}s) -> {lap_status}
- Worst Lap: Lap {stats['worst_lap_num']} at {stats['worst_lap_time']:.2f}s (Target: {lap_target}s) -> {worst_status}
- Performance Trend: Driver pace is {trend} throughout the session."""

prompt = f"""### SYSTEM:
You are a Race Data Analyst. Your job is to provide a professional, data-driven summary of the session. Do not mention "the team" or "missing laps". Speak directly about the driver's metrics.

### SESSION DATA:
{analysis_header}

### SUMMARY COMMENTARY:
Based on the data, you can see that"""

# ── Load Granite ──────────────────────────────────────────────────────────────
print_header("Loading Granite Model")
model_name = "ibm-granite/granite-4.0-350m"

tokenizer = AutoTokenizer.from_pretrained(model_name)
model = AutoModelForCausalLM.from_pretrained(
    model_name,
    torch_dtype=torch.float32,
    device_map="cpu"
)
model.eval()

# ── Run Inference ─────────────────────────────────────────────────────────────
print_header("Generating Analysis")
inputs = tokenizer(prompt, return_tensors="pt")

with torch.no_grad():
    output = model.generate(
        **inputs,
        max_new_tokens=100,
        do_sample=False,
        temperature=0.5, 
        repetition_penalty=1.1,
        pad_token_id=tokenizer.eos_token_id
    )

response_tokens = output[0][inputs["input_ids"].shape[1]:]
commentary = tokenizer.decode(response_tokens, skip_special_tokens=True).strip()

full_report = f"{analysis_header}\n\nGRANITE SUMMARY:\nBased on the data, you can see that {commentary}"

# ── Save and Print Result ─────────────────────────────────────────────────────
OUTPUT_PATH = os.path.join(DATA_DIR, "granite_analysis.txt")

with open(OUTPUT_PATH, "w") as f:
    f.write("── Granite AI Analytics Report ──\n\n")
    f.write(full_report + "\n")
    f.write("═" * 50 + "\n")

print_header("Granite AI Analytics Report")
print(f"{full_report}\n")
print(f"{BOLD}{CYAN}{'═' * 50}{RESET}\n")
print(f"Analysis saved to {OUTPUT_PATH}")