"""
End-to-end voice assistant loop:

    ESP32 button press
        -> record (recorder.py)
        -> speech-to-text (stt.py, faster-whisper, offline)
        -> text-to-speech (tts.py, Piper)
        -> stream reply back to ESP32 speaker (speaker.py)
        -> repeat

Run:
    python pipeline.py
"""

import serial
import time

import recorder
import stt
import tts
import speaker
import backendClient

PORT = "/dev/ttyUSB0"       # change to your ESP32's serial port
BAUD = 921600

RECORDED_WAV = "recording.wav"
REPLY_WAV = "output.wav"


def main():
    print(f"Opening {PORT} at {BAUD} baud...")
    ser = serial.Serial(PORT, BAUD, timeout=0.2)

    time.sleep(2)  # let the ESP32 finish resetting/booting
    ser.reset_input_buffer()

    print("Connected. Waiting for button presses on the ESP32...")

    try:
        while True:
            # 1) Wait for a full recording (AUD0 ... AUD1)
            print("\nWaiting for a recording...")
            raw_audio = recorder.receive_recording(ser)
            print(f"[Recorder] Received {len(raw_audio)} bytes.")

            recorder.raw_int32_to_wav(raw_audio, RECORDED_WAV)

            # 2) Speech-to-text
            print("[STT] Transcribing...")
            text, lang = stt.transcribe(RECORDED_WAV)
            print(f"[STT] Heard ({lang}): {text!r}")

            if not text.strip():
                print("Nothing recognized — listening again.")
                continue
            text, lang = stt.transcribe(RECORDED_WAV)

            answer = backendClient.ask(text)
            print(f"[Backend] Answer: {answer!r}")

            if not answer.strip():
                print("No answer — listening again.")
                continue

            # 3) Text-to-speech
            reply_path = tts.create_tts(answer, output_file=REPLY_WAV)
            if not reply_path:
                print("TTS failed — listening again.")
                continue

            # 4) Stream the reply back to the ESP32 speaker
            speaker.send_wav(ser, reply_path)

    except KeyboardInterrupt:
        print("\nStopping.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
