import json, time, os, torch, random
from transformers import AutoTokenizer, AutoModelForCausalLM

DATA_PATH = os.path.expanduser("~/.torcs/DrivingData/live_data.json")
MODEL_NAME = "ibm-granite/granite-4.0-350m"

print("Loading Granite for Live Commentary...")
tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)
model = AutoModelForCausalLM.from_pretrained(
    MODEL_NAME,
    torch_dtype=torch.float32,
    low_cpu_mem_usage=True
).to("cpu")
model.eval()
print("Model loaded successfully!")

TEMPLATE_CHANCE = 0.6
last_corkscrew_time = 0
last_lowspeed_time = 0

def get_sector(seg_id):
    if seg_id < 40:  return 1
    if seg_id < 100: return 2
    if seg_id < 175: return 3
    if seg_id < 235: return 4
    if seg_id < 310: return 5
    if seg_id < 390: return 6
    if seg_id < 500: return 7
    if seg_id < 540: return 8
    if seg_id < 604: return 9
    return 9

FREE_PROMPTS = [
    "You are an F1 TV commentator. Describe the tension in the crowd in ONE short sentence.\nCommentator says: \"",
    "You are an F1 TV commentator. Comment on the driving style you are seeing in ONE short sentence.\nCommentator says: \"",
    "You are an F1 TV commentator. Talk about the championship implications of this race in ONE short sentence.\nCommentator says: \"",
    "You are an F1 TV commentator. React to the conditions on track in ONE short sentence.\nCommentator says: \"",
    "You are an F1 TV commentator. Say something dramatic about this moment in the race in ONE short sentence.\nCommentator says: \"",
]

def generate_free_commentary():
    prompt = random.choice(FREE_PROMPTS)
    inputs = tokenizer(prompt, return_tensors="pt")
    with torch.no_grad():
        output = model.generate(
            **inputs,
            max_new_tokens=30,
            do_sample=True,
            temperature=0.9,
            repetition_penalty=1.4,
            no_repeat_ngram_size=4
        )
    result = tokenizer.decode(output[0][inputs.input_ids.shape[1]:], skip_special_tokens=True).strip()
    return result.split('"')[0].split('\n')[0].strip()

def generate_commentary(data):
    global last_corkscrew_time, last_lowspeed_time

    sector = get_sector(data['Segment'])
    track_pos = data['trackPos']
    off_track = abs(track_pos) > 7.5
    damage_str = "no damage" if data['damage'] == 0 else f"damage at level {data['damage']}"
    speed = int(data['speed'])
    gear = data['gear']

    # Off track always takes priority
    if off_track:
        fact_line = f"The car is OFF TRACK in sector {sector}, speed {speed}km/h, gear {gear}, {damage_str}."
        prompt = (
            f"F1 commentator reacts in ONE punchy sentence with NO generic phrases like 'looks like' or 'situation'.\n"
            f"Facts: {fact_line}\n"
            f"React using the exact speed and sector number: \""
        )
        inputs = tokenizer(prompt, return_tensors="pt")
        with torch.no_grad():
            output = model.generate(
                **inputs,
                max_new_tokens=30,
                do_sample=True,
                temperature=0.6,
                repetition_penalty=1.5,
                no_repeat_ngram_size=4
            )
        result = tokenizer.decode(output[0][inputs.input_ids.shape[1]:], skip_special_tokens=True).strip()
        result = result.split('"')[0].split('\n')[0].strip()
        if str(speed) not in result and str(sector) not in result:
            result = f"He's gone off track in sector {sector} at {speed}km/h!"
        return result

    # Corkscrew - once per 60 seconds only
    if sector == 7:
        now = time.time()
        if now - last_corkscrew_time >= 60:
            last_corkscrew_time = now
            if speed < 0:
                return f"He's going the wrong way through the corkscrew!"
            elif speed > 100:
                return f"He's approaching the corkscrew at a speed of {speed}km/h, will he make it?"
            elif speed < 90:
                return f"They've slowed down for the corkscrew, crawling through at just {speed}km/h."
            else:
                return f"Carefully through the corkscrew at {speed}km/h!"

    # Insane speed
    if speed > 300:
        return f"300KM/H! Absolutely unreal speed in sector {sector}!"
    if speed > 270:
        lines = [
            f"UNBELIEVABLE! {speed}km/h through sector {sector}, this is absolute madness!",
            f"What a speed! {speed}km/h — this car is an absolute rocket!",
            f"{speed}km/h! I don't believe what I'm seeing in sector {sector}!",
        ]
        return random.choice(lines)

    # Low speed - once per 60 seconds only
    if speed < 50:
        now = time.time()
        if now - last_lowspeed_time >= 60:
            last_lowspeed_time = now
            return f"{speed}km/h? What's he playing at? This ain't go-karting buddy."

    # Random free line
    if random.random() < TEMPLATE_CHANCE:
        return generate_free_commentary()

    # Standard data-driven commentary
    fact_line = f"Sector {sector} of 9, speed {speed}km/h, gear {gear}, {damage_str}."
    prompt = (
        f"F1 commentator reacts in ONE punchy sentence with NO generic phrases like 'looks like' or 'situation'.\n"
        f"Facts: {fact_line}\n"
        f"React using the exact speed and sector number: \""
    )

    inputs = tokenizer(prompt, return_tensors="pt")
    with torch.no_grad():
        output = model.generate(
            **inputs,
            max_new_tokens=30,
            do_sample=True,
            temperature=0.6,
            repetition_penalty=1.5,
            no_repeat_ngram_size=4
        )
    result = tokenizer.decode(output[0][inputs.input_ids.shape[1]:], skip_special_tokens=True).strip()
    result = result.split('"')[0].split('\n')[0].strip()

    if str(speed) not in result and str(sector) not in result:
        result = f"Sector {sector}, pushing hard at {speed}km/h in gear {gear}!"

    return result

last_mtime = 0
while True:
    try:
        current_mtime = os.path.getmtime(DATA_PATH)
        if current_mtime != last_mtime:
            with open(DATA_PATH, 'r') as f:
                data = json.load(f)
            commentary = generate_commentary(data)
            print(f"\n[LIVE]: {commentary}")
            # ADD THIS:
            with open("/tmp/live_commentary.txt", "w") as f:
                f.write(commentary)
            last_mtime = current_mtime
    except Exception as e:
        print(e)
    time.sleep(0.5)