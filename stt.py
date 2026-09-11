"""
Local, offline speech-to-text using faster-whisper.

Install:
    pip install faster-whisper

The model downloads automatically on first use (needs internet once;
after that it's fully offline).
"""

from faster_whisper import WhisperModel

# "small" is a good CPU speed/accuracy balance for Hindi + English.
# Bump to "medium" for better accuracy if your machine can take the
# extra latency, or drop to "base"/"tiny" for faster replies.
MODEL_SIZE = "small"
DEVICE = "cpu"
COMPUTE_TYPE = "int8"  # fast on CPU; use "float16" if running on GPU

_model = None


def _get_model():
    global _model
    if _model is None:
        print(f"[STT] Loading faster-whisper model '{MODEL_SIZE}'...")
        _model = WhisperModel(MODEL_SIZE, device=DEVICE, compute_type=COMPUTE_TYPE)
    return _model


def transcribe(wav_path, language=None):
    """
    Transcribes a WAV file.

    language: force a language code (e.g. "hi", "en"), or leave None
    to let Whisper auto-detect (handles mixed Hindi/English fine).

    Returns (text, detected_language).
    """
    model = _get_model()

    segments, info = model.transcribe(
        wav_path,
        language=language,
        vad_filter=True,  # trims leading/trailing silence
    )

    text = " ".join(seg.text.strip() for seg in segments).strip()
    return text, info.language


if __name__=="__main__":
    text, languague = transcribe("output.wav", "en")
    print(text)
    print(languague)
