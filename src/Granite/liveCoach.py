import json, time, os, torch, random, signal, traceback
from transformers import AutoTokenizer, AutoModelForCausalLM
import sys

# ── Setup Logging ─────────────────────────────────────────────────────────────
log_path = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData", "granite_error.log")
log_file = open(log_path, 'w', buffering=1)
sys.stderr = log_file

# ── Process Control ───────────────────────────────────────────────────────────
def _force_exit(sig, frame):
    print("\n[INFO] Coach Interrupted — shutting down.")
    os._exit(0)

signal.signal(signal.SIGINT,  _force_exit)
signal.signal(signal.SIGTERM, _force_exit)

DATA_PATH  = os.path.expanduser("~/.torcs/DrivingData/live_coaching_data.json")
COACH_PATH = os.path.expanduser("~/.torcs/DrivingData/live_coaching.txt")
MODEL_NAME = "ibm-granite/granite-4.0-350m"

SEGMENT_NAMES = [
    "First Straight", "Hairpin", "Corner 2", "Corner 3",
    "Long Left", "Back Straight", "The Corkscrew", "Kink", "Final Straight"
]

# ── Load Model ────────────────────────────────────────────────────────────────
print("Loading Granite for Live Coaching...")
tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)
model = AutoModelForCausalLM.from_pretrained(
    MODEL_NAME,
    torch_dtype=torch.float32,
    low_cpu_mem_usage=True
).to("cpu")
model.eval()
print("Coach Model loaded.")

HAS_CHAT_TEMPLATE = (hasattr(tokenizer, "apply_chat_template") and tokenizer.chat_template is not None)

# ── Hardcoded Coaching Lines ──────────────────────────────────────────────────
LINES = {
    "WIDE": [
        "You're too wide in {seg} — stay off the dirty air.",
        "Track limits! Keep it between the white lines in {seg}.",
        "You're losing time running wide in {seg}. Tighten the entry.",
        "Watch the exit in {seg}, you're pushing too far out."
    ],
    "LOSING_TIME": [
        "Brake later into {seg} — you're leaving time on the table.",
        "Slow through {seg}. Carry more mid-corner speed.",
        "Get on the power earlier out of {seg}.",
        "Focus on the apex in {seg}, you're missing the clip.",
        "Delta is climbing; trust the aero through {seg}."
    ],
    "GAINING_TIME": [
        "Excellent pace through {seg}, keep that rhythm!",
        "Delta is green! That's the line to take in {seg}.",
        "Purple sector! You found the limit in {seg}.",
        "Perfect exit from {seg}. Keep pushing."
    ],
    "DAMAGE": [
        "Car is damaged! Be careful on the brakes into {seg}.",
        "Aero balance is off due to damage. Adjust your entry for {seg}.",
        "Contact detected. Check the steering alignment through {seg}."
    ],
    "BATTLE": [
        "Defensive line into {seg}! Don't give them the inside.",
        "Opponent P{opp_pos} is closing. Close the door in {seg}.",
        "Pressure is on. Stay focused on your marks in {seg}.",
        "He's looking for a move in {seg}. Hold the middle."
    ],
    "BUILDING_BASELINE": [
        "Focus on consistency. Nail the apex in {seg}.",
        "Building data. Keep it smooth through {seg}.",
        "No reference lap yet. Just find your flow in {seg}."
    ],
    "STANDARD": [
        "Smooth is fast. Nice work through {seg}.",
        "Steady through {seg}. Speed is {spd} km/h.",
        "Holding steady. Next target is the {seg}.",
        "Good gear management. Keep it up."
    ]
}

# ── Granite Templates ─────────────────────────────────────────────────────────
GRANITE_TEMPLATES = {
    "LOSING_TIME": [
        "You're {delta:.2f}s down in {seg}. To recover, you need to —",
        "Lost time in {seg}. Coach advice: —",
    ],
    "DAMAGE": [
        "Car damage is affecting {seg}. Technical tip: —",
        "With that wing damage, through {seg} you should —"
    ]
}

# ── Granite Completion & Freeform ─────────────────────────────────────────────
SYSTEM_COACH = (
    "You are a professional F1 driving coach. Complete the instruction with "
    "exactly 5 words or fewer. Be technical, sharp, and actionable."
)

