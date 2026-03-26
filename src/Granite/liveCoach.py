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
        "You're running wide in {seg}. Tighten your line.",
        "Stay within track limits in {seg}.",
        "You're losing exit speed in {seg}.",
        "Control the car on exit in {seg}."
    ],
    "LOSING_TIME": [
        "Brake later into {seg}.",
        "Carry more speed through {seg}.",
        "Earlier throttle out of {seg}.",
        "Focus on the apex in {seg}.",
        "Trust the grip through {seg}."
    ],
    "GAINING_TIME": [
        "Good line through {seg}. Repeat it.",
        "Strong exit from {seg}.",
        "You're gaining time there. Stay consistent.",
        "Keep that rhythm through {seg}."
    ],
    "DAMAGE": [
        "Car has damage. Brake earlier into {seg}.",
        "Adjust entry speed for {seg}.",
        "Keep inputs smooth through {seg}."
    ],
    "BATTLE": [
        "Cover the inside into {seg}.",
        "Stay defensive into {seg}.",
        "Hold your line through {seg}.",
        "Don't open the door in {seg}."
    ],
    "BUILDING_BASELINE": [
        "Focus on consistency in {seg}.",
        "Keep inputs smooth through {seg}.",
        "Build a clean reference lap."
    ],
    "STANDARD": [
        "Keep it smooth through {seg}.",
        "Maintain this pace.",
        "Focus on your braking point.",
        "Good control. Keep building.",
        "You're doing well. Stay focused."
    ]
}

# ── Granite Templates (ALL EVENTS) ────────────────────────────────────────────
GRANITE_TEMPLATES = {
    "WIDE": [
        "You're running wide in {seg}. Fix it by",
        "Track limits in {seg}. Correct it by",
        "You're drifting out in {seg}. Focus on"
    ],
    "LOSING_TIME": [
        "You're losing time in {seg}. Improve by",
        "Delta is up in {seg}. Focus on",
        "You're off pace in {seg}. Work on"
    ],
    "GAINING_TIME": [
        "You're gaining time in {seg}. Keep it by",
        "Strong pace in {seg}. Maintain by",
        "Good sector in {seg}. Continue by"
    ],
    "DAMAGE": [
        "Car damage affects {seg}. Adjust by",
        "With damage in {seg}, compensate by",
        "Balance is off in {seg}. Manage it by"
    ],
    "BATTLE": [
        "Opponent pressure into {seg}. Defend by",
        "Into {seg}, manage the opponent by",
        "Car behind is close in {seg}. Control it by"
    ],
    "BUILDING_BASELINE": [
        "Building baseline in {seg}. Focus on",
        "No reference yet in {seg}. Work on",
        "Establish consistency in {seg} by"
    ],
    "STANDARD": [
        "Through {seg}, improve by",
        "Maintain performance in {seg} by",
        "Refine your technique in {seg} by"
    ]
}

# ── Granite Completion ────────────────────────────────────────────────────────
SYSTEM_COACH = (
    "You are a professional race Coach. Complete the sentence in 3 to 5 words. "
    "Be precise, technical, and give actionable driving advice."
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
    except:
        return ""

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

        if dmg > self._prev["damage"] + CFG["damage_jump"]:
            evs.append(Event("DAMAGE", 10))
        
        if tpos > CFG["wide_threshold"]:
            evs.append(Event("WIDE", 9))

        if data.get("opponents"):
            p_seg = int(data.get("segment", 0))
            for o in data["opponents"]:
                if abs(p_seg - int(o.get("segment", 0))) <= 1:
                    evs.append(Event("BATTLE", 8))
                    break

        if not has_prev or lap < 2:
            evs.append(Event("BUILDING_BASELINE", 2))
        else:
            if delta > 0.5:
                evs.append(Event("LOSING_TIME", 7))
            elif delta < -0.3:
                evs.append(Event("GAINING_TIME", 6))

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
def build_coaching(event: Event, data: dict) -> str:
    seg_idx = int(data.get("segment", 0))
    seg_name = SEGMENT_NAMES[seg_idx] if seg_idx < len(SEGMENT_NAMES) else f"Sector {seg_idx}"
    delta = float(data.get("delta", 0.0))
    spd = int(data.get("speed", 0))
    
    fmt = {"seg": seg_name, "delta": delta, "abs_delta": abs(delta), "spd": spd}
    
    if event.name == "BATTLE" and data.get("opponents"):
        fmt["opp_pos"] = data["opponents"][0].get("place", "??")

    # ── Granite now 50% chance ───────────────────────────────────────────────
    if event.name in GRANITE_TEMPLATES and random.random() < 0.50:
        prefix = random.choice(GRANITE_TEMPLATES[event.name]).format(**fmt)
        completion = granite_complete(prefix)
        if completion:
            return f"{prefix} {completion}"

    # ── Fallback ─────────────────────────────────────────────────────────────
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