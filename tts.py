"""
Text-to-Speech using Piper.

The TTS layer receives raw backend/LLM text and converts it into
natural speech-friendly text before sending it to Piper.

Features:
- Removes Markdown
- Removes source/citation metadata
- Expands symbols such as %, &, / 
- Converts numbers into spoken forms
- Cleans Unicode whitespace
- Removes unnecessary punctuation
- Automatically selects Hindi or English Piper voice
"""

import subprocess
import os
import wave
import audioop
import re

VOICE_DIR = "voices"

HINDI_VOICE = os.path.join(
    VOICE_DIR,
    "hi_IN-pratham-medium.onnx"
)

ENGLISH_VOICE = os.path.join(
    VOICE_DIR,
    "en_US-lessac-medium.onnx"
)

DEFAULT_OUTPUT_FILE = "output.wav"

TARGET_RATE = 16000
TARGET_CHANNELS = 1
TARGET_WIDTH = 2


# ---------------------------------------------------------
# LANGUAGE DETECTION
# ---------------------------------------------------------

def is_hindi(text):
    """Detect Devanagari text."""
    return any("\u0900" <= char <= "\u097F" for char in text)


# ---------------------------------------------------------
# TTS TEXT CLEANING
# ---------------------------------------------------------

def clean_for_tts(text):
    """
    Convert backend/LLM output into speech-friendly text.

    This text is ONLY for TTS.
    The original backend answer should still be displayed
    normally in the UI.
    """

    if not text:
        return ""

    # -----------------------------------------------------
    # Unicode cleanup
    # -----------------------------------------------------

    # Narrow no-break space / non-breaking space
    text = text.replace("\u202f", " ")
    text = text.replace("\u00a0", " ")

    # Other invisible/control characters
    text = re.sub(r"[\u200b-\u200f\u2060\ufeff]", "", text)

    # -----------------------------------------------------
    # Remove source/citation information
    # -----------------------------------------------------

    # Examples:
    # ( Source: PMFBY Scheme English, Section 10.7 )
    # Source: PMFBY...
    # [Source: ...]
    # Source, PMFBY...

    text = re.sub(
        r"\(?\s*source\s*[:,-].*?\)?(?=\s*$)",
        "",
        text,
        flags=re.IGNORECASE | re.DOTALL
    )

    # Remove common citation blocks
    text = re.sub(
        r"\(?\s*source\s*:.*?\)",
        "",
        text,
        flags=re.IGNORECASE | re.DOTALL
    )

    # -----------------------------------------------------
    # Remove Markdown
    # -----------------------------------------------------

    # Bold
    text = re.sub(r"\*\*(.*?)\*\*", r"\1", text)

    # Italic
    text = re.sub(r"__(.*?)__", r"\1", text)
    text = re.sub(r"\*(.*?)\*", r"\1", text)

    # Markdown headings
    text = re.sub(r"^#{1,6}\s*", "", text, flags=re.MULTILINE)

    # Markdown bullets
    text = re.sub(r"^\s*[-*+]\s*", "", text, flags=re.MULTILINE)

    # Markdown numbered lists
    text = re.sub(r"^\s*\d+\.\s*", "", text, flags=re.MULTILINE)

    # -----------------------------------------------------
    # Symbols → words
    # -----------------------------------------------------

    text = text.replace("&", " and ")

    # Percent
    text = text.replace("%", " percent ")

    # Slash
    text = text.replace("/", " or ")

    # Plus
    text = text.replace("+", " plus ")

    # Equals
    text = text.replace("=", " equals ")

    # -----------------------------------------------------
    # Common legal/financial abbreviations
    # -----------------------------------------------------

    replacements = {
        r"\bPMFBY\b": "PMFBY",
        r"\bPACS\b": "PACS",
        r"\bFIR\b": "FIR",
        r"\bRTI\b": "RTI",
        r"\bOTP\b": "OTP",
        r"\bSMS\b": "SMS",
        r"\bAI\b": "A I",
        r"\bLLM\b": "L L M",
    }

    for pattern, replacement in replacements.items():
        text = re.sub(
            pattern,
            replacement,
            text,
            flags=re.IGNORECASE
        )

    # -----------------------------------------------------
    # Decimal numbers
    #
    # 1.5 -> 1 point 5
    # 2.5 -> 2 point 5
    # -----------------------------------------------------

    def decimal_to_speech(match):
        number = match.group(0)

        if "." in number:
            left, right = number.split(".", 1)
            return f"{left} point {' '.join(right)}"

        return number

    text = re.sub(
        r"\b\d+\.\d+\b",
        decimal_to_speech,
        text
    )

    # -----------------------------------------------------
    # Numbers
    #
    # Keep this conservative.
    # We mainly want percentages to sound natural.
    # -----------------------------------------------------

    number_words = {
        "0": "zero",
        "1": "one",
        "2": "two",
        "3": "three",
        "4": "four",
        "5": "five",
        "6": "six",
        "7": "seven",
        "8": "eight",
        "9": "nine",
        "10": "ten",
        "11": "eleven",
        "12": "twelve",
        "13": "thirteen",
        "14": "fourteen",
        "15": "fifteen",
        "16": "sixteen",
        "17": "seventeen",
        "18": "eighteen",
        "19": "nineteen",
        "20": "twenty",
        "30": "thirty",
        "40": "forty",
        "50": "fifty",
        "60": "sixty",
        "70": "seventy",
        "80": "eighty",
        "90": "ninety",
        "100": "one hundred",
    }

    def number_to_word(match):
        value = match.group(0)

        # Don't aggressively convert large numbers.
        if value in number_words:
            return number_words[value]

        return value

    text = re.sub(
        r"\b\d{1,3}\b",
        number_to_word,
        text
    )

    # -----------------------------------------------------
    # Parentheses
    # -----------------------------------------------------

    text = text.replace("(", " ")
    text = text.replace(")", " ")

    # -----------------------------------------------------
    # Remove leftover technical punctuation
    # -----------------------------------------------------

    text = text.replace("[", " ")
    text = text.replace("]", " ")
    text = text.replace("{", " ")
    text = text.replace("}", " ")

    # Hyphen -> pause
    text = text.replace("-", " ")

    # Colon -> pause
    text = text.replace(":", ". ")

    # Semicolon -> pause
    text = text.replace(";", ". ")

    # -----------------------------------------------------
    # Collapse whitespace
    # -----------------------------------------------------

    text = re.sub(r"\s+", " ", text)

    # -----------------------------------------------------
    # Clean repeated punctuation
    # -----------------------------------------------------

    text = re.sub(r"\.{2,}", ".", text)
    text = re.sub(r",+", ",", text)

    # -----------------------------------------------------
    # Final cleanup
    # -----------------------------------------------------

    return text.strip()


