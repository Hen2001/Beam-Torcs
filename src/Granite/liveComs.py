import json, time, os, torch, random, signal, traceback
from transformers import AutoTokenizer, AutoModelForCausalLM
import sys

# ── Setup Logging ─────────────────────────────────────────────────────────────
log_path = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData", "granite_error.log")
log_file = open(log_path, 'w', buffering=1)
sys.stderr = log_file

# ── Process Control ───────────────────────────────────────────────────────────
def _force_exit(sig, frame):
    print("\n[INFO] Interrupted — shutting down.")
    os._exit(0)

signal.signal(signal.SIGINT,  _force_exit)
signal.signal(signal.SIGTERM, _force_exit)

DATA_PATH  = os.path.expanduser("~/.torcs/DrivingData/live_data.json")
COMM_PATH  = os.path.expanduser("~/.torcs/DrivingData/live_commentary.txt")
MODEL_NAME = "ibm-granite/granite-3.1-1b-a400m-instruct"

# ── Load Model ────────────────────────────────────────────────────────────────
print("Loading Granite (Race Engineer Mode)...")
tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)
model = AutoModelForCausalLM.from_pretrained(
    MODEL_NAME,
    torch_dtype=torch.float32,
    low_cpu_mem_usage=True
).to("cpu")
model.eval()
print("Model loaded successfully!")

HAS_CHAT_TEMPLATE = (hasattr(tokenizer, "apply_chat_template") and tokenizer.chat_template is not None)

# ── Dynamic Sector Mapping ────────────────────────────────────────────────────
def get_sector(seg_id: int, track_name: str) -> int:
    """Maps segment IDs to sectors for all your tracks."""
    t = track_name.lower()
    if "corkscrew" in t:
        if seg_id < 40: return 1
        if seg_id < 100: return 2
        if seg_id < 175: return 3
        if seg_id < 235: return 4
        if seg_id < 310: return 5
        if seg_id < 390: return 6
        if seg_id < 500: return 7
        if seg_id < 540: return 8
        return 9
    elif "cg-speedway" in t:
        if seg_id < 81: return 1
        return 2 if seg_id < 191 else 3
    elif "spring" in t:
        return (seg_id // 150) + 1
    elif "forza" in t:
        return (seg_id // 140) + 1
    # Default fallback for other tracks
    return (seg_id // 100) + 1

# ── Event Controller ──────────────────────────────────────────────────────────
CFG = {
    "off_track_threshold": 7.0,
    "crash_damage_jump": 400,
    "spin_delta_threshold": 75,
    "cooldown": {
        "OFF_TRACK": 8, "CRASH": 12, "SPIN": 15, "OVERTAKE": 10,
        "LAP": 10, "STANDARD": 6
    }
}

class Event:
    def __init__(self, name, priority, temp, prompt):
        self.name = name
        self.priority = priority
        self.temp = temp
        self.prompt = prompt

class EventController:
    def __init__(self):
        self._fired = {}
        self._prev = {"speed": 0, "damage": 0, "place": 99, "lap": 0}

    def get_event(self, data: dict):
        now = time.time()
        track = data.get("trackName", "unknown")
        seg   = int(data.get("Segment", 0))
        sec   = get_sector(seg, track)
        spd   = int(data.get("speed", 0))
        pos   = int(data.get("place", 99))
        dmg   = int(data.get("damage", 0))
        tpos  = abs(float(data.get("trackPos", 0)))
        lap   = int(data.get("currentLap", 0))

        evs = []
        if tpos > CFG["off_track_threshold"]:
            evs.append(Event("OFF_TRACK", 10, 0.5, f"Car off track in sector {sec} at {spd} km/h."))
        
        dmg_jump = dmg - self._prev["damage"]
        if dmg_jump >= CFG["crash_damage_jump"]:
            evs.append(Event("CRASH", 9, 0.6, f"Heavy impact detected. {dmg_jump} damage points sustained."))

        if self._prev["speed"] - spd >= CFG["spin_delta_threshold"] and spd < 100:
            evs.append(Event("SPIN", 8, 0.5, f"Loss of control in sector {sec}. Speed dropped to {spd} km/h."))

        if pos < self._prev["place"] and self._prev["place"] != 99:
            evs.append(Event("OVERTAKE", 7, 0.6, f"Position gained. Now in P{pos}."))

        if lap > self._prev["lap"] and self._prev["lap"] > 0:
            evs.append(Event("LAP", 5, 0.5, f"Lap {lap} complete. Position P{pos}."))

        evs.append(Event("STANDARD", 0, 0.6, f"Technical update: Sector {sec}, {spd} km/h, Position P{pos}."))

        evs.sort(key=lambda e: e.priority, reverse=True)
        for e in evs:
            if now - self._fired.get(e.name, 0) >= CFG["cooldown"].get(e.name, 5):
                self._fired[e.name] = now
                self._prev.update({"speed": spd, "damage": dmg, "place": pos, "lap": lap})
                return e
        return None

controller = EventController()

# ── AI Generation (Race Engineer Logic) ───────────────────────────────────────
SYSTEM = (
    "You are a professional race engineer. Provide a technical data update. "
    "Output ONLY one sentence (max 15 words). "
    "Use ONLY the speed, sector, and position provided. "
    "NEVER mention teams, real driver names, hashtags, or MotoGP/F1. "
    "Do NOT use quotes or conversational filler like 'Here is your update'."
)

def generate_ai(user_prompt, temperature=0.5):
    if HAS_CHAT_TEMPLATE:
        msg = [{"role": "system", "content": SYSTEM}, {"role": "user", "content": user_prompt}]
        p_str = tokenizer.apply_chat_template(msg, tokenize=False, add_generation_prompt=True)
    else:
        p_str = f"System: {SYSTEM}\nUser: {user_prompt}\nEngineer:"

    inputs = tokenizer(p_str, return_tensors="pt").to("cpu")
    with torch.no_grad():
        out = model.generate(**inputs, max_new_tokens=25, do_sample=True, 
                             temperature=temperature, repetition_penalty=1.3)
    
    txt = tokenizer.decode(out[0], skip_special_tokens=True)
    res = txt.split("assistant")[-1].strip() if HAS_CHAT_TEMPLATE else txt.split("Engineer:")[-1].strip()
    return res.replace('"', '').replace("'", "").strip()

# ── Main Loop ─────────────────────────────────────────────────────────────────
print(f"[INFO] Monitoring {DATA_PATH}...")
last_mtime = 0

while True:
    try:
        if os.path.exists(DATA_PATH):
            mtime = os.path.getmtime(DATA_PATH)
            if mtime != last_mtime:
                time.sleep(0.05) # Sync buffer
                with open(DATA_PATH, 'r') as f:
                    data = json.load(f)
                
                event = controller.get_event(data)
                if event:
                    commentary = generate_ai(event.prompt, event.temp)
                    print(f"[{event.name}] {commentary}")
                    with open(COMM_PATH, "w") as cf:
                        cf.write(commentary)
                last_mtime = mtime
    except (json.JSONDecodeError, ValueError):
        pass 
    except Exception:
        traceback.print_exc()
    time.sleep(0.25)