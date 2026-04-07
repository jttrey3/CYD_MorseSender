// morse.h — Morse code engine for CYD Morse Sender
// Phase 5: lookup table + Paris timing + sequence builder

#pragma once
#include <stdint.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// TX output pin
// Phase 5/6/7: Red channel of CYD common-anode RGB LED (LOW = on).
// Future: swap MORSE_TX_PIN to the optocoupler relay GPIO, keep polarity.
// ---------------------------------------------------------------------------
#define MORSE_TX_PIN    4       // GPIO4 = Red LED  (later: relay GPIO)
#define MORSE_TX_ON     LOW     // Common-anode LED: LOW illuminates
#define MORSE_TX_OFF    HIGH

// ---------------------------------------------------------------------------
// Maximum sequence buffer size (uint16_t intervals, alternating mark/space).
// Worst case: 64-char message, all '0' ("-----") = 639 intervals.
// ---------------------------------------------------------------------------
#define MORSE_BUF_SIZE  768

// ---------------------------------------------------------------------------
// Lookup tables
// ---------------------------------------------------------------------------

// A–Z  (index 0 = 'A')
static const char * const _MORSE_ALPHA[26] = {
    ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....",  // A-H
    "..",   ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.",  // I-P
    "--.-", ".-.",  "...",  "-",    "..-",  "...-", ".--",  "-..-",  // Q-X
    "-.--", "--.."                                                   // Y-Z
};

// 0–9  (index 0 = '0')
static const char * const _MORSE_DIGIT[10] = {
    "-----", ".----", "..---", "...--", "....-",
    ".....", "-....", "--...", "---..", "----."
};

// Punctuation
static const struct { char ch; const char *code; } _MORSE_PUNCT[] = {
    { '.',  ".-.-.-" },
    { ',',  "--..--" },
    { '?',  "..--.." },
    { '\'', ".----." },
    { '!',  "-.-.--" },
    { '/',  "-..-."  },
    { '(',  "-.--."  },
    { ')',  "-.--.-" },
    { '&',  ".-..."  },
    { ':',  "---..." },
    { ';',  "-.-.-." },
    { '=',  "-...-"  },
    { '+',  ".-.-."  },
    { '-',  "-....-" },
    { '_',  "..--.-" },
    { '"',  ".-..-." },
    { '@',  ".--.-." },
    { '\0', NULL      }   // sentinel
};

// Return the dot/dash string for a character, or NULL if unsupported.
static const char *morseForChar(char c)
{
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return _MORSE_ALPHA[c - 'A'];
    if (c >= '0' && c <= '9') return _MORSE_DIGIT[c - '0'];
    for (int i = 0; _MORSE_PUNCT[i].ch != '\0'; i++) {
        if (_MORSE_PUNCT[i].ch == c) return _MORSE_PUNCT[i].code;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// buildMorseSequence()
//
// Converts a text string into an alternating mark/space duration array.
//   buf[0]  = first mark  duration (ms)
//   buf[1]  = first space duration (ms)
//   buf[2]  = second mark duration (ms)
//   ...
// The array always begins and ends with a mark.
//
// Paris timing at N WPM:  unit = 1200 / N  ms
//   dit             = 1 unit
//   dah             = 3 units
//   inter-element   = 1 unit  (between dits/dahs within a character)
//   inter-character = 3 units (between letters)
//   inter-word      = 7 units (space character)
//
// Returns: number of intervals written (>0), or -1 if bufCap is too small.
// Unknown characters are silently skipped.
// ---------------------------------------------------------------------------
static int buildMorseSequence(const char *text, int wpm,
                               uint16_t *buf, int bufCap)
{
    if (!text || !buf || bufCap <= 0 || wpm <= 0) return -1;

    const uint16_t unit = (uint16_t)(1200 / wpm);
    const uint16_t dit  = unit;
    const uint16_t dah  = (uint16_t)(unit * 3);
    const uint16_t ieg  = unit;            // inter-element gap
    const uint16_t icg  = (uint16_t)(unit * 3);   // inter-character gap
    const uint16_t iwg  = (uint16_t)(unit * 7);   // inter-word gap

    int n = 0;
    bool lastWasChar = false;
    uint16_t pendingGap = 0;    // gap to insert before the next character

#define _PUSH(v)  do { if (n >= bufCap) return -1; buf[n++] = (v); } while(0)

    for (int ti = 0; text[ti] != '\0'; ti++) {
        char ch = text[ti];

        if (ch == ' ') {
            // Promote the pending inter-character gap to an inter-word gap.
            // Do NOT touch already-written marks — the gap is always pushed
            // as a space (odd index) immediately before the next character.
            if (lastWasChar) {
                pendingGap = iwg;
            }
            lastWasChar = false;
            continue;
        }

        const char *code = morseForChar(ch);
        if (!code) continue;    // unsupported character — skip

        // Push the inter-character (or inter-word) gap before this character.
        // pendingGap is set by a space; check it independently of lastWasChar
        // because the space handler clears lastWasChar.
        if (pendingGap > 0) {
            _PUSH(pendingGap);
            pendingGap = 0;
        } else if (lastWasChar) {
            _PUSH(icg);
        }

        for (int ci = 0; code[ci] != '\0'; ci++) {
            if (ci > 0) {
                _PUSH(ieg);     // inter-element gap between elements
            }
            _PUSH(code[ci] == '-' ? dah : dit);
        }

        lastWasChar = true;
    }

#undef _PUSH
    return n;
}