# ---------------------------------------------------------
# PIPER TTS
# ---------------------------------------------------------

def create_tts(text, output_file=DEFAULT_OUTPUT_FILE):

    text = clean_for_tts(text)

    if not text:
        print("[TTS] ERROR: Empty text after cleaning.")
        return None

    if is_hindi(text):
        voice = HINDI_VOICE
        language = "Hindi"
    else:
        voice = ENGLISH_VOICE
        language = "English"

    print(f"[TTS] ({language}) {text!r}")
    print(f"[TTS] Voice: {voice}")

    if not os.path.exists(voice):
        print("[TTS] ERROR: Voice model not found:", voice)
        return None

    temp_file = "piper_temp.wav"

    command = [
        "piper",
        "--model",
        voice,
        "--output_file",
        temp_file
    ]

    try:

        subprocess.run(
            command,
            input=text,
            text=True,
            check=True
        )

    except subprocess.CalledProcessError as e:

        print("[TTS] Piper failed:", e)
        return None

    except FileNotFoundError:

        print("[TTS] ERROR: 'piper' executable not found.")
        return None

    # -----------------------------------------------------
    # Read Piper WAV
    # -----------------------------------------------------

    with wave.open(temp_file, "rb") as wav:

        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()

        frames = wav.readframes(
            wav.getnframes()
        )

    # -----------------------------------------------------
    # Stereo -> mono
    # -----------------------------------------------------

    if channels == 2:

        frames = audioop.tomono(
            frames,
            sample_width,
            0.5,
            0.5
        )

        channels = 1

    elif channels != 1:

        print(
            "[TTS] ERROR: Unsupported channel count:",
            channels
        )

        return None

    # -----------------------------------------------------
    # Resample -> 16kHz
    # -----------------------------------------------------

    if sample_rate != TARGET_RATE:

        frames, _ = audioop.ratecv(
            frames,
            sample_width,
            channels,
            sample_rate,
            TARGET_RATE,
            None
        )

    # -----------------------------------------------------
    # Validate sample width
    # -----------------------------------------------------

    if sample_width != TARGET_WIDTH:

        print(
            "[TTS] ERROR: Piper produced",
            sample_width * 8,
            "bit audio."
        )

        return None

    # -----------------------------------------------------
    # Write final WAV
    # -----------------------------------------------------

    with wave.open(output_file, "wb") as wav:

        wav.setnchannels(TARGET_CHANNELS)
        wav.setsampwidth(TARGET_WIDTH)
        wav.setframerate(TARGET_RATE)

        wav.writeframes(frames)

    if os.path.exists(temp_file):
        os.remove(temp_file)

    print("[TTS] Wrote", output_file)

    return output_file


# ---------------------------------------------------------
# TEST
# ---------------------------------------------------------

if __name__ == "__main__":

    import sys

    text = " ".join(sys.argv[1:])

    if not text:
        text = """
        Kharif food & oilseed crops: **2 % of Sum Insured**
        Rabi food & oilseed crops: **1.5 % of Sum Insured**
        Annual commercial/horticultural crops: **5 % of Sum Insured**

        (Source: PMFBY Scheme English, Section 10.7)
        """

    print("\nOriginal:")
    print(text)

    print("\nTTS text:")
    print(clean_for_tts(text))

    create_tts(text)
