"""
Streams a 16kHz/16-bit/mono WAV file to the ESP32's speaker ring
buffer, pacing writes using the board's "FREE:<bytes>" flow-control
reports (see firmware/esp32_combined.ino -> statusTask).

Designed to be called with an already-open serial.Serial so the
pipeline can keep one connection open across many record/reply
cycles instead of reopening the port each time.
"""

import wave
import time
import re
import struct

MAX_INFLIGHT_SECONDS = 1.0
BYTES_PER_SECOND = 16000 * 2  # 16kHz * 16-bit mono
CHUNK_FRAMES = 1024
CHUNK_DURATION = CHUNK_FRAMES / 16000.0

FREE_LINE_RE = re.compile(rb"FREE:(\d+)")

# Keep comfortably under the firmware's MAX_DISPLAY_TEXT (250 bytes).
MAX_TEXT_BYTES = 240


def drain_status_lines(ser, last_free):
    """
    Reads any complete lines the ESP32 has already sent, updates
    last_free from the most recent 'FREE:<n>' line, and prints
    anything else (debug/status text) for visibility.
    """
    while ser.in_waiting > 0:
        line = ser.readline()
        if not line:
            break
        match = FREE_LINE_RE.search(line)
        if match:
            last_free = int(match.group(1))
        else:
            text = line.decode(errors="replace").rstrip()
            if text:
                print("[ESP32]", text)
    return last_free


def _truncate_utf8(text, max_bytes):
    """Truncates to at most max_bytes of UTF-8 without splitting a
    multi-byte character in half."""
    encoded = text.encode("utf-8")
    if len(encoded) <= max_bytes:
        return text
    return encoded[:max_bytes].decode("utf-8", errors="ignore")


def send_text(ser, text):
    """
    Sends a TEXT header (magic + 2-byte length + UTF-8 payload) so
    the ESP32 can show what's about to be spoken on its OLED.
    Call this BEFORE send_wav() for the same reply.
    """
    text = text.strip()
    if not text:
        return

    truncated = _truncate_utf8(text, MAX_TEXT_BYTES)
    if truncated != text:
        truncated = _truncate_utf8(text, MAX_TEXT_BYTES - 3) + "..."

    payload = truncated.encode("utf-8")

    ser.write(b"TEXT")
    ser.write(struct.pack("<H", len(payload)))
    ser.write(payload)
    ser.flush()


def send_reply(ser, wav_path, text):
    """Sends the display text, then streams the audio — use this
    for both a fresh reply and a repeat."""
    send_text(ser, text)
    send_wav(ser, wav_path)


def send_wav(ser, wav_path):
    """Streams wav_path to the ESP32 for playback."""
    with wave.open(wav_path, "rb") as wav:
        if wav.getnchannels() != 1:
            raise RuntimeError("WAV must be mono")
        if wav.getsampwidth() != 2:
            raise RuntimeError("WAV must be 16-bit")
        if wav.getframerate() != 16000:
            raise RuntimeError("WAV must be 16000 Hz")

        last_free = None
        print(f"[Speaker] Sending {wav_path} to ESP32...")

        while True:
            data = wav.readframes(CHUNK_FRAMES)
            if not data:
                break

            send_start = time.time()

            # Flow control: wait until the board has room.
            while True:
                last_free = drain_status_lines(ser, last_free)

                if last_free is None:
                    time.sleep(0.02)
                    continue

                if last_free >= len(data):
                    break

                time.sleep(0.005)

            ser.write(data)

            if last_free is not None:
                last_free = max(0, last_free - len(data))

            # Fallback pacing to stay near real-time even if FREE
            # reports are sparse.
            elapsed = time.time() - send_start
            sleep_time = CHUNK_DURATION - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    print("[Speaker] Finished sending.")


if __name__ == "__main__":
    import serial

    PORT = "/dev/ttyUSB0"
    BAUD = 921600

    ser = serial.Serial(PORT, BAUD, timeout=0.05)
    time.sleep(2)
    ser.reset_input_buffer()
    send_wav(ser, "output.wav")
    ser.close()