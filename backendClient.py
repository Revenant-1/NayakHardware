"""
Client for the Nayak Legal Assistant backend (api_server.py).

Only talks to /api/command — audio-to-text is still done locally
by stt.py (faster-whisper). This module just takes the recognized
text, sends it to the backend, and returns the answer to speak.

Install:
    pip install requests
"""

import requests

BASE_URL = "http://127.0.0.1:8000"  # change to wherever api_server.py runs

_token = None
_session_id = None


def _login():
    """Guest login — swap this for /api/auth/login if you want a
    persistent account instead of a fresh guest each run."""
    global _token
    resp = requests.post(f"{BASE_URL}/api/auth/guest-login", timeout=10)
    resp.raise_for_status()
    _token = resp.json()["token"]
    return _token


def _auth_headers():
    if _token is None:
        _login()
    return {"Authorization": f"Bearer {_token}"}


def ask(text):
    """
    Sends recognized text to /api/command and returns the assistant's
    answer text. Keeps the same session_id across calls so the
    backend has conversational context.
    """
    global _session_id
    headers = _auth_headers()

    resp = requests.post(
        f"{BASE_URL}/api/command",
        headers=headers,
        json={"text": text, "session_id": _session_id},
        timeout=60,
    )

    if resp.status_code == 401:
        _login()
        return ask(text)

    resp.raise_for_status()
    result = resp.json()
    _session_id = result.get("session_id", _session_id)
    return result["response"]
