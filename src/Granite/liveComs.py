import json, time, os, torch, random, signal, traceback
from transformers import AutoTokenizer, AutoModelForCausalLM

# ── Force-kill on Ctrl+C ──────────────────────────────────────────────────────
def _force_exit(sig, frame):
    print("\n[INFO] Interrupted — shutting down.")
    os._exit(0)

signal.signal(signal.SIGINT,  _force_exit)
signal.signal(signal.SIGTERM, _force_exit)

DATA_PATH  = os.path.expanduser("~/.torcs/DrivingData/live_data.json")
MODEL_NAME = "ibm-granite/granite-3.1-1b-a400m-instruct"

print("Loading Granite for Live Commentary...")
tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)
model = AutoModelForCausalLM.from_pretrained(
    MODEL_NAME,
    dtype=torch.float32,
    low_cpu_mem_usage=True
).to("cpu")
model.eval()
print("Model loaded successfully!")

HAS_CHAT_TEMPLATE = (
    hasattr(tokenizer, "apply_chat_template")
    and tokenizer.chat_template is not None
)
if HAS_CHAT_TEMPLATE:
    print("[INFO] Using apply_chat_template.")
else:
    print("[WARN] No chat template — using manual fallback.")


def get_sector(seg_id):
    """Map the segment ID to a sector number (1-9) based on track layout."""
# ══════════════════════════════════════════════════════════════════════════════
#  EVENT CONTROLLER
# ══════════════════════════════════════════════════════════════════════════════

# ── Tuneable thresholds ───────────────────────────────────────────────────────
CFG = {
    "off_track_threshold":   7.5,    # |trackPos| > this  → off track
    "high_speed_threshold":  270,    # km/h
    "low_speed_threshold":   50,     # km/h
    "spin_delta_threshold":  80,     # km/h drop in one cycle → spin
    "battle_gap_threshold":  0.15,   # track-position units (opponents array)
    "crash_damage_jump":     500,    # damage-322point jump in one cycle → crash
    "corkscrew_sector":      7,

    # per-event cooldowns (seconds)
    "cooldown": {
        "OFF_TRACK":    6,
        "CRASH":        10,
        "SPIN":         12,
        "DAMAGE":       15,
        "OVERTAKE":     8,
        "BATTLE":       20,
        "LAP_COMPLETE": 5,
        "HIGH_SPEED":   8,
        "CORKSCREW":    60,
        "LOW_SPEED":    60,
        "STANDARD":     4,
        "ATMOSPHERE":   30,
    }
}

# ── Sector helper ─────────────────────────────────────────────────────────────
def get_sector(seg_id: int) -> int:
    if seg_id < 40:  return 1
    if seg_id < 100: return 2
    if seg_id < 175: return 3
    if seg_id < 235: return 4
    if seg_id < 310: return 5
    if seg_id < 390: return 6
    if seg_id < 500: return 7
    if seg_id < 540: return 8
    return 9

# ── Event class ───────────────────────────────────────────────────────────────
class Event:
    def __init__(self, name: str, priority: int, temperature: float,
                 prompt_fn, *prompt_args):
        self.name        = name
        self.priority    = priority
        self.temperature = temperature
        self._prompt_fn  = prompt_fn
        self._args       = prompt_args

    def build_prompt(self) -> str:
        return self._prompt_fn(*self._args)

    def __repr__(self):
        return f"<Event {self.name} priority={self.priority}>"

# ── Prompt builders ───────────────────────────────────────────────────────────
def _p_off_track(sector, speed):
    return (f"The car has left the track in sector {sector} at {speed} km/h. "
            f"Say one dramatic sentence about going off track at {speed} km/h.")

def _p_crash(damage_jump, sector):
    return (f"The car suffered a sudden heavy impact in sector {sector}, "
            f"taking {damage_jump} damage points in an instant. "
            f"Say one alarmed sentence about this major crash.")

def _p_spin(speed, sector):
    return (f"The car suddenly lost speed to {speed} km/h in sector {sector} — "
            f"a spin or lockup. "
            f"Say one dramatic sentence about the car spinning in sector {sector}.")

