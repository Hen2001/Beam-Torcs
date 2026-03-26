import json, time, os, torch, random, signal, traceback
from transformers import AutoTokenizer, AutoModelForCausalLM
import sys

# ── Setup Logging ─────────────────────────────────────────────────────────────
log_path = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData", "granite_coach_error.log")
log_file = open(log_path, 'w', buffering=1)
sys.stderr = log_file

# ── Process Control ───────────────────────────────────────────────────────────
def _force_exit(sig, frame):
    print("\n[INFO] Interrupted — shutting down.")
    os._exit(0)
signal.signal(signal.SIGINT,  _force_exit)
signal.signal(signal.SIGTERM, _force_exit)

DATA_PATH  = os.path.expanduser("~/.torcs/DrivingData/live_data.json")
COACH_PATH = os.path.expanduser("~/.torcs/DrivingData/live_coaching.txt")
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

# ── Granite: Coaching Completion ──────────────────────────────────────────────
SYSTEM_COACH = (
    "You are a racing coach giving real-time advice to a driver. "
    "Complete the instruction fragment with exactly 4 words or fewer. "
    "Be direct, technical, and actionable. No filler, no names."
)

def granite_complete(prefix: str) -> str:
    try:
        if HAS_CHAT_TEMPLATE:
            msg = [
                {"role": "system", "content": SYSTEM_COACH},
                {"role": "user",   "content": prefix}
            ]
            p_str = tokenizer.apply_chat_template(msg, tokenize=False, add_generation_prompt=True)
        else:
            p_str = f"{SYSTEM_COACH}\n{prefix}"

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

# ── Granite: Freeform Coaching ────────────────────────────────────────────────
SYSTEM_FREEFORM_COACH = (
    "You are a racing coach giving real-time advice. Write exactly ONE short instruction. "
    "You must reference the sector number and delta time provided. "
    "Max 12 words. Be specific and technical. No driver names. No questions. No filler."
)

def granite_freeform(seg: int, delta: float, spd: int) -> str:
    try:
        direction = "slower" if delta > 0 else "faster"
        user_prompt = (
            f"Sector {seg}, {abs(delta):.1f}s {direction} than last lap, "
            f"current speed {spd} km/h. One coaching instruction."
        )
        if HAS_CHAT_TEMPLATE:
            msg = [
                {"role": "system", "content": SYSTEM_FREEFORM_COACH},
                {"role": "user",   "content": user_prompt}
            ]
            p_str = tokenizer.apply_chat_template(msg, tokenize=False, add_generation_prompt=True)
        else:
            p_str = f"{SYSTEM_FREEFORM_COACH}\n{user_prompt}\nCoaching:"

        inputs = tokenizer(p_str, return_tensors="pt").to("cpu")
        with torch.no_grad():
            out = model.generate(
                **inputs,
                max_new_tokens=20,
                do_sample=True,
                temperature=0.6,
                repetition_penalty=1.4
            )
        txt = tokenizer.decode(out[0], skip_special_tokens=True)
        res = txt.split("assistant")[-1].strip() if HAS_CHAT_TEMPLATE else txt.split("Coaching:")[-1].strip()
        res = res.split(".")[0].split("\n")[0].strip()
        res = res.replace('"', '').replace("'", "").strip()
        return res
    except Exception:
        traceback.print_exc()
        return ""

# ── Hardcoded Coaching Lines ──────────────────────────────────────────────────
LINES = {
    "LOSING_TIME": [
        "Sector {seg} costing you — brake later into the apex.",
        "You're {delta:.1f}s down in sector {seg}. Carry more mid-corner speed.",
        "Losing {delta:.1f}s here. Commit to the throttle earlier.",
        "Sector {seg} is your weak point. Hit the apex tighter.",
        "Too cautious in sector {seg} — trust the grip.",
    ],
    "GAINING_TIME": [
        "Sector {seg} up {abs_delta:.1f}s. Keep that rhythm.",
        "Good sector {seg}. Same entry speed next time.",
        "That's the lap time, sector {seg} is working.",
        "Strong through sector {seg} — carry that confidence.",
    ],
    "WIDE": [
        "You're running wide — tighten the line.",
        "Too much kerb. Stay tighter through the apex.",
        "Wide exit — you're losing traction on the dirty side.",
        "Bring it in — you're off the racing line.",
    ],
    "SLOW_CORNER": [
        "More speed at {spd} km/h — you're underdriving.",
        "Don't lift so early. Trust the car at {spd} km/h.",
        "You're leaving time on the table — commit earlier.",
        "Too conservative here. The car can take more.",
    ],
    "DAMAGE": [
        "Damage registered — check your balance.",
        "Car took a hit. Adjust your braking point.",
        "Damage affecting handling — compensate on entry.",
    ],
    "BATTLE": [
        "Car alongside in sector {seg} — hold your line.",
        "Opponent closing at {opp_spd} km/h — defend the apex.",
        "Side by side sector {seg} — don't get distracted.",
        "Pressure on — stay smooth, don't overcook it.",
        "Opponent P{opp_place} right there — clean lap wins this.",
    ],
    "BUILDING_BASELINE": [
        "Building your baseline — focus on clean laps.",
        "No reference yet — drive smooth, nail the lines.",
        "First lap data coming in. Stay consistent.",
        "Establishing your pace — hit every apex cleanly.",
    ],
    "STANDARD": [
        "Stay on the racing line through sector {seg}.",
        "Smooth inputs — {spd} km/h, keep it consistent.",
        "Sector {seg}: brake late, apex early, full throttle exit.",
        "Maintain this pace — sector {seg} looking clean.",
        "Focus on the exit, not the entry.",
        "Trail brake into the apex for better rotation.",
    ],
}

