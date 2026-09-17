"""
Gemini client for Nayak Legal Assistant.

STT is still done locally by stt.py.
This module takes the recognized text and sends it directly to Gemini.
"""

from google import genai

# =========================
# CONFIG
# =========================

GEMINI_API_KEY = "AIzaSyCJufq2qSfoluKRySS0eyHe6aFPbdbb8zc"

MODEL = "gemini-3.5-flash"

SYSTEM_PROMPT = """
You are Nayak, a legal assistant.

Your job is to help the user understand legal issues clearly and accurately.

Rules:
- Give concise, practical answers.
- Explain legal concepts in plain language.
- Clearly distinguish general legal information from legal advice.
- If jurisdiction matters, ask for or identify the relevant jurisdiction.
- Do not invent laws, cases, sections, or legal authorities.
- If you are uncertain, say so.
- When appropriate, structure answers with bullets or numbered steps.
- Since your answers may be spoken aloud, avoid unnecessary formatting.
"""

# =========================
# GEMINI CLIENT
# =========================

client = genai.Client(api_key=GEMINI_API_KEY)

# Conversation history
history = []


def ask(text):
    """
    Sends recognized STT text to Gemini and returns the response.

    Example:
        response = ask("What is the limitation period for a civil suit?")
    """

    global history

    # Add user's message
    history.append({
        "role": "user",
        "parts": [{"text": text}]
    })

    response = client.models.generate_content(
        model=MODEL,
        contents=history,
        config=genai.types.GenerateContentConfig(
            system_instruction=SYSTEM_PROMPT,
            temperature=0.3,
            max_output_tokens=1000,
        ),
    )

    answer = response.text

    # Add Gemini's response to conversation history
    history.append({
        "role": "model",
        "parts": [{"text": answer}]
    })

    return answer
