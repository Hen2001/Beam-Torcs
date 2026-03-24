# Package installs and preperations needed for AI features

## Package Installs -

### Granite Analysis/Coaching/Commentator (analyse.py/liveCoach.py/liveComs.py)

*virtual environment setup*

sudo apt install python3.12-venv

python3 -m venv .venv

*ensure you are in a virtual environment before continuing*

pip install transformers torch accelerate

pip install --upgrade transformers 

pip install protobuf - 

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

*You can now configure and start your race, if you hold 'r', you can now ask the race engineer questions about your car's state.*

