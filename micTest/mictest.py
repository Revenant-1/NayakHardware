import serial
import struct
import wave
import time
import math
import os


# =====================================================
# CONFIGURATION
# =====================================================

SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE = 921600

SAMPLE_RATE = 16000

BYTES_PER_SAMPLE = 4


# =====================================================
# OUTPUT FILES
# =====================================================

RAW_FILE = "inmp441_raw_int32.bin"

WAV_SHIFT_8 = "recording_shift8.wav"
WAV_SHIFT_12 = "recording_shift12.wav"
WAV_SHIFT_14 = "recording_shift14.wav"
WAV_SHIFT_16 = "recording_shift16.wav"


# =====================================================
# SERIAL
# =====================================================

print(f"Opening {SERIAL_PORT}...")

ser = serial.Serial(
    SERIAL_PORT,
    BAUD_RATE,
    timeout=5
)


# ESP32 may reset when serial opens.
time.sleep(2)


print("Connected.")
print()
print("Press the ESP32 button to START recording.")
print("Press it again to STOP recording.")
print()


# =====================================================
# READ EXACT NUMBER OF BYTES
# =====================================================

def read_exactly(count):

    data = bytearray()


    while len(data) < count:

        chunk = ser.read(
            count - len(data)
        )


        if not chunk:

            raise RuntimeError(
                f"Serial timeout while waiting for "
                f"{count} bytes."
            )


        data.extend(chunk)


    return bytes(data)


# =====================================================
# FIND MAGIC HEADER
# =====================================================

def find_magic(magic):

    buffer = bytearray()


    while True:

        byte = ser.read(1)


        if not byte:

            continue


        buffer.append(byte[0])


        if len(buffer) > len(magic):

            buffer.pop(0)


        if bytes(buffer) == magic:

            return


# =====================================================
# WAIT FOR RECORDING
# =====================================================

print("Waiting for AUD0...")


find_magic(b"AUD0")


print()
print("==============================")
print(" RECORDING DETECTED")
print("==============================")
print()


# =====================================================
# RECEIVE RECORDING
# =====================================================

total_bytes = 0

start_time = time.time()


with open(
    RAW_FILE,
    "wb"
) as raw_file:

    recording = True


    while recording:

        # -------------------------------------------------
        # Every message begins with one of:
        #
        # CHNK
        # AUD1
        # -------------------------------------------------

        header = read_exactly(4)


        # =================================================
        # AUDIO CHUNK
        # =================================================

        if header == b"CHNK":

            length_bytes = read_exactly(2)


            chunk_length = struct.unpack(
                "<H",
                length_bytes
            )[0]


            if chunk_length == 0:

                raise RuntimeError(
                    "Received zero-length audio chunk."
                )


            audio_chunk = read_exactly(
                chunk_length
            )


            raw_file.write(
                audio_chunk
            )


            total_bytes += chunk_length


            elapsed = (
                time.time() -
                start_time
            )


            if elapsed > 0:

                kb = total_bytes / 1024

                rate = (
                    total_bytes /
                    elapsed
                ) / 1024

                print(
                    f"\rReceived: "
                    f"{kb:9.1f} KB | "
                    f"{rate:6.1f} KB/s",
                    end="",
                    flush=True
                )


        # =================================================
        # RECORDING FINISHED
        # =================================================

        elif header == b"AUD1":

            final_length_bytes = read_exactly(4)


            final_length = struct.unpack(
                "<I",
                final_length_bytes
            )[0]


            if final_length != total_bytes:

                raise RuntimeError(
                    "Final audio length does not match "
                    "received audio length."
                )


            recording = False


        # =================================================
        # UNKNOWN HEADER
        # =================================================

        else:

            raise RuntimeError(
                f"Unknown packet header: {header!r}"
            )


# =====================================================
# TRANSFER COMPLETE
# =====================================================

elapsed = time.time() - start_time


print()
print()
print("==============================")
print(" RECORDING COMPLETE")
print("==============================")

print(
    f"Audio bytes: {total_bytes}"
)

print(
    f"Transfer time: {elapsed:.2f} seconds"
)


# =====================================================
# AUDIO INFORMATION
# =====================================================

total_samples = (
    total_bytes //
    BYTES_PER_SAMPLE
)


duration = (
    total_samples /
    SAMPLE_RATE
)


print(
    f"Samples:     {total_samples}"
)

print(
    f"Duration:    {duration:.2f} seconds"
)

print(
    f"Duration:    {duration / 60:.2f} minutes"
)


# =====================================================
# LOAD RAW AUDIO
# =====================================================

print()
print("Loading raw samples...")


with open(
    RAW_FILE,
    "rb"
) as f:

    raw_data = f.read()


if len(raw_data) % 4 != 0:

    raise RuntimeError(
        "Raw audio is not aligned to 32-bit samples."
    )


samples = struct.unpack(
    "<" + "i" * (len(raw_data) // 4),
    raw_data
)


# =====================================================
# RAW SAMPLE ANALYSIS
# =====================================================

print()
print("==============================")
print(" RAW SAMPLE ANALYSIS")
print("==============================")

print(
    f"Samples:         {len(samples)}"
)

print(
    f"Minimum:         {min(samples)}"
)

print(
    f"Maximum:         {max(samples)}"
)


average_abs = (
    sum(abs(x) for x in samples)
    /
    len(samples)
)


print(
    f"Average absolute: {average_abs:.2f}"
)


# =====================================================
# CREATE WAV
# =====================================================

def create_wav(
    filename,
    shift
):

    converted = bytearray()


    for sample in samples:

        value = sample >> shift


        # -------------------------------------------------
        # Convert to signed 16-bit range.
        # -------------------------------------------------

        if value > 32767:

            value = 32767


        elif value < -32768:

            value = -32768


        converted.extend(
            struct.pack(
                "<h",
                value
            )
        )


    # -------------------------------------------------
    # Write WAV.
    # -------------------------------------------------

    with wave.open(
        filename,
        "wb"
    ) as wav:

        wav.setnchannels(1)

        wav.setsampwidth(2)

        wav.setframerate(
            SAMPLE_RATE
        )

        wav.writeframes(
            converted
        )


    # -------------------------------------------------
    # Calculate RMS.
    # -------------------------------------------------

    converted_samples = struct.unpack(
        "<" + "h" * (len(converted) // 2),
        converted
    )


    rms = math.sqrt(
        sum(
            x * x
            for x in converted_samples
        )
        /
        len(converted_samples)
    )


    print(
        f"{filename}: "
        f"shift={shift}, "
        f"RMS={rms:.2f}"
    )


# =====================================================
# CREATE WAV VERSIONS
# =====================================================

print()
print("==============================")
print(" CREATING WAV VERSIONS")
print("==============================")


create_wav(
    WAV_SHIFT_8,
    8
)


create_wav(
    WAV_SHIFT_12,
    12
)


create_wav(
    WAV_SHIFT_14,
    14
)


create_wav(
    WAV_SHIFT_16,
    16
)


# =====================================================
# FINISH
# =====================================================

ser.close()


print()
print("==============================")
print(" TEST COMPLETE")
print("==============================")

print()
print("Play these files:")
print()
print(f"  {WAV_SHIFT_8}")
print(f"  {WAV_SHIFT_12}")
print(f"  {WAV_SHIFT_14}")
print(f"  {WAV_SHIFT_16}")
print()
