import json
import sys
import os
from transformers import AutoTokenizer, AutoModelForCausalLM
import torch

# ── Paths ─────────────────────────────────────────────────────────────────────
# Script lives at src/drivers/human/analyse.py 
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

# ── Reference Data (The "Ghost" Car) ──────────────────────────────────────────
reference_data = {
    "target_lap": 65.50,         # A "Pro" lap time
    "target_avg_speed": 195.0,    # Target km/h
    "max_acceptable_damage": 100, 
    "fuel_efficiency_target": 0.02 # Liters per meter
}

# ── Load and Parse JSON ───────────────────────────────────────────────────────
with open(STATS_PATH, "r") as f:
    stats = json.load(f)

# Calculate performance deltas
best_lap = stats.get('best_lap_time', 0)
lap_delta = best_lap - reference_data["target_lap"]
speed_delta = stats.get('avg_speed_kmh', 0) - reference_data["target_avg_speed"]

# Format lap times for the AI
lap_history = ", ".join([f"L{l['lap']}: {l['time']:.2f}s" for l in stats.get("lap_times", [])])

# ── Build The Improved Prompt ─────────────────────────────────────────────────
prompt = f"""Role: Professional TORCS Racing Coach
Task: Compare Driver Performance against Pro Reference Data.

[CURRENT SESSION DATA]
- Finish Position: P{stats.get('finish_pos')}
- Best Lap: {best_lap:.3f}s
- Avg Speed: {stats.get('avg_speed_kmh', 0):.1f} km/h
- Damage: {stats.get('damage', 0)} points
- Lap History: {lap_history}

[PRO REFERENCE DATA]
- Target Lap: {reference_data['target_lap']:.2f}s
- Target Avg Speed: {reference_data['target_avg_speed']:.1f} km/h
- Target Damage: 0

[PERFORMANCE DELTA]
- Lap Time Gap: {lap_delta:+.3f}s vs Pro
- Speed Gap: {speed_delta:+.1f} km/h vs Pro

Instruction: 
Provide a high-pressure technical critique in 4-5 sentences. 
If the Lap Time Gap is positive (+), tell the driver where they are losing time. 
If Damage is 0 but the driver is slow, tell them to push harder. 
End with a specific 'Focus for Next Outing'.

Coach Feedback:"""

print_header("Prompt Generated")
# print(prompt) # Uncomment to debug

# ── Load Granite ──────────────────────────────────────────────────────────────
print_header("Loading Granite model...")

model_name = "ibm-granite/granite-4.0-350m"

# 1. Use a standard dtype (float32 is safe for CPU)
# 2. Remove device_map="cpu" if you don't have 'accelerate' installed yet
# 3. Add low_cpu_mem_usage to prevent the idle hang during tensor allocation

tokenizer = AutoTokenizer.from_pretrained(model_name)
model = AutoModelForCausalLM.from_pretrained(
    model_name,
    torch_dtype=torch.float32,
    low_cpu_mem_usage=True
).to("cpu") # Explicitly move to CPU after loading

model.eval()
print("Model loaded successfully!")

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