# ── Granite Prefix Templates ──────────────────────────────────────────────────
GRANITE_TEMPLATES = {
    "LOSING_TIME": [
        "Sector {seg}, down {delta:.1f}s —",
        "Losing time in sector {seg} —",
    ],
    "GAINING_TIME": [
        "Sector {seg} up {abs_delta:.1f}s —",
        "Fastest through sector {seg} —",
    ],
}

# ── Event Controller ──────────────────────────────────────────────────────────
CFG = {
    "delta_loss_threshold":   0.3,
    "delta_gain_threshold":  -0.2,
    "wide_threshold":         5.0,
    "slow_speed_threshold":   40,
    "crash_damage_jump":      400,
    "battle_seg_threshold":   2,    # macro segments apart to count as a battle
    "cooldown": {
        "LOSING_TIME":        12,
        "GAINING_TIME":       10,
        "WIDE":                8,
        "SLOW_CORNER":        10,
        "DAMAGE":             15,
        "BATTLE":             12,
        "BUILDING_BASELINE":  20,
        "STANDARD":            8,
    }
}

class Event:
    def __init__(self, name, priority):
        self.name     = name
        self.priority = priority

class CoachController:
    def __init__(self):
        self._fired            = {}
        self._prev             = {"damage": 0, "seg": -1}
        self._battle_opponent  = None

    def get_event(self, data: dict):
        now      = time.time()
        spd      = abs(int(data.get("speed",      0)))
        seg      = int(data.get("segment",     0))
        dmg      = int(data.get("damage",      0))
        tpos     = abs(float(data.get("trackPos", 0)))
        delta    = float(data.get("delta",       0.0))
        has_prev = data.get("hasPrevLap", False)
        evs      = []

        # No previous lap yet
        if not has_prev:
            evs.append(Event("BUILDING_BASELINE", 1))

        # Damage jump
        if dmg - self._prev["damage"] >= CFG["crash_damage_jump"]:
            evs.append(Event("DAMAGE", 10))

        # Running wide
        if tpos > CFG["wide_threshold"]:
            evs.append(Event("WIDE", 7))

        # Segment delta
        if has_prev and delta != 0.0:
            if delta > CFG["delta_loss_threshold"]:
                evs.append(Event("LOSING_TIME", 8))
            elif delta < CFG["delta_gain_threshold"]:
                evs.append(Event("GAINING_TIME", 6))

        # Unusually slow
        if spd < CFG["slow_speed_threshold"] and tpos < CFG["wide_threshold"]:
            evs.append(Event("SLOW_CORNER", 5))

        # Opponent proximity — BATTLE
        if "opponents" in data:
            player_seg = seg
            for o in data["opponents"]:
                opp_seg = int(o.get("segment", -100))
                if abs(player_seg - opp_seg) <= CFG["battle_seg_threshold"]:
                    evs.append(Event("BATTLE", 9))
                    self._battle_opponent = o
                    break
            else:
                self._battle_opponent = None

        evs.append(Event("STANDARD", 0))
        evs.sort(key=lambda e: e.priority, reverse=True)

        for e in evs:
            if now - self._fired.get(e.name, 0) >= CFG["cooldown"].get(e.name, 8):
                self._fired[e.name] = now
                self._prev.update({"damage": dmg, "seg": seg})
                return e

        return None

controller = CoachController()

# ── Coaching Builder ──────────────────────────────────────────────────────────
_last_used = {}

def build_coaching(event: Event, data: dict) -> str:
    spd       = abs(int(data.get("speed",    0)))
    seg       = int(data.get("segment",   0))
    delta     = float(data.get("delta",     0.0))
    abs_delta = abs(delta)

    # Pull opponent info if available
    opp       = controller._battle_opponent
    opp_spd   = abs(int(opp.get("speed",  0))) if opp else 0
    opp_place = int(opp.get("place",      0))  if opp else 0

    fmt = dict(
        spd=spd, seg=seg,
        delta=delta, abs_delta=abs_delta,
        opp_spd=opp_spd, opp_place=opp_place
    )

    # Granite freeform for LOSING_TIME / GAINING_TIME — 30% chance
    if event.name in ("LOSING_TIME", "GAINING_TIME") and data.get("hasPrevLap") and random.random() < 0.30:
        result = granite_freeform(seg, delta, spd)
        if result:
            print(f"  [granite] {result}")
            return result

    # Granite prefix completion for LOSING_TIME / GAINING_TIME — 50% chance
    if event.name in GRANITE_TEMPLATES and random.random() < 0.5:
        prefix = random.choice(GRANITE_TEMPLATES[event.name]).format(**fmt)
        ending = granite_complete(prefix)
        if ending:
            result = f"{prefix} {ending}"
            print(f"  [granite] {result}")
            return result

    # Hardcoded fallback
    pool    = LINES[event.name]
    last    = _last_used.get(event.name, -1)
    avoid   = _last_used.get(f"{event.name}_prev", -1)
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
                    coaching = build_coaching(event, data)
                    print(f"[{event.name}] {coaching}")
                    with open(COACH_PATH, "w") as cf:
                        cf.write(coaching)

                last_mtime = mtime

    except (json.JSONDecodeError, ValueError):
        pass
    except Exception:
        traceback.print_exc()

    time.sleep(0.25)