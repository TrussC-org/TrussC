// =============================================================================
// tcSound モジュール実装
// stb_vorbis, dr_wav, dr_mp3 の実装をここに含める
// =============================================================================

// stb_vorbis - OGG Vorbis デコーダ
extern "C" {
#include "stb_vorbis.c"
}

// dr_wav - WAV デコーダ
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

// dr_mp3 - MP3 デコーダ
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