def _p_damage(damage, sector):
    return (f"The car has taken damage level {damage} in sector {sector}. "
            f"Say one concerned sentence about the damage in sector {sector}.")

def _p_overtake(place):
    return (f"The driver just overtook another car and moved into position {place}. "
            f"Say one excited sentence about moving into position {place}.")

def _p_battle(gap):
    gap_m = int(gap * 100)
    return (f"An opponent is only {gap_m} metres behind. "
            f"Say one tense sentence about the close battle happening right now.")

def _p_lap(lap, place):
    return (f"The driver has completed lap {lap} and is currently in position {place}. "
            f"Say one upbeat sentence about finishing lap {lap} in position {place}.")

def _p_high_speed(sector, speed):
    return (f"The car is at {speed} km/h in sector {sector}. "
            f"Say one amazed sentence about the car reaching {speed} km/h.")

def _p_corkscrew(speed):
    return (f"The car is entering the Corkscrew corner at {speed} km/h. "
            f"Say one excited sentence about taking the Corkscrew at {speed} km/h.")

def _p_low_speed(speed):
    return (f"The car has slowed to only {speed} km/h. "
            f"Say one concerned sentence about the car being at only {speed} km/h.")

def _p_standard(sector, speed, gear, place):
    return (f"The car is in sector {sector}, travelling at {speed} km/h, "
            f"in gear {gear}, in position {place}. "
            f"Give a one-sentence race update using those exact numbers.")

_ATMOSPHERE_OPTIONS = [
    "Say one dramatic sentence about the tension of a motorsport race. Do not mention any driver name.",
    "Say one sentence about how brave motorsport drivers are in general.",
    "Say one dramatic sentence about the crowd atmosphere at a race. Do not mention any name.",
    "Say one sentence about the raw power of an F1 car producing 800 horsepower.",
    "Say one sentence about the incredible speeds seen in motorsport today.",
]

def _p_atmosphere():
    return random.choice(_ATMOSPHERE_OPTIONS)

