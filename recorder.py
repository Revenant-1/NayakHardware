"""
Receives a recording from the ESP32 over serial.

Protocol (matches firmware/esp32_combined.ino):

    AUD0
      CHNK <uint16 length> <audio bytes>
      CHNK <uint16 length> <audio bytes>
      ...
    AUD1 <uint32 total_audio_bytes>

Audio is raw int32 PCM at 16 kHz mono (INMP441 native format).
"""

import struct
import time
import wave

SAMPLE_RATE = 16000


def _read_exactly(ser, count, timeout=10.0):
    """
    Reads exactly `count` bytes, tolerating the serial port's own
    short read-timeout (0.2s) by retrying until `timeout` seconds
    have passed with no new data at all — rather than raising on
    the very first empty read.
    """
    data = bytearray()
    deadline = time.time() + timeout

    while len(data) < count:
        chunk = ser.read(count - len(data))
        if chunk:
            data.extend(chunk)
            deadline = time.time() + timeout  # progress resets the clock
        elif time.time() > deadline:
            raise RuntimeError(
                f"Serial timeout while waiting for {count} bytes "
                f"(got {len(data)})."
            )

    return bytes(data)


def _find_magic(ser, magic):
    buf = bytearray()
    while True:
        b = ser.read(1)
        if not b:
            continue
        buf.append(b[0])
        if len(buf) > len(magic):
            buf.pop(0)
        if bytes(buf) == magic:
            return


def receive_recording(ser, on_progress=None):
    """
    Blocks until it sees the AUD0 marker (i.e. until the button is
    pressed on the ESP32), then reads CHNK-framed audio until AUD1.

    Returns the raw bytes (int32 PCM, 16 kHz, mono).
    """
    _find_magic(ser, b"AUD0")

    raw = bytearray()

    while True:
        header = _read_exactly(ser, 4)

        if header == b"CHNK":
            length = struct.unpack("<H", _read_exactly(ser, 2))[0]
            if length == 0:
                raise RuntimeError("Received zero-length audio chunk.")
            raw.extend(_read_exactly(ser, length))
            if on_progress:
                on_progress(len(raw))

        elif header == b"AUD1":
            final_length = struct.unpack("<I", _read_exactly(ser, 4))[0]
            if final_length != len(raw):
                raise RuntimeError(
                    f"Final audio length mismatch: firmware said "
                    f"{final_length}, received {len(raw)}."
                )
            break

        else:
            raise RuntimeError(f"Unknown packet header: {header!r}")

    return bytes(raw)


def raw_int32_to_wav(raw_bytes, wav_path, headroom=0.9):
    """
    Converts raw int32 PCM samples (INMP441's native output) into a
    normal 16-bit mono WAV file.

    Instead of guessing a fixed bit-shift (which depends on the mic's
    actual gain), this scales by the recording's own peak sample so
    it always uses the full 16-bit range without clipping.
    """
    if len(raw_bytes) % 4 != 0:
        raise RuntimeError("Raw audio is not aligned to 32-bit samples.")

    samples = struct.unpack("<" + "i" * (len(raw_bytes) // 4), raw_bytes)

    if not samples:
        raise RuntimeError("No audio samples received.")

    peak = max(1, max(abs(s) for s in samples))
    scale = (headroom * 32767) / peak

    converted = bytearray()
    for s in samples:
        v = int(s * scale)
        v = max(-32768, min(32767, v))
        converted.extend(struct.pack("<h", v))

    with wave.open(wav_path, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(converted)

    return wav_path