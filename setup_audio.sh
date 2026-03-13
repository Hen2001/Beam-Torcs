#!/bin/bash
# setup_audio.sh — Run once after cloning the repo before first use.
# Sets up WSLg audio, installs dependencies, and verifies mic access.

set -e

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║    TORCS Race Engineer — Audio Setup     ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# ── ALSA → PulseAudio bridge ──────────────────────────────────────────────────
echo "[1/4] Configuring ALSA to route through PulseAudio..."
mkdir -p ~/.config/alsa
cat > ~/.asoundrc << 'EOF'
pcm.default pulse
ctl.default pulse
pcm.pulse { type pulse }
ctl.pulse { type pulse }
EOF
echo "      ✓ ~/.asoundrc written"

# ── Persist PULSE_SERVER ──────────────────────────────────────────────────────
echo "[2/4] Persisting PULSE_SERVER environment variable..."
if ! grep -q "PULSE_SERVER" ~/.bashrc; then
    echo 'export PULSE_SERVER=unix:/mnt/wslg/PulseServer' >> ~/.bashrc
    echo "      ✓ Added to ~/.bashrc"
else
    echo "      ✓ Already set in ~/.bashrc"
fi
export PULSE_SERVER=unix:/mnt/wslg/PulseServer

# ── System dependencies ───────────────────────────────────────────────────────
echo "[3/4] Installing system dependencies..."
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
echo "      Installing Python packages..."
pip install --quiet pyaudio pynput openai-whisper torch transformers
echo "      ✓ Python packages installed"

# ── Verify audio ──────────────────────────────────────────────────────────────
echo "[4/4] Verifying microphone access..."

if ! pactl --server=unix:/mnt/wslg/PulseServer info > /dev/null 2>&1; then
    echo ""
    echo "  ✗ WSLg PulseAudio is not running."
    echo "    This usually means WSLg hasn't initialised audio yet."
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

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║           Setup complete! ✓              ║"
echo "║   Start TORCS and enable the engineer.   ║"
echo "╚══════════════════════════════════════════╝"
echo ""