# ── Controller ────────────────────────────────────────────────────────────────
class EventController:
    """
    Priority ladder (highest → lowest):
      10  OFF_TRACK       car left the track
      9   CRASH           sudden large damage jump
      8   SPIN            sudden severe speed loss
      7   DAMAGE          any ongoing damage
      6   OVERTAKE        gained a position
      5   BATTLE          opponent close behind
      4   LAP_COMPLETE    crossed the finish line
      3   HIGH_SPEED      over 270 km/h
      2   CORKSCREW       sector 7 entry
      1   LOW_SPEED       under 50 km/h
      0   STANDARD        generic fallback
     -1   ATMOSPHERE      random colour commentary
    """

    def __init__(self, cfg: dict = None):
        self.cfg          = cfg or CFG
        self._last_fired  = {}
        self._prev_speed  = None
        self._prev_damage = 0
        self._prev_place  = None
        self._prev_lap    = 0

    def get_event(self, data: dict):
        candidates = self._detect(data)
        if not candidates:
            return None

        candidates.sort(key=lambda e: e.priority, reverse=True)
        now = time.time()

        chosen = None
        for event in candidates:
            cd   = self.cfg["cooldown"].get(event.name, 5)
            last = self._last_fired.get(event.name, 0)
            if now - last >= cd:
                chosen = event
                break

        if chosen:
            self._last_fired[chosen.name] = now

        self._prev_speed  = int(data.get("speed",      0))
        self._prev_damage = int(data.get("damage",     0))
        self._prev_place  = int(data.get("place",     99))
        self._prev_lap    = int(data.get("currentLap", data.get("lap", 0)))

        return chosen

    def _detect(self, data: dict) -> list:
        c = self.cfg

        sector    = get_sector(int(data.get("Segment",   0)))
        speed     = int(data.get("speed",    0))
        gear      = int(data.get("gear",     0))
        place     = int(data.get("place",   99))
        damage    = int(data.get("damage",   0))
        track_pos = float(data.get("trackPos", 0))
        lap       = int(data.get("currentLap", data.get("lap", 0)))
        opponents = data.get("opponents", [])

        events = []

        # Priority 10: OFF TRACK
        if abs(track_pos) > c["off_track_threshold"]:
            events.append(Event("OFF_TRACK", 10, 0.7,
                                _p_off_track, sector, speed))

        # Priority 9: CRASH
        damage_jump = damage - self._prev_damage
        if damage_jump >= c["crash_damage_jump"]:
            events.append(Event("CRASH", 9, 0.7,
                                _p_crash, damage_jump, sector))

        # Priority 8: SPIN
        if (self._prev_speed is not None
                and self._prev_speed - speed >= c["spin_delta_threshold"]
                and speed < 120):
            events.append(Event("SPIN", 8, 0.75,
                                _p_spin, speed, sector))

        # Priority 7: DAMAGE
        if damage > 0 and damage_jump < c["crash_damage_jump"]:
            events.append(Event("DAMAGE", 7, 0.75,
                                _p_damage, damage, sector))

        # Priority 6: OVERTAKE
        if self._prev_place is not None and place < self._prev_place:
            events.append(Event("OVERTAKE", 6, 0.7,
                                _p_overtake, place))

        # Priority 5: BATTLE
        if opponents:
            behind = [abs(o) for o in opponents if o < 0]
            if behind and min(behind) <= c["battle_gap_threshold"]:
                events.append(Event("BATTLE", 5, 0.75,
                                    _p_battle, min(behind)))

        # Priority 4: LAP COMPLETE
        if lap > self._prev_lap and self._prev_lap > 0:
            events.append(Event("LAP_COMPLETE", 4, 0.7,
                                _p_lap, lap, place))

        # Priority 3: HIGH SPEED
        if speed > c["high_speed_threshold"]:
            events.append(Event("HIGH_SPEED", 3, 0.7,
                                _p_high_speed, sector, speed))

        # Priority 2: CORKSCREW
        if sector == c["corkscrew_sector"]:
            events.append(Event("CORKSCREW", 2, 0.75,
                                _p_corkscrew, speed))

        # Priority 1: LOW SPEED
        if speed < c["low_speed_threshold"]:
            events.append(Event("LOW_SPEED", 1, 0.75,
                                _p_low_speed, speed))

        # Priority 0: STANDARD (always present as fallback)
        events.append(Event("STANDARD", 0, 0.75,
                            _p_standard, sector, speed, gear, place))

        # Priority -1: ATMOSPHERE (35% chance)
        if random.random() < 0.35:
            events.append(Event("ATMOSPHERE", -1, 0.85,
                                _p_atmosphere))

        return events


# ── Instantiate controller ────────────────────────────────────────────────────
controller = EventController()


# ══════════════════════════════════════════════════════════════════════════════
#  LLM
# ══════════════════════════════════════════════════════════════════════════════

SYSTEM = (
    "You are a live motorsport TV commentator. "
    "Output ONLY one sentence of 10 to 20 words. "
    "Use ONLY the facts in the user message. "
    "Do NOT invent driver names, team names, or any detail not given. "
    "Do NOT use 'it seems', 'it looks like', or 'appears'. "
    "Do NOT use quotes or asterisks. "
    "Start directly with the commentary."
)

def generate_ai(user_prompt, temperature=0.8):
    if HAS_CHAT_TEMPLATE:
        messages = [
            {"role": "system", "content": SYSTEM},
            {"role": "user",   "content": user_prompt}
        ]
        prompt_str = tokenizer.apply_chat_template(
            messages,
            tokenize=False,
            add_generation_prompt=True
        )
    else:
        prompt_str = f"[INST] <<SYS>>\n{SYSTEM}\n<</SYS>>\n\n{user_prompt} [/INST]"

    encoded   = tokenizer(prompt_str, return_tensors="pt")
    input_ids = encoded.input_ids
    attn_mask = encoded.attention_mask

