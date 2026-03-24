#!/bin/bash
# setup_audio.sh — Run once after cloning the repo before first use.
# Sets up WSLg audio, installs dependencies, and verifies mic access.

set -e

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║    TORCS Race Engineer — Audio Setup     ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# ── Remove ~/.asoundrc if present (breaks pyaudio on WSL) ────────────────────
if [ -f ~/.asoundrc ]; then
    echo "[!] Removing ~/.asoundrc — this file breaks pyaudio on WSLg."
    rm ~/.asoundrc
    echo "    ✓ Removed"
fi

# ── Persist PULSE_SERVER ──────────────────────────────────────────────────────
echo "[1/5] Persisting PULSE_SERVER environment variable..."
if ! grep -q "PULSE_SERVER" ~/.bashrc; then
    echo 'export PULSE_SERVER=unix:/mnt/wslg/PulseServer' >> ~/.bashrc
    echo "      ✓ Added to ~/.bashrc"
else
    echo "      ✓ Already set in ~/.bashrc"
fi
export PULSE_SERVER=unix:/mnt/wslg/PulseServer

# ── System dependencies ───────────────────────────────────────────────────────
echo "[2/5] Installing system dependencies..."
sudo apt install -y \
    python3-dev \
    portaudio19-dev \
    libpulse-dev \
    libasound2-plugins \
    pulseaudio-utils \
    festival \
    > /dev/null 2>&1
echo "      ✓ System packages installed"

# ── Python dependencies ───────────────────────────────────────────────────────
echo "[3/5] Installing Python packages..."
pip install --quiet pyaudio pynput openai-whisper torch transformers protobuf
echo "      ✓ Python packages installed"

# ── Configure festival to use PulseAudio ─────────────────────────────────────
echo "[4/5] Configuring festival TTS..."
cat > ~/.festivalrc << 'EOF'
(Parameter.set 'Audio_Command "pacat --server=unix:/mnt/wslg/PulseServer --playback --rate=16000 --format=s16le --channels=1 $FILE")
(Parameter.set 'Audio_Method 'Audio_Command)
EOF
echo "      ✓ ~/.festivalrc written"

# ── Verify audio ──────────────────────────────────────────────────────────────
echo "[5/5] Verifying audio..."

if ! pactl --server=unix:/mnt/wslg/PulseServer info > /dev/null 2>&1; then
    echo ""
    echo "  ✗ WSLg PulseAudio is not running."
    echo "    Fix: Run 'wsl --shutdown' in Windows PowerShell, reopen WSL, then re-run this script."
    exit 1
fi
echo "      ✓ PulseAudio socket reachable"

# Check pyaudio can see the pulse device
DEVICE=$(python3 -c "
import os, pyaudio
os.environ['PULSE_SERVER'] = 'unix:/mnt/wslg/PulseServer'
p = pyaudio.PyAudio()
found = False
for i in range(p.get_device_count()):
    d = p.get_device_info_by_index(i)
    if d['name'] == 'pulse' and d['maxInputChannels'] > 0:
        print(f\"Device {i}: {d['name']} ({int(d['maxInputChannels'])} input channels)\")
        found = True
        break
p.terminate()
if not found:
    exit(1)
" 2>/dev/null)

if [ $? -ne 0 ]; then
    echo ""
    echo "  ✗ PulseAudio device not visible to pyaudio."
    echo "    Try: pip uninstall pyaudio -y && pip install pyaudio --no-binary pyaudio --no-cache-dir"
    exit 1
fi
echo "      ✓ $DEVICE"

# Test TTS
echo "      Testing TTS output..."
PULSE_SERVER=unix:/mnt/wslg/PulseServer bash -c 'echo "Race engineer ready." | festival --tts' 2>/dev/null
echo "      ✓ TTS test complete (you should have heard audio)"

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║               Setup complete! ✓                     ║"
echo "║                                                      ║"
echo "║  Notes:                                              ║"
echo "║  - Set difficulty to PRO for tyre wear/temp data     ║"
echo "║  - If audio drops, run 'wsl --shutdown' in Windows   ║"
echo "║    PowerShell and restart WSL                        ║"
echo "║  - Hold R in-game to ask the race engineer           ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""