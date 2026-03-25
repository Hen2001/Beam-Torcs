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
MODEL_NAME = "ibm-granite/granite-4.0-350m"

# ── Load Model ────────────────────────────────────────────────────────────────
print("Loading Granite...")
tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)
model = AutoModelForCausalLM.from_pretrained(
    MODEL_NAME,
    torch_dtype=torch.float32,
    low_cpu_mem_usage=True
).to("cpu")
model.eval()
print("Model loaded.")

HAS_CHAT_TEMPLATE = (hasattr(tokenizer, "apply_chat_template") and tokenizer.chat_template is not None)

# ── Sector Mapping ────────────────────────────────────────────────────────────
def get_sector(seg_id: int, track_name: str) -> int:
    t = track_name.lower()
    if "corkscrew" in t:
        if seg_id < 40:  return 1
        if seg_id < 100: return 2
        if seg_id < 175: return 3
        if seg_id < 235: return 4
        if seg_id < 310: return 5
        if seg_id < 390: return 6
        if seg_id < 500: return 7
        if seg_id < 540: return 8
        return 9
    elif "speedway" in t:
        if seg_id < 81:  return 1
        return 2 if seg_id < 191 else 3
    elif "spring" in t:
        return (seg_id // 150) + 1
    elif "forza" in t:
        return (seg_id // 140) + 1
    return (seg_id // 100) + 1

# ── Hardcoded Commentary Lines ────────────────────────────────────────────────
LINES = {
    "BATTLE": [
        ""
    ]
    "OFF_TRACK": [
        "Car off track in sector {sec} — driver fighting to recover.",
        "Running wide in sector {sec} at {spd} km/h, losing time.",
        "Off the racing line in sector {sec}, grass under the tyres.",
        "Lost it at sector {sec}, down to {spd} km/h.",
        "Moment of drama in sector {sec} — car leaving the circuit.",
        "Track limits exceeded in sector {sec}, big moment for the driver.",
        "Into the gravel at sector {sec}, that'll cost some time.",
        "Car understeering wide in sector {sec} at {spd} km/h.",
    ],
    "CRASH": [
        "Heavy contact detected — {dmg} damage points sustained.",
        "Big impact in sector {sec}, the car is taking serious damage.",
        "Collision in sector {sec} — that's going to hurt.",
        "Significant damage after that hit, {dmg} points on the clock.",
        "That was a heavy one — car damaged in sector {sec}.",
        "Wall contact in sector {sec}, damage racking up.",
    ],
    "SPIN": [
        "Snap oversteer in sector {sec} — driver catching a slide.",
        "Rear stepped out in sector {sec}, speed dropping fast.",
        "Loss of control in sector {sec}, down to {spd} km/h.",
        "Car spinning in sector {sec} — recovery underway.",
        "Big moment through sector {sec}, the rear came around.",
        "Oversteer in sector {sec} — that cost some serious time.",
        "Driver fighting a spin in sector {sec}.",
        "The car got away from them in sector {sec}.",
    ],
    "OVERTAKE": [
        "Position gained — now running P{pos}.",
        "Move completed, up to P{pos} at {spd} km/h.",
        "Overtake done, P{pos} secured through sector {sec}.",
        "Through into P{pos}, pushing hard at {spd} km/h.",
        "That's a place gained — P{pos} and looking strong.",
        "Clean move in sector {sec}, up to P{pos}.",
        "Aggressive move pays off — P{pos} now.",
        "Through into the points — P{pos} on the road.",
    ],
    "LAP": [
        "Lap {lap} complete — sitting P{pos}.",
        "Through the line to start lap {lap}, currently P{pos}.",
        "Lap {lap} in the books, P{pos} on the road.",
        "Crosses the line — lap {lap} underway, P{pos}.",
        "Another lap done, {lap} complete and running P{pos}.",
        "End of lap {lap} — P{pos}, let's see what this one brings.",
        "Lap {lap} signed off, driver holds P{pos}.",
        "Clean lap {lap} completed, P{pos} maintained.",
    ],
    "STANDARD": [
        "Through sector {sec} at {spd} km/h, holding P{pos}.",
        "Running P{pos} through sector {sec}, {spd} km/h.",
        "Sector {sec}, P{pos}, carrying {spd} km/h.",
        "P{pos} in sector {sec}, {spd} km/h on the board.",
        "Solid through sector {sec} at {spd} km/h.",
        "Pushing through sector {sec}, P{pos} at {spd} km/h.",
        "Clean run through sector {sec}, {spd} km/h.",
        "Sector {sec} looking good — {spd} km/h, P{pos}.",
        "{spd} km/h through sector {sec}, P{pos} on track.",
        "Committed through sector {sec} at {spd} km/h.",
        "Smooth through sector {sec}, {spd} km/h.",
        "No dramas in sector {sec}, {spd} km/h, P{pos}.",
        "Driver focused through sector {sec} at {spd} km/h.",
        "Good pace in sector {sec} — {spd} km/h, P{pos}.",
        "Holding the line in sector {sec} at {spd} km/h.",
    ],
}

# ── Granite Templates (prefix completion for LAP / OVERTAKE) ─────────────────
GRANITE_TEMPLATES = {
    "OVERTAKE": [
        "Driver moves up to P{pos} in sector {sec} —",
        "Position gained at {spd} km/h in sector {sec} —",
    ],
    "LAP": [
        "Lap {lap} complete, P{pos} —",
        "Through the line, lap {lap} done —",
    ],
}

# ── Granite: Short Completion ─────────────────────────────────────────────────
SYSTEM_COMPLETION = (
    "You are a racing commentator. Complete this sentence fragment with "
    "exactly 4 words or fewer. Be punchy and specific. No filler, no names."
)

def granite_complete(prefix: str, temperature: float = 0.55) -> str:
    try:
        if HAS_CHAT_TEMPLATE:
            msg = [
                {"role": "system", "content": SYSTEM_COMPLETION},
                {"role": "user",   "content": prefix}
            ]
            p_str = tokenizer.apply_chat_template(msg, tokenize=False, add_generation_prompt=True)
        else:
            p_str = f"{SYSTEM_COMPLETION}\n{prefix}"

        inputs = tokenizer(p_str, return_tensors="pt").to("cpu")
        with torch.no_grad():
            out = model.generate(
                **inputs,
                max_new_tokens=7,
                do_sample=False,
                repetition_penalty=1.4
            )

        txt = tokenizer.decode(out[0], skip_special_tokens=True)
        res = txt.split("assistant")[-1].strip() if HAS_CHAT_TEMPLATE else txt.split(prefix)[-1].strip()
        res = res.split(".")[0].split("\n")[0].strip()
        res = res.replace('"', '').replace("'", "").strip()

        words = res.split()
        if len(words) < 2 or len(words) > 6:
            return ""
        prefix_words = set(prefix.lower().split())
        if sum(1 for w in words if w.lower() in prefix_words) > 2:
            return ""
        return res

    except Exception:
        traceback.print_exc()
        return ""

# ── Granite: Freeform (used for STANDARD ~30% of the time) ───────────────────
RACING_WORDS = {
    "sector", "km/h", "lap", "position", "corner", "braking", "throttle",
    "pace", "gap", "line", "smooth", "pushing", "carrying", "holding",
    "through", "into", "clean", "fast", "hard", "strong", "tight"
}

SYSTEM_FREEFORM = (
    "You are an F1 TV commentator. Write exactly ONE short sentence of race commentary. "
    "You must mention the speed and sector number provided. "
    "Max 12 words. No driver names. No team names. No questions. No filler."
)

def granite_freeform(spd: int, sec: int, pos: int, temperature: float = 0.6) -> str:
    try:
        user_prompt = f"Sector {sec}, speed {spd} km/h, position P{pos}. One commentary sentence."

        if HAS_CHAT_TEMPLATE:
            msg = [
                {"role": "system", "content": SYSTEM_FREEFORM},
                {"role": "user",   "content": user_prompt}
            ]
            p_str = tokenizer.apply_chat_template(msg, tokenize=False, add_generation_prompt=True)
        else:
            p_str = f"{SYSTEM_FREEFORM}\n{user_prompt}\nCommentary:"

        inputs = tokenizer(p_str, return_tensors="pt").to("cpu")
        with torch.no_grad():
            out = model.generate(
                **inputs,
                max_new_tokens=20,
                do_sample=True,
                temperature=temperature,
                repetition_penalty=1.4
            )

        txt = tokenizer.decode(out[0], skip_special_tokens=True)
        res = txt.split("assistant")[-1].strip() if HAS_CHAT_TEMPLATE else txt.split("Commentary:")[-1].strip()
        res = res.split(".")[0].split("\n")[0].strip()
        res = res.replace('"', '').replace("'", "").strip()

        return res

    except Exception:
        traceback.print_exc()
        return ""

def validate_granite(text: str, spd: int, sec: int) -> bool:
    words = text.split()
    if not (4 <= len(words) <= 14):
        return False
    if str(spd) not in text and str(sec) not in text:
        return False
    lower_words = set(w.lower().strip(".,!?") for w in words)
    if not lower_words.intersection(RACING_WORDS):
        return False
    if text.lower().startswith("sector") and len(words) < 6:
        return False
    return True

# ── Event Controller ──────────────────────────────────────────────────────────
CFG = {
    "off_track_threshold":  7.5,
    "crash_damage_jump":    400,
    "spin_speed_drop":      80,
    "spin_max_speed":       90,
    "spin_track_pos_min":   3.5,
    "cooldown": {
        "OFF_TRACK": 10, "CRASH": 15, "SPIN": 20,
        "OVERTAKE":  10, "LAP":   12, "STANDARD": 8
    }
}

class Event:
    def __init__(self, name, priority):
        self.name     = name
        self.priority = priority

class EventController:
    def __init__(self):
        self._fired = {}
        self._prev  = {"speed": 0, "damage": 0, "place": 99, "lap": 0}

    def get_event(self, data: dict):
        now  = time.time()
        spd  = abs(int(data.get("speed",    0)))
        pos  = int(data.get("place",    99))
        dmg  = int(data.get("damage",   0))
        tpos = abs(float(data.get("trackPos", 0)))
        lap  = int(data.get("currentLap", 0))

        evs = []

        if tpos > CFG["off_track_threshold"]:
            evs.append(Event("OFF_TRACK", 10))

        if dmg - self._prev["damage"] >= CFG["crash_damage_jump"]:
            evs.append(Event("CRASH", 9))

        speed_drop = self._prev["speed"] - spd
        if (speed_drop >= CFG["spin_speed_drop"]
                and spd < CFG["spin_max_speed"]
                and tpos >= CFG["spin_track_pos_min"]):
            evs.append(Event("SPIN", 8))

        if pos < self._prev["place"] and self._prev["place"] != 99:
            evs.append(Event("OVERTAKE", 7))

        if lap > self._prev["lap"] and self._prev["lap"] > 0:
            evs.append(Event("LAP", 5))

        evs.append(Event("STANDARD", 0))

        evs.sort(key=lambda e: e.priority, reverse=True)
        for e in evs:
            if now - self._fired.get(e.name, 0) >= CFG["cooldown"].get(e.name, 5):
                self._fired[e.name] = now
                self._prev.update({"speed": spd, "damage": dmg, "place": pos, "lap": lap})
                return e
        return None

controller = EventController()

# ── Commentary Builder ────────────────────────────────────────────────────────
_last_used = {}

def build_commentary(event: Event, data: dict) -> str:
    spd = abs(int(data.get("speed",      0)))
    pos = int(data.get("place",      99))
    dmg = int(data.get("damage",     0))
    lap = int(data.get("currentLap", 0))
    sec = get_sector(int(data.get("Segment", 0)), data.get("trackName", ""))
    fmt = dict(spd=spd, pos=pos, sec=sec, dmg=dmg, lap=lap)

    # ── Granite freeform: STANDARD, 30% chance ──────────────────────────────
    if event.name == "STANDARD" and random.random() < 0.30:
        result = granite_freeform(spd, sec, pos)
        if result:
            print(f"  [granite] {result}")
            return result

    # ── Granite completion: LAP and OVERTAKE, 50% chance ────────────────────
    if event.name in GRANITE_TEMPLATES and random.random() < 0.5:
        prefix = random.choice(GRANITE_TEMPLATES[event.name]).format(**fmt)
        ending = granite_complete(prefix)
        if ending:
            result = f"{prefix} {ending}"
            print(f"  [granite] {result}")
            return result

    # ── Hardcoded fallback ───────────────────────────────────────────────────
    pool  = LINES[event.name]
    last  = _last_used.get(event.name, -1)
    avoid = _last_used.get(f"{event.name}_prev", -1)
    indices = [i for i in range(len(pool)) if i != last and i != avoid]
    if not indices:
        indices = list(range(len(pool)))
    idx = random.choice(indices)
    _last_used[f"{event.name}_prev"] = last
    _last_used[event.name] = idx
    return pool[idx].format(**fmt)

# ── Main Loop ─────────────────────────────────────────────────────────────────
print(f"[INFO] Monitoring {DATA_PATH}...")
last_mtime = 0

while True:
    try:
        if os.path.exists(DATA_PATH):
            mtime = os.path.getmtime(DATA_PATH)
            if mtime != last_mtime:
                time.sleep(0.05)
                with open(DATA_PATH, 'r') as f:
                    data = json.load(f)

                event = controller.get_event(data)
                if event:
                    commentary = build_commentary(event, data)
                    print(f"[{event.name}] {commentary}")
                    with open(COMM_PATH, "w") as cf:
                        cf.write(commentary)
                last_mtime = mtime

    except (json.JSONDecodeError, ValueError):
        pass
    except Exception:
        traceback.print_exc()
    time.sleep(0.25)