FREE_PROMPTS = [
    "You are an F1 TV commentator. Describe the tension in the crowd in ONE short sentence.\nCommentator says: \"",
    "You are an F1 TV commentator. Comment on the driving style you are seeing in ONE short sentence.\nCommentator says: \"",
    "You are an F1 TV commentator. React to the conditions on track in ONE short sentence.\nCommentator says: \"",
    "You are an F1 TV commentator. Say something dramatic about this moment in the race in ONE short sentence.\nCommentator says: \"",
    "You are an F1 TV commentator. Comment on the car the racer is driving in ONE short sentance: Its a open wheel F1 Car 1.6L Turbo 800hp \nCommentator says: \"",
]

def generate_free_commentary():
    """Generate a free-form commentary line using a random prompt."""
    prompt = random.choice(FREE_PROMPTS)
    inputs = tokenizer(prompt, return_tensors="pt")
    with torch.no_grad():
        output = model.generate(
            input_ids,
            attention_mask=attn_mask,
            max_new_tokens=30,
            do_sample=True,
            temperature=temperature,
            repetition_penalty=1.4,
            no_repeat_ngram_size=4,
            pad_token_id=tokenizer.eos_token_id
        )

    result = tokenizer.decode(
        output[0][input_ids.shape[1]:], skip_special_tokens=True
    ).strip()

    result = result.lstrip('"\'')

    for stop in ['.', '!', '?']:
        idx = result.find(stop)
        if idx != -1:
            result = result[:idx + 1]
            break

    if len(result) < 10:
        print(f"[DEBUG] Output too short, discarding: '{result}'")
        return None

    return result.strip()


<<<<<<< HEAD
# ── Event prompts — each tells the model exactly what to say ─────────────────
def prompt_off_track(sector, speed):
    return (
        f"The car has left the track in sector {sector} at {speed} km/h. "
        f"Say one dramatic sentence about the car going off track at {speed} km/h."
    )
def generate_commentary(data):
    """Generate live commentary based on the current telemetry data."""
    global last_corkscrew_time, last_lowspeed_time, last_place, last_overtake_time  # Fix 1: added last_place and last_overtake_time
=======
# ══════════════════════════════════════════════════════════════════════════════
#  COMMENTARY
# ══════════════════════════════════════════════════════════════════════════════
>>>>>>> e665df8 (Chnaged prompt layout for the comms - uses an event controller. Also added segment times for all other maps, changed the printout report to accomodate this and various other changes.)

def generate_commentary(data):
    event = controller.get_event(data)
    if event is None:
        return None

    print(f"[EVENT] {event.name} (priority {event.priority})")
    return generate_ai(event.build_prompt(), temperature=event.temperature)


# ══════════════════════════════════════════════════════════════════════════════
#  FILE-WATCH LOOP
# ══════════════════════════════════════════════════════════════════════════════

print(f"[INFO] Watching: {DATA_PATH}")
print("[INFO] Waiting for TORCS to start writing data...\n")

last_mtime = 0
while True:
    try:
        if not os.path.exists(DATA_PATH):
            print(f"[WAITING] Data file not found: {DATA_PATH}")
            time.sleep(2)
            continue

        current_mtime = os.path.getmtime(DATA_PATH)
        if current_mtime != last_mtime:
            last_mtime = current_mtime

            with open(DATA_PATH, 'r') as f:
                data = json.load(f)

            commentary = generate_commentary(data)
            if commentary:
                print(f"[LIVE]: {commentary}")
                with open(os.path.expanduser("~/.torcs/DrivingData/live_commentary.txt"), "w") as f:
                    f.write(commentary)

    except FileNotFoundError as e:
        print(f"[FILE ERROR] {e}")
        time.sleep(2)
    except json.JSONDecodeError as e:
        print(f"[JSON ERROR] Malformed data file — {e}")
    except KeyError as e:
        print(f"[DATA ERROR] Missing expected key in JSON: {e}")
    except Exception as e:
        print(f"[ERROR] {type(e).__name__}: {e}")
        traceback.print_exc()

    time.sleep(0.5)