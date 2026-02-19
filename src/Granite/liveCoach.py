import json, time, os, torch, random
from transformers import AutoTokenizer, AutoModelForCausalLM

DATA_PATH = os.path.expanduser("~/.torcs/DrivingData/live_coaching_data.json")
OUTPUT_PATH = os.path.expanduser("~/.torcs/live_coaching.txt")
MODEL_NAME = "ibm-granite/granite-4.0-350m"

SEGMENT_NAMES = [
    "First Straight", "Hairpin", "Corner 2", "Corner 3",
    "Long Left", "Back Straight", "The Corkscrew", "Kink", "Final Straight"
]

print("Loading Granite for Live Coaching...")
tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)
model = AutoModelForCausalLM.from_pretrained(
    MODEL_NAME,
    torch_dtype=torch.float32,
    low_cpu_mem_usage=True
).to("cpu")
model.eval()
print("Model loaded!")

def generate_coaching(prompt):
    inputs = tokenizer(prompt, return_tensors="pt")
    with torch.no_grad():
        output = model.generate(
            **inputs,
            max_new_tokens=35,
            do_sample=True,
            temperature=0.7,
            repetition_penalty=1.4,
            no_repeat_ngram_size=4
        )
    result = tokenizer.decode(output[0][inputs.input_ids.shape[1]:], skip_special_tokens=True).strip()
    return result.split('"')[0].split('\n')[0].strip()

def get_coaching(data):
    speed      = int(data.get('speed', 0))
    gear       = data.get('gear', 0)
    damage     = data.get('damage', 0)
    track_pos  = data.get('trackPos', 0)
    segment    = data.get('segment', 0)
    lap        = data.get('lap', 0)
    prev_time  = data.get('prevSegTime', 0.0)
    cur_time   = data.get('curSegTime', 0.0)
    delta      = data.get('delta', 0.0)
    has_prev   = data.get('hasPrevLap', False)

    seg_name = SEGMENT_NAMES[segment] if segment < len(SEGMENT_NAMES) else f"Sector {segment}"

    # No reference lap yet
    if not has_prev or lap < 2:
        prompt = (
            f"You are an F1 driving coach. The driver is on lap {lap} at {speed}km/h in gear {gear} "
            f"through the {seg_name}. No reference lap yet. Give ONE short tip for this corner.\n"
            f"Coach says: \""
        )
        return generate_coaching(prompt)

    # Damage warning - highest priority
    if damage > 500:
        return f"Significant damage at level {damage} — protect the car through {seg_name}."

    # Off track
    if abs(track_pos) > 7.5:
        return f"Get back on track! You're {abs(track_pos):.1f}m off centre through {seg_name}."

    # Delta-based coaching
    if delta != 0.0:
        sign = "+" if delta > 0 else ""
        if delta > 1.5:
            prompt = (
                f"F1 driving coach. Driver is {delta:.2f}s SLOWER than last lap through {seg_name} "
                f"at {speed}km/h gear {gear}. Give ONE sharp coaching tip to recover the time.\n"
                f"Coach says: \""
            )
            result = generate_coaching(prompt)
            if not result or len(result) < 5:
                result = f"You're {delta:.2f}s down on {seg_name} — brake later and carry more speed."
            return result
        elif delta > 0.3:
            return f"{sign}{delta:.2f}s on {seg_name} — push harder here, you have the pace."
        elif delta < -0.5:
            return f"Excellent! {delta:.2f}s up on {seg_name} — keep that momentum through the next sector."
        else:
            return f"Lap {lap} — {sign}{delta:.2f}s on {seg_name}. On pace, keep it consistent."

    prompt = (
        f"F1 driving coach. Driver at {speed}km/h gear {gear} through {seg_name} on lap {lap}. "
        f"Give ONE specific coaching tip for this corner.\n"
        f"Coach says: \""
    )
    return generate_coaching(prompt)

last_mtime = 0
while True:
    try:
        current_mtime = os.path.getmtime(DATA_PATH)
        if current_mtime != last_mtime:
            with open(DATA_PATH, 'r') as f:
                data = json.load(f)
            coaching = get_coaching(data)
            print(f"\n[COACH]: {coaching}")
            with open(OUTPUT_PATH, "w") as f:
                f.write(coaching)
            last_mtime = current_mtime
    except Exception as e:
        print(e)
    time.sleep(0.5)