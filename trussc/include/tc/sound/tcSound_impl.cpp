// =============================================================================
// tcSound module implementation
// Include implementations of stb_vorbis, dr_wav, dr_mp3 here
// =============================================================================

// stb_vorbis - OGG Vorbis decoder
extern "C" {
#include "stb_vorbis.c"
}

// dr_wav - WAV decoder
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

// dr_mp3 - MP3 decoder
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
