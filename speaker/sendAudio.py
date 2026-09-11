import serial
import wave
import time
import re

PORT = "/dev/ttyUSB0"
BAUD = 921600
FILE = "output.wav"

# How much audio (in seconds) we're willing to have in flight
# in the ESP32's ring buffer at once. Keep this comfortably
# below the firmware's RING_BUFFER_SIZE (64 KB ~= 2 sec at
# 16kHz/16-bit/mono) to leave headroom.
MAX_INFLIGHT_SECONDS = 1.0

BYTES_PER_SECOND = 16000 * 2  # 16kHz * 16-bit mono
CHUNK_FRAMES = 1024
CHUNK_BYTES = CHUNK_FRAMES * 2  # 16-bit mono => 2 bytes/frame
CHUNK_DURATION = CHUNK_FRAMES / 16000.0

FREE_LINE_RE = re.compile(rb"FREE:(\d+)")


def drain_status_lines(ser, last_free):
    """
    Read any complete lines the ESP32 has sent, update last_free
    from the most recent 'FREE:<n>' line, and print anything else
    (e.g. [WARNING]/[AUDIO] messages) for visibility.
    Non-blocking-ish: only reads what's already waiting.
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
                print(text)
    return last_free


def main():
    print("Opening WAV...")
    with wave.open(FILE, "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()

        print("Channels:", channels)
        print("Sample width:", sample_width * 8, "bit")
        print("Sample rate:", sample_rate)

        if channels != 1:
            raise RuntimeError("WAV must be MONO")
        if sample_width != 2:
            raise RuntimeError("WAV must be 16-bit")
        if sample_rate != 16000:
            raise RuntimeError("WAV must be 16000 Hz")

        print("Opening ESP32...")
        ser = serial.Serial(PORT, BAUD, timeout=0.05)
        time.sleep(2)  # let the board reset and boot

        # Drain boot-up banner / initial messages
        ser.reset_input_buffer()

        last_free = None
        max_inflight_bytes = int(MAX_INFLIGHT_SECONDS * BYTES_PER_SECOND)

        print("Streaming audio...")

        while True:
            data = wav.readframes(CHUNK_FRAMES)
            if not data:
                break

            send_start = time.time()

            # --- Flow control: wait until the ESP32 has room ---
            while True:
                last_free = drain_status_lines(ser, last_free)

                if last_free is None:
                    # Haven't heard from the board yet; give it a
                    # moment rather than blasting data blind.
                    time.sleep(0.02)
                    continue

                if last_free >= len(data):
                    break

                # Not enough room reported yet - wait a bit and
                # re-check (the board reports free space every
                # ~20ms per FREE_REPORT_INTERVAL_MS in firmware).
                time.sleep(0.005)

            ser.write(data)

            # Account for the bytes we just sent so we don't need
            # to wait for a fresh report before sending the next
            # small chunk.
            if last_free is not None:
                last_free = max(0, last_free - len(data))

            # --- Fallback pacing (belt-and-suspenders) ---
            # Keeps us near real-time even if FREE reports are
            # sparse or delayed, without depending on them solely.
            elapsed = time.time() - send_start
            sleep_time = CHUNK_DURATION - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

        print("Finished.")
        ser.close()


if __name__ == "__main__":
    main()