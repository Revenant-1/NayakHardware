# NayakHardware

NayakHardware is an ESP32-based voice assistant that listens to user questions, transcribes speech locally, sends the text to a legal AI backend, converts the response into speech, and plays it back through a speaker connected to the ESP32.

This project is designed for offline speech recognition on the machine running the Python pipeline, while the actual legal guidance is generated via a cloud LLM (Gemini) and then spoken aloud using Piper voice models.

It is built around a compact embedded hardware loop:

- Record audio using an ESP32 + INMP441 microphone
- Stream that recording to a PC over serial
- Transcribe speech locally using faster-whisper
- Ask a legal assistant backend for an answer
- Clean and speak the response using Piper TTS
- Send the generated audio back to the ESP32 for playback

This repository includes both the firmware for the ESP32 board and the Python pipeline used to orchestrate the complete interaction.

## Features

- ESP32-based microphone recording and speaker playback
- OLED status display on the hardware device
- Serial protocol for recording and reply streaming
- Local offline speech-to-text with faster-whisper
- Legal assistant behavior using Gemini
- Hindi and English voice support through Piper models
- Automatic speech cleanup before TTS
- Repeat function to replay the last spoken answer
- Designed for low-latency, single-device conversational flow

## Hardware Overview

The hardware side is implemented in `esp32.ino` and uses:

- ESP32 development board
- INMP441 digital microphone
- MAX98357A I2S amplifier / speaker module
- SH1106 OLED display
- 2 push buttons:
  - Record / Stop button
  - Repeat button

The firmware uses a single I2S peripheral and quickly switches between microphone mode and speaker mode so the ESP32 can do both recording and playback without needing a second I2S port.

## Software Architecture

The Python side is orchestrated by `pipeline.py` and follows this flow:

1. ESP32 records a user voice command.
2. The recording is transmitted to the PC over UART/serial.
3. `recorder.py` processes the incoming recorded stream.
4. `stt.py` transcribes the WAV file using faster-whisper.
5. `backendClient.py` sends the recognized text to Gemini.
6. `tts.py` cleans the answer and converts it to speech using Piper.
7. `speaker.py` streams the generated audio back to the ESP32.
8. The ESP32 plays the audio through the MAX98357A amplifier.

## Repository Structure

```text
NayakHardware/
├── backendClient.py         # Gemini API client and system prompt
├── esp32.ino                # ESP32 firmware for mic, speaker, OLED, serial flow
├── pipeline.py              # Main orchestration loop for the full pipeline
├── recorder.py              # Audio capture and WAV conversion logic
├── stt.py                  # Offline speech-to-text using faster-whisper
├── tts.py                  # Text cleanup + Piper-based TTS
├── speaker.py               # Serial audio streaming to ESP32 speaker
├── requirements.txt        # Python dependencies
├── output.wav              # Example generated output audio
├── recording.wav          # Sample recorded input audio
├── __pycache__/            # Python cache files
├── displayTest/            # Hardware display test sketches and assets
├── micTest/                # Microphone validation scripts and sample audio
├── speaker/                 # Speaker testing sketches and sample waveform files
├── voices/                 # Piper voice model files for English and Hindi
└── .gitignore
```

## Main Components

### 1. `pipeline.py`
This is the main runtime file that ties together the entire assistant flow.

It:

- Opens the serial port to the ESP32
- Waits for recorded audio from the board
- Runs `recorder.receive_recording()` and `raw_int32_to_wav()`
- Calls `stt.transcribe()`
- Sends the text to `backendClient.ask()`
- Converts the answer to sound with `tts.create_tts()`
- Streams the result back to `speaker.send_reply()`

### 2. `backendClient.py`
This module connects the project to the Gemini API and sends user queries to the legal assistant backend.

The system prompt is tuned to a legal assistant role and instructs the model to:

- answer in plain language
- avoid making up legal authorities
- clearly differentiate legal information from legal advice
- ask for jurisdiction when needed
- stay concise and practical

### 3. `stt.py`
This module uses `faster-whisper` for offline transcription.

It loads a Whisper model and transforms raw recorded WAV audio into text. The project is configured for CPU inference and uses a lightweight model size to maintain responsiveness.

### 4. `tts.py`
This is one of the most important modules in the repo. It converts backend output into spoken text and prepares it for the Piper speech engine.

It includes functions to:

- strip markdown
- remove citation metadata from model output
- handle numbers and decimals cleanly
- replace symbols like `%`, `&`, `/`, `+` with spoken equivalents
- clean Unicode whitespace and punctuation
- detect Hindi vs English text
- select the correct voice model
- output a final WAV file suitable for playback

### 5. `speaker.py`
This module handles the return audio path from the PC to the ESP32.

It:

- reads the generated WAV file
- validates the sampling format
- streams chunks over serial
- uses `FREE:<bytes>` flow-control messages from the ESP32 to pace transmission
- keeps playback in real time without overflowing the ESP32's buffer

### 6. `esp32.ino`
This is the embedded firmware running on the ESP32.

It manages:

- microphone capture via I2S
- data transmission to the PC
- speaker playback from incoming audio
- OLED text updates
- button-driven recording and repeat flow
- ring buffer management for streaming audio

## Dependencies

Python dependencies are listed in `requirements.txt`:

```bash
pip install -r requirements.txt
```

Current dependencies:

- requests
- faster-whisper
- pyserial

You will also need the `piper` executable available on your system path for TTS generation.

## Required Tools

### Python
Install Python 3.10+.

### Piper TTS
The project expects the Piper CLI tool to be installed and available in your environment.

Example:

```bash
# Linux/macOS example
piper --help
```

If Piper is not installed, TTS generation will fail.

### Arduino / ESP32 Toolchain
To compile and upload the firmware in `esp32.ino`, use:

