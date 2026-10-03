#pragma once
#include <stdint.h>
#include "config/storage.h"

// =============================================================================
// Scene Codes -- a short code for the live sound, to enter on another table
// (or this one, on later firmware) and get the sound back. Spec:
// notes/scene_code.md.
//
// The code holds built-ins only: each layer's root, octave, scale, voice,
// shift, Low Note and Wrap, Layer B's Same as A flags, both modes and Layer
// Turns. Every field is stored relative to its default for the code's
// version, so a field at its default is zero bits, and trailing zero
// characters are dropped: a code changing only the roots is 3 characters.
// =============================================================================

constexpr uint8_t SCENE_CODE_MAX = 10;   // characters

// The 32 code characters, most distinct first. A character's position is
// its 5-bit value.
extern const char SCENE_CODE_ALPHABET[33];

// The live sound as a code, `out` at least SCENE_CODE_MAX + 1. A custom or
// scene voice goes out as the built-in it was made from, a Learned or Custom
// scale as Major.
void sceneCodeEncode(const SavedConfig& cfg, char* out);

// The scene a code holds, or false if it is not a valid code. Fields outside
// the code take the values its version pins; fields newer than the version
// take the factory values.
bool sceneCodeDecode(const char* code, Scene& out);
