import json, time, os, torch, random, signal, traceback
from transformers import AutoTokenizer, AutoModelForCausalLM
import sys

# Redirect stderr to the log file so TORCS can read progress
log_path = os.path.join(os.path.expanduser("~"), ".torcs", "DrivingData", "granite_error.log")
log_file = open(log_path, 'w', buffering=1)  # line buffered
sys.stderr = log_file

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

last_corkscrew_time = 0
last_lowspeed_time  = 0
last_place          = None
last_overtake_time  = 0
last_comment_time   = 0
COOLDOWN            = 4

def get_sector(seg_id):
    """Map the segment ID to a sector number (1-9) based on track layout."""
    if seg_id < 40:  return 1
    if seg_id < 100: return 2
    if seg_id < 175: return 3
    if seg_id < 235: return 4
    if seg_id < 310: return 5
    if seg_id < 390: return 6
    if seg_id < 500: return 7
    if seg_id < 540: return 8
    return 9

# ── Core generation ───────────────────────────────────────────────────────────
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
    attn_mask = encoded.attention_mask   # ← fixes the attention mask warning

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

    # Strip any leading punctuation/quotes the model adds
    result = result.lstrip('"\'')

    # Keep only the first sentence
    for stop in ['.', '!', '?']:
        idx = result.find(stop)
        if idx != -1:
            result = result[:idx + 1]
            break

    # Reject if too short or model leaked a name we didn't give it
    if len(result) < 10:
        print(f"[DEBUG] Output too short, discarding: '{result}'")
        return None

    return result.strip()

# ── System prompt — tight constraints for a small model ──────────────────────
SYSTEM = (
    "You are a live motorsport TV commentator. "
    "Output ONLY one sentence of 10 to 20 words. "
    "Use ONLY the facts in the user message. "
    "Do NOT invent driver names, team names, or any detail not given. "
    "Do NOT use 'it seems', 'it looks like', or 'appears'. "
    "Do NOT use quotes or asterisks. "
    "Start directly with the commentary."
)

# ── Event prompts — each tells the model exactly what to say ─────────────────
def prompt_off_track(sector, speed):
    return (
        f"The car has left the track in sector {sector} at {speed} km/h. "
        f"Say one dramatic sentence about the car going off track at {speed} km/h."
    )
def generate_commentary(data):
    """Generate live commentary based on the current telemetry data."""
    global last_corkscrew_time, last_lowspeed_time, last_place, last_overtake_time  # Fix 1: added last_place and last_overtake_time

def prompt_corkscrew(speed):
    return (
        f"The car is entering the Corkscrew corner at {speed} km/h. "
        f"Say one excited sentence about taking the Corkscrew at {speed} km/h."
    )

def prompt_high_speed(sector, speed):
    return (
        f"The car is at {speed} km/h in sector {sector}. "
        f"Say one amazed sentence about the car reaching {speed} km/h."
    )

def prompt_low_speed(speed):
    return (
        f"The car has slowed to only {speed} km/h. "
        f"Say one concerned sentence about the car being at only {speed} km/h."
    )

def prompt_overtake(place):
    return (
        f"The driver just overtook another car and moved into position {place}. "
        f"Say one excited sentence about moving into position {place}."
    )

def prompt_damage(damage, sector):
    return (
        f"The car has taken damage level {damage} in sector {sector}. "
        f"Say one concerned sentence about the damage in sector {sector}."
    )

def prompt_standard(sector, speed, gear, place):
    return (
        f"The car is in sector {sector}, travelling at {speed} km/h, in gear {gear}, in position {place}. "
        f"Give a one-sentence race update using those exact numbers."
    )

def prompt_atmosphere():
    options = [
        "Say one dramatic sentence about the tension of a motorsport race. Do not mention any driver name.",
        "Say one sentence about how brave motorsport drivers are in general.",
        "Say one dramatic sentence about the crowd atmosphere at a race. Do not mention any name.",
        "Say one sentence about the raw power of an F1 car producing 800 horsepower.",
        "Say one sentence about the incredible speeds seen in motorsport today.",
    ]
    return random.choice(options)

# ── Main commentary logic ─────────────────────────────────────────────────────
def generate_commentary(data):
    global last_corkscrew_time, last_lowspeed_time, last_place, last_overtake_time, last_comment_time

    now = time.time()
    if now - last_comment_time < COOLDOWN:
        return None

    sector    = get_sector(data['Segment'])
    track_pos = data['trackPos']
    off_track = abs(track_pos) > 1.0
    damage    = data['damage']
    speed     = int(data['speed'])
    gear      = data['gear']
    place     = int(data['place'])

    result = None

    if off_track:
        result = generate_ai(prompt_off_track(sector, speed), temperature=0.7)
    elif sector == 7 and (now - last_corkscrew_time) >= 60:
        last_corkscrew_time = now
        result = generate_ai(prompt_corkscrew(speed), temperature=0.75)
    elif speed > 270:
        result = generate_ai(prompt_high_speed(sector, speed), temperature=0.7)
    elif speed < 50 and (now - last_lowspeed_time) >= 60:
        last_lowspeed_time = now
        result = generate_ai(prompt_low_speed(speed), temperature=0.75)
    elif damage > 0:
        result = generate_ai(prompt_damage(damage, sector), temperature=0.75)
    elif last_place is not None and place < last_place and (now - last_overtake_time) >= 10:
        last_overtake_time = now
        result = generate_ai(prompt_overtake(place), temperature=0.7)
    elif random.random() < 0.35:
        result = generate_ai(prompt_atmosphere(), temperature=0.85)
    else:
        result = generate_ai(prompt_standard(sector, speed, gear, place), temperature=0.75)

    last_place = place
    if result:
        last_comment_time = now
    return result

# ── File-watch loop ───────────────────────────────────────────────────────────
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