- Arduino IDE or PlatformIO
- ESP32 board package
- U8g2 library for the OLED display

Board configuration should match the actual ESP32 board in use.

## Setup Instructions

### 1. Clone the repository

```bash
git clone https://github.com/Revenant-1/NayakHardware.git
cd NayakHardware
```

### 2. Install Python dependencies

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### 3. Install Piper TTS
Follow the installation instructions for your platform and ensure the `piper` binary is available in PATH.

The repo already includes voice files under `voices/`:

- `en_US-lessac-medium.onnx`
- `hi_IN-pratham-medium.onnx`

These are used by the TTS layer.

### 4. Configure the serial port
Open `pipeline.py` and check the following section:

```python
PORT = "/dev/ttyUSB0"
BAUD = 921600
```

Change the serial port to the correct path for your machine and board.

Typical values:

- Linux: `/dev/ttyUSB0` or `/dev/ttyACM0`
- macOS: `/dev/cu.usbserial-*`
- Windows: COM port number

### 5. Configure the Google Gemini key
The project uses a Gemini client in `backendClient.py`.

Important: do not hardcode secrets in version-controlled source code. Prefer environment variables.

Example:

```bash
export GEMINI_API_KEY="your_api_key_here"
```

Then modify `backendClient.py` to read the value from the environment instead of embedding it directly.

### 6. Upload firmware to the ESP32
Open `esp32.ino` in Arduino IDE or PlatformIO, install the required libraries, compile it, and upload it to the board.

Make sure the GPIO wiring matches the board definitions in the source:

- `BUTTON_PIN 4`
- `REPEAT_BUTTON_PIN 18`
- `OLED_SDA 21`
- `OLED_SCL 22`
- `MIC_BCK_PIN 26`
- `MIC_WS_PIN 25`
- `MIC_SD_PIN 32`
- `SPK_BCK_PIN 13`
- `SPK_WS_PIN 33`
- `SPK_DOUT_PIN 27`

## Running the System

Once the firmware is flashed and the Python environment is ready, run:

```bash
python pipeline.py
```

The program will wait for the ESP32 to send recorded audio and then continue through the entire voice pipeline.

### Basic usage flow

1. Press the record button on the ESP32.
2. Speak your question or command.
3. Press the button again to stop recording.
4. The PC converts the audio to text.
5. The legal assistant replies.
6. The answer is spoken via the speaker.
7. The OLED screen shows status messages like "Listening...", "Thinking...", and "Speaking...".

### Repeat mode

Press the repeat button while idle to ask the PC to resend and replay the last answer without recording a new question.

## How the Audio Protocol Works

The project uses a compact custom serial protocol between the ESP32 and the PC.

### Recording path

When the user presses the record button, the ESP32 enters recording mode and emits audio chunks with headers such as:

- `AUD0`
- `CHNK`
- `AUD1`

Data is sent in binary form and reassembled on the Python side by `recorder.py`.

### Reply playback path

After STT and backend processing, the Python layer sends a generated WAV file back to the ESP32. The ESP32 fills a ring buffer and plays the audio while monitoring flow control updates from the PC.

## Notes on Speech and Language

This project supports both English and Hindi speech output:

- English uses `en_US-lessac-medium.onnx`
- Hindi uses `hi_IN-pratham-medium.onnx`

The TTS layer detects Devanagari text and selects the Hindi voice automatically. Otherwise it defaults to English.

## Safety and Legal Disclaimer

This project is intended for educational and prototyping use.

It is not a substitute for professional legal advice. The assistant is designed to provide general legal information, but legal rights, processes, and requirements vary by jurisdiction and case.

Users should verify important legal conclusions with a qualified lawyer or legal aid service in the relevant jurisdiction.

## Troubleshooting

### No serial connection
- Verify that the ESP32 is connected to the correct USB port
- Check the serial port in `pipeline.py`
- Ensure the board is flashed and powered correctly

### `piper` command not found
- Install Piper TTS and make sure the binary is in your PATH
- Confirm the `voices/` directory exists and contains the model files

### Microphone not recording
- Check the INMP441 wiring
- Verify the I2S pins match the board configuration
- Test the microphone separately using the files in `micTest/`

### Speaker not outputting audio
- Check the MAX98357A wiring
- Confirm the amplifier is powered correctly
- Inspect the serial flow and speaker test scripts in `speaker/`

### Gemini request fails
- Confirm the API key is configured correctly
- Check quota and API access
- Review the network and request configuration in `backendClient.py`

## Testing and Validation Assets

This repo includes test scripts and sample files for validating key components:

- `displayTest/` for display behavior
- `micTest/` for microphone and audio validation
- `speaker/` for speaker playback checks

These helper files are useful for debugging hardware issues before integrating the full system.

## Future Improvements

Possible enhancements for this project include:

- moving secrets to environment variables or a secure config file
- adding a startup calibration routine for better mic volume
- adding proper exception handling and logging
- supporting more languages and voice options
- packaging the Python code into a cleaner CLI tool
- adding a non-volatile configuration file for board and device settings
- improving legal answer validation and source transparency

## License

This repository does not currently include an explicit license file. If you plan to reuse or distribute the project, add a license before publication.

## Project Summary

NayakHardware is a hardware-enabled legal voice assistant built around an ESP32 controller and a Python-based speech pipeline. It combines embedded audio capture, local transcription, AI legal answer generation, and speech synthesis in a single interactive system aimed at practical, real-time assistance.

It is well suited for prototyping conversational legal support devices, smart kiosks, or accessible voice interfaces in Hindi and English.

---

If you want, I can also generate a shorter, more polished README for GitHub landing pages, or tailor it for a specific audience such as students, investors, or hardware makers.
