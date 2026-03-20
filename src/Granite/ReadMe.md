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

bash setup_audio.sh

*This will install all packages and configurations for you, You should hear the race engineer say "Race Engineer Ready", this will confirm everything is working correctly*

make

sudo make install

torcs

*you can now configure and start your race, if you press 'r', you will be prompted for microphone access in your browser, click allow & now you can hold 'r' to talk to your race_engineer!*

