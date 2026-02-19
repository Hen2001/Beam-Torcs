# Package installs and preperations needed for AI features

## Package Installs -

### End-of-Race Granite Lap Analysis & Feedback (analyse.py)

*virtual environment setup*

sudo apt install python3.12-venv

python3 -m venv .venv

*ensure you are in a virtual environment before continuing*

pip install transformers torch accelerate

pip install --upgrade transformers 

make

sudo make install

*You're good to go! Configure your race & driver, complete the race and await feedback on the finish page.*

### AI Race Engineer (race_engineer.py)

*virtual environment setup*

sudo apt install python3.12-venv

python3 -m venv .venv

*ensure you are in a virtual environment before continuing*

pip install flask

sudo apt install ffmpeg

pip install flask-socketio

sudo apt install libcurl4-openssl-dev

pip install openai-whisper pyttsx3 sounddevice scipy

*now copy the path of the "race_engineer.py" file and paste it into the following template command:*

python3 /path/to/src/Granite/race_engineer.py

*e.g. my command is python3 /home/zackc/Beam-Torcs/src/Granite/race_engineer.py*

*once the race_engineer is running, wait for "Whisper" & "Granite" to load, and open http://127.0.0.1:5000 in your browser, then make a new terminal in vscode & run:*

make

sudo make install

torcs

*you can now configure and start your race, if you press 'r', you will be prompted for microphone access in your browser, click allow & now you can hold 'r' to talk to your race_engineer!*