def granite_complete(prefix: str) -> str:
    try:
        if HAS_CHAT_TEMPLATE:
            msg = [{"role": "system", "content": SYSTEM_COACH}, {"role": "user", "content": prefix}]
            p_str = tokenizer.apply_chat_template(msg, tokenize=False, add_generation_prompt=True)
        else:
            p_str = f"{SYSTEM_COACH}\n{prefix}"

        inputs = tokenizer(p_str, return_tensors="pt").to("cpu")
        with torch.no_grad():
            out = model.generate(**inputs, max_new_tokens=8, do_sample=False, repetition_penalty=1.2)
        
        txt = tokenizer.decode(out[0], skip_special_tokens=True)
        res = txt.split("assistant")[-1].strip() if HAS_CHAT_TEMPLATE else txt.split(prefix)[-1].strip()
        return res.split(".")[0].split("\n")[0].strip().replace('"', '')
    except: return ""

# ── Event Controller ──────────────────────────────────────────────────────────
CFG = {
    "wide_threshold": 7.5,
    "damage_jump": 50,
    "cooldown": {
        "WIDE": 8, "DAMAGE": 15, "LOSING_TIME": 12, 
        "GAINING_TIME": 15, "BATTLE": 10, "STANDARD": 20
    }
}

class CoachController:
    def __init__(self):
        self._fired = {}
        self._prev = {"damage": 0, "lap": 0}

    def get_event(self, data: dict):
        now = time.time()
        spd = int(data.get("speed", 0))
        dmg = int(data.get("damage", 0))
        tpos = abs(float(data.get("trackPos", 0)))
        delta = float(data.get("delta", 0.0))
        lap = int(data.get("lap", 0))
        has_prev = data.get("hasPrevLap", False)
        
        evs = []

        # 1. Damage (High Priority)
        if dmg > self._prev["damage"] + CFG["damage_jump"]:
            evs.append(Event("DAMAGE", 10))
        
        # 2. Track Limits
        if tpos > CFG["wide_threshold"]:
            evs.append(Event("WIDE", 9))

        # 3. Battles
        if data.get("opponents"):
            p_seg = int(data.get("segment", 0))
            for o in data["opponents"]:
                if abs(p_seg - int(o.get("segment", 0))) <= 1:
                    evs.append(Event("BATTLE", 8))
                    break

        # 4. Performance
        if not has_prev or lap < 2:
            evs.append(Event("BUILDING_BASELINE", 2))
        else:
            if delta > 0.5: evs.append(Event("LOSING_TIME", 7))
            elif delta < -0.3: evs.append(Event("GAINING_TIME", 6))

        evs.append(Event("STANDARD", 0))
        evs.sort(key=lambda e: e.priority, reverse=True)

        for e in evs:
            if now - self._fired.get(e.name, 0) >= CFG["cooldown"].get(e.name, 5):
                self._fired[e.name] = now
                self._prev.update({"damage": dmg, "lap": lap})
                return e
        return None

class Event:
    def __init__(self, name, priority):
        self.name = name
        self.priority = priority

controller = CoachController()

# ── Coaching Builder ──────────────────────────────────────────────────────────
_last_used = {}

def build_coaching(event: Event, data: dict) -> str:
    seg_idx = int(data.get("segment", 0))
    seg_name = SEGMENT_NAMES[seg_idx] if seg_idx < len(SEGMENT_NAMES) else f"Sector {seg_idx}"
    delta = float(data.get("delta", 0.0))
    spd = int(data.get("speed", 0))
    
    fmt = {"seg": seg_name, "delta": delta, "abs_delta": abs(delta), "spd": spd}
    
    # Opponent info for battles
    if event.name == "BATTLE" and data.get("opponents"):
        fmt["opp_pos"] = data["opponents"][0].get("place", "??")

    # ── Granite logic: 40% chance on specific events ─────────────────────────
    if event.name in GRANITE_TEMPLATES and random.random() < 0.40:
        prefix = random.choice(GRANITE_TEMPLATES[event.name]).format(**fmt)
        completion = granite_complete(prefix)
        if completion:
            return f"{prefix} {completion}"

    # ── Fallback to Hardcoded ────────────────────────────────────────────────
    pool = LINES.get(event.name, LINES["STANDARD"])
    return random.choice(pool).format(**fmt)

# ── Main Loop ─────────────────────────────────────────────────────────────────
print(f"[INFO] Coach Monitoring {DATA_PATH}...")
last_mtime = 0

while True:
    try:
        if os.path.exists(DATA_PATH):
            mtime = os.path.getmtime(DATA_PATH)
            if mtime != last_mtime:
                with open(DATA_PATH, 'r') as f:
                    data = json.load(f)

                event = controller.get_event(data)
                if event:
                    advice = build_coaching(event, data)
                    print(f"[{event.name}] {advice}")
                    with open(COACH_PATH, "w") as cf:
                        cf.write(advice)
                last_mtime = mtime
    except Exception:
        traceback.print_exc()
    time.sleep(0.25)