#pragma once

// =============================================================================
// TrussC Sound
// sokol_audio + stb_vorbis ベースのサウンド再生
//
// 設計:
// - AudioEngine: シングルトン、sokol_audio の初期化、ミキサー管理
// - SoundBuffer: デコード済みサウンドデータ（共有可能）
// - Sound: ユーザー向けクラス、再生制御
//
// 使用例:
//   tc::Sound sound;
//   sound.load("music.ogg");
//   sound.play();
//   sound.setVolume(0.8f);
//   sound.setPan(-0.5f);   // 左寄り
//   sound.setSpeed(1.5f);  // 1.5倍速
//   sound.setLoop(true);
// =============================================================================

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <cstring>

// sokol_audio 前方宣言（ヘッダーは sokol_impl.mm でインクルード済み）
extern "C" {
    typedef struct saudio_desc {
        int sample_rate;
        int num_channels;
        int buffer_frames;
        int packet_frames;
        int num_packets;
        void (*stream_cb)(float* buffer, int num_frames, int num_channels);
        void (*stream_userdata_cb)(float* buffer, int num_frames, int num_channels, void* user_data);
        void* user_data;
        // logger は省略
        char _padding[64];
    } saudio_desc;

    void saudio_setup(const saudio_desc* desc);
    void saudio_shutdown(void);
    bool saudio_isvalid(void);
}

// stb_vorbis 前方宣言
extern "C" {
    typedef struct stb_vorbis stb_vorbis;

    typedef struct {
        unsigned int sample_rate;
        int channels;
        unsigned int setup_memory_required;
        unsigned int setup_temp_memory_required;
        unsigned int temp_memory_required;
        int max_frame_size;
    } stb_vorbis_info;

    stb_vorbis* stb_vorbis_open_filename(const char *filename, int *error, void *alloc_buffer);
    stb_vorbis_info stb_vorbis_get_info(stb_vorbis *f);
    int stb_vorbis_stream_length_in_samples(stb_vorbis *f);
    int stb_vorbis_get_samples_float_interleaved(stb_vorbis *f, int channels, float *buffer, int num_floats);
    void stb_vorbis_close(stb_vorbis *f);
}

// dr_wav 前方宣言
extern "C" {
    typedef uint64_t drwav_uint64;
    float* drwav_open_file_and_read_pcm_frames_f32(
        const char* filename, unsigned int* channels, unsigned int* sampleRate,
        drwav_uint64* totalFrameCount, void* pAllocationCallbacks);
    void drwav_free(void* p, void* pAllocationCallbacks);
}

// dr_mp3 前方宣言
extern "C" {
    typedef uint64_t drmp3_uint64;
    typedef uint32_t drmp3_uint32;
    typedef struct {
        drmp3_uint32 channels;
        drmp3_uint32 sampleRate;
    } drmp3_config;
    float* drmp3_open_file_and_read_pcm_frames_f32(
        const char* filePath, drmp3_config* pConfig,
        drmp3_uint64* pTotalFrameCount, void* pAllocationCallbacks);
    void drmp3_free(void* p, void* pAllocationCallbacks);
}

namespace trussc {

// ---------------------------------------------------------------------------
// サウンドバッファ（デコード済みデータ）
// ---------------------------------------------------------------------------
class SoundBuffer {
public:
    std::vector<float> samples;  // インターリーブされたサンプル
    int channels = 0;
    int sampleRate = 0;
    size_t numSamples = 0;       // チャンネルあたりのサンプル数

    bool loadOgg(const std::string& path) {
        int error = 0;
        stb_vorbis* vorbis = stb_vorbis_open_filename(path.c_str(), &error, nullptr);
        if (!vorbis) {
            printf("SoundBuffer: failed to open %s (error=%d)\n", path.c_str(), error);
            return false;
        }

        stb_vorbis_info info = stb_vorbis_get_info(vorbis);
        channels = info.channels;
        sampleRate = info.sample_rate;
        numSamples = stb_vorbis_stream_length_in_samples(vorbis);

        samples.resize(numSamples * channels);

        int decoded = stb_vorbis_get_samples_float_interleaved(
            vorbis, channels, samples.data(), samples.size());

        stb_vorbis_close(vorbis);

        printf("SoundBuffer: loaded %s (%d ch, %d Hz, %zu samples)\n",
               path.c_str(), channels, sampleRate, numSamples);

        return decoded > 0;
    }

    bool loadWav(const std::string& path) {
        unsigned int ch, sr;
        drwav_uint64 frameCount;

        float* data = drwav_open_file_and_read_pcm_frames_f32(
            path.c_str(), &ch, &sr, &frameCount, nullptr);

        if (!data) {
            printf("SoundBuffer: failed to open WAV %s\n", path.c_str());
            return false;
        }

        channels = ch;
        sampleRate = sr;
        numSamples = frameCount;

        samples.resize(numSamples * channels);
        std::memcpy(samples.data(), data, samples.size() * sizeof(float));

        drwav_free(data, nullptr);

        printf("SoundBuffer: loaded WAV %s (%d ch, %d Hz, %zu samples)\n",
               path.c_str(), channels, sampleRate, numSamples);

        return true;
    }

    bool loadMp3(const std::string& path) {
        drmp3_config config = {0, 0};
        drmp3_uint64 frameCount = 0;

        float* data = drmp3_open_file_and_read_pcm_frames_f32(
            path.c_str(), &config, &frameCount, nullptr);

        if (!data) {
            printf("SoundBuffer: failed to open MP3 %s\n", path.c_str());
            return false;
        }

        channels = config.channels;
        sampleRate = config.sampleRate;
        numSamples = frameCount;

        samples.resize(numSamples * channels);
        std::memcpy(samples.data(), data, samples.size() * sizeof(float));

        drmp3_free(data, nullptr);

        printf("SoundBuffer: loaded MP3 %s (%d ch, %d Hz, %zu samples)\n",
               path.c_str(), channels, sampleRate, numSamples);

        return true;
    }

    float getDuration() const {
        if (sampleRate == 0) return 0;
        return (float)numSamples / sampleRate;
    }

    // テスト用: サイン波を生成
    void generateSineWave(float frequency, float duration, int sr = 44100) {
        sampleRate = sr;
        channels = 1;
        numSamples = (size_t)(duration * sampleRate);
        samples.resize(numSamples);

        for (size_t i = 0; i < numSamples; i++) {
            float t = (float)i / sampleRate;
            samples[i] = 0.5f * std::sin(2.0f * 3.14159265f * frequency * t);
        }

        printf("SoundBuffer: generated sine wave (%.0f Hz, %.1f sec)\n", frequency, duration);
    }
};

// ---------------------------------------------------------------------------
// 再生中の音声インスタンス
// ---------------------------------------------------------------------------
struct PlayingSound {
    std::shared_ptr<SoundBuffer> buffer;
    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};        // -1.0 (左) ~ 0.0 (中央) ~ 1.0 (右)
    std::atomic<float> speed{1.0f};      // 0.5 (半速) ~ 1.0 (通常) ~ 2.0 (倍速)
    std::atomic<bool> loop{false};
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};

    // 再生位置（浮動小数点で管理、speed 対応用）
    double positionF{0.0};
};

// ---------------------------------------------------------------------------
// オーディオエンジン（シングルトン）
// ---------------------------------------------------------------------------
class AudioEngine {
public:
    static constexpr int MAX_PLAYING_SOUNDS = 32;
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int NUM_CHANNELS = 2;  // ステレオ出力
    static constexpr int ANALYSIS_BUFFER_SIZE = 4096;  // FFT解析用バッファサイズ

    static AudioEngine& getInstance() {
        static AudioEngine instance;
        return instance;
    }

    bool init() {
        if (initialized_) return true;

        saudio_desc desc = {};
        desc.sample_rate = SAMPLE_RATE;
        desc.num_channels = NUM_CHANNELS;
        desc.stream_userdata_cb = audioCallback;
        desc.user_data = this;

        saudio_setup(&desc);

        if (!saudio_isvalid()) {
            printf("AudioEngine: failed to initialize\n");
            return false;
        }

        initialized_ = true;
        printf("AudioEngine: initialized (%d Hz, %d ch)\n", SAMPLE_RATE, NUM_CHANNELS);
        return true;
    }

    void shutdown() {
        if (initialized_) {
            saudio_shutdown();
            initialized_ = false;
        }
    }

    // FFT解析用: 最新のオーディオサンプルを取得（モノラル、左右平均）
    // numSamples: 取得するサンプル数（最大 ANALYSIS_BUFFER_SIZE）
    // 戻り値: 取得したサンプル数
    size_t getAnalysisBuffer(float* outBuffer, size_t numSamples) {
        if (!initialized_ || numSamples == 0) return 0;

        numSamples = std::min(numSamples, (size_t)ANALYSIS_BUFFER_SIZE);

        std::lock_guard<std::mutex> lock(analysisMutex_);

        // リングバッファから最新のサンプルをコピー
        size_t readPos = (analysisWritePos_ + ANALYSIS_BUFFER_SIZE - numSamples) % ANALYSIS_BUFFER_SIZE;

        for (size_t i = 0; i < numSamples; i++) {
            outBuffer[i] = analysisBuffer_[(readPos + i) % ANALYSIS_BUFFER_SIZE];
        }

        return numSamples;
    }

    // 新しい再生インスタンスを追加
    std::shared_ptr<PlayingSound> play(std::shared_ptr<SoundBuffer> buffer) {
        if (!initialized_ || !buffer) return nullptr;

        std::lock_guard<std::mutex> lock(mutex_);

        // 空きスロットを探す
        for (auto& slot : playingSounds_) {
            if (!slot || !slot->playing) {
                slot = std::make_shared<PlayingSound>();
                slot->buffer = buffer;
                slot->positionF = 0.0;
                slot->volume = 1.0f;
                slot->pan = 0.0f;
                slot->speed = 1.0f;
                slot->loop = false;
                slot->playing = true;
                slot->paused = false;
                return slot;
            }
        }

        printf("AudioEngine: max playing sounds reached\n");
        return nullptr;
    }

private:
    AudioEngine() {
        playingSounds_.resize(MAX_PLAYING_SOUNDS);
        analysisBuffer_.resize(ANALYSIS_BUFFER_SIZE, 0.0f);
    }

    ~AudioEngine() {
        shutdown();
    }

    static void audioCallback(float* buffer, int num_frames, int num_channels, void* user_data) {
        auto* engine = static_cast<AudioEngine*>(user_data);
        engine->mixAudio(buffer, num_frames, num_channels);
    }

    void mixAudio(float* buffer, int num_frames, int num_channels) {
        // バッファをクリア
        std::memset(buffer, 0, num_frames * num_channels * sizeof(float));

        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& sound : playingSounds_) {
            if (!sound || !sound->playing || sound->paused) continue;

            auto& src = sound->buffer;
            double posF = sound->positionF;
            float vol = sound->volume;
            float pan = sound->pan;
            float speed = sound->speed;

            // pan から左右の音量係数を計算
            // pan = -1.0: 左100%, 右0%
            // pan =  0.0: 左100%, 右100%
            // pan =  1.0: 左0%,   右100%
            float panL = (pan <= 0.0f) ? 1.0f : (1.0f - pan);
            float panR = (pan >= 0.0f) ? 1.0f : (1.0f + pan);

            for (int frame = 0; frame < num_frames; frame++) {
                size_t pos0 = (size_t)posF;
                size_t pos1 = pos0 + 1;
                float frac = (float)(posF - pos0);

                // ループ処理
                if (pos0 >= src->numSamples) {
                    if (sound->loop) {
                        posF = 0.0;
                        pos0 = 0;
                        pos1 = 1;
                        frac = 0.0f;
                    } else {
                        sound->playing = false;
                        break;
                    }
                }

                // 境界チェック (pos1 が範囲外の場合)
                if (pos1 >= src->numSamples) {
                    pos1 = sound->loop ? 0 : pos0;
                }

                // サンプル取得 (線形補間)
                float left0, right0, left1, right1;
                if (src->channels == 1) {
                    // モノラル
                    left0 = right0 = src->samples[pos0];
                    left1 = right1 = src->samples[pos1];
                } else {
                    // ステレオ
                    left0 = src->samples[pos0 * 2];
                    right0 = src->samples[pos0 * 2 + 1];
                    left1 = src->samples[pos1 * 2];
                    right1 = src->samples[pos1 * 2 + 1];
                }

                // 線形補間
                float left = left0 + (left1 - left0) * frac;
                float right = right0 + (right1 - right0) * frac;

                // 音量とパンを適用
                left *= vol * panL;
                right *= vol * panR;

                // ミックス
                buffer[frame * num_channels] += left;
                if (num_channels > 1) {
                    buffer[frame * num_channels + 1] += right;
                }

                posF += speed;
            }

            sound->positionF = posF;
        }

        // クリッピング
        for (int i = 0; i < num_frames * num_channels; i++) {
            if (buffer[i] > 1.0f) buffer[i] = 1.0f;
            if (buffer[i] < -1.0f) buffer[i] = -1.0f;
        }

        // FFT解析用リングバッファにコピー（モノラル化: 左右平均）
        {
            std::lock_guard<std::mutex> lock(analysisMutex_);
            for (int frame = 0; frame < num_frames; frame++) {
                float mono;
                if (num_channels > 1) {
                    mono = (buffer[frame * num_channels] + buffer[frame * num_channels + 1]) * 0.5f;
                } else {
                    mono = buffer[frame * num_channels];
                }
                analysisBuffer_[analysisWritePos_] = mono;
                analysisWritePos_ = (analysisWritePos_ + 1) % ANALYSIS_BUFFER_SIZE;
            }
        }
    }

    bool initialized_ = false;
    std::vector<std::shared_ptr<PlayingSound>> playingSounds_;
    std::mutex mutex_;

    // FFT解析用リングバッファ
    std::vector<float> analysisBuffer_;
    size_t analysisWritePos_ = 0;
    std::mutex analysisMutex_;
};

// ---------------------------------------------------------------------------
// サウンドクラス（ユーザー向け）
// ---------------------------------------------------------------------------
class Sound {
public:
    Sound() = default;
    ~Sound() = default;

    // コピー・ムーブ
    Sound(const Sound&) = default;
    Sound& operator=(const Sound&) = default;
    Sound(Sound&&) = default;
    Sound& operator=(Sound&&) = default;

    // -------------------------------------------------------------------------
    // 読み込み
    // -------------------------------------------------------------------------
    bool load(const std::string& path) {
        // AudioEngine を初期化（初回のみ）
        AudioEngine::getInstance().init();

        buffer_ = std::make_shared<SoundBuffer>();

        // 拡張子で判別
        std::string ext = path.substr(path.find_last_of('.') + 1);
        bool success = false;

        if (ext == "ogg" || ext == "OGG") {
            success = buffer_->loadOgg(path);
        } else if (ext == "wav" || ext == "WAV") {
            success = buffer_->loadWav(path);
        } else if (ext == "mp3" || ext == "MP3") {
            success = buffer_->loadMp3(path);
        } else {
            printf("Sound: unsupported format: %s\n", ext.c_str());
        }

        if (!success) {
            buffer_.reset();
            return false;
        }
        return true;
    }

    // テスト用: サイン波を生成
    void loadTestTone(float frequency = 440.0f, float duration = 1.0f) {
        AudioEngine::getInstance().init();
        buffer_ = std::make_shared<SoundBuffer>();
        buffer_->generateSineWave(frequency, duration);
    }

    bool isLoaded() const { return buffer_ != nullptr; }

    // -------------------------------------------------------------------------
    // 再生制御
    // -------------------------------------------------------------------------
    void play() {
        if (!buffer_) return;

        // 既に再生中なら停止
        stop();

        playing_ = AudioEngine::getInstance().play(buffer_);
        if (playing_) {
            playing_->volume = volume_;
            playing_->pan = pan_;
            playing_->speed = speed_;
            playing_->loop = loop_;
        }
    }

    void stop() {
        if (playing_) {
            playing_->playing = false;
            playing_.reset();
        }
    }

    void pause() {
        if (playing_) {
            playing_->paused = true;
        }
    }

    void resume() {
        if (playing_) {
            playing_->paused = false;
        }
    }

    // -------------------------------------------------------------------------
    // 設定
    // -------------------------------------------------------------------------
    void setVolume(float vol) {
        volume_ = vol;
        if (playing_) {
            playing_->volume = vol;
        }
    }

    float getVolume() const { return volume_; }

    void setLoop(bool loop) {
        loop_ = loop;
        if (playing_) {
            playing_->loop = loop;
        }
    }

    bool isLoop() const { return loop_; }

    void setPan(float pan) {
        // -1.0 (左) ~ 0.0 (中央) ~ 1.0 (右)
        pan_ = (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan;
        if (playing_) {
            playing_->pan = pan_;
        }
    }

    float getPan() const { return pan_; }

    void setSpeed(float speed) {
        // 0.1 ~ 4.0 の範囲に制限（極端な値を防ぐ）
        speed_ = (speed < 0.1f) ? 0.1f : (speed > 4.0f) ? 4.0f : speed;
        if (playing_) {
            playing_->speed = speed_;
        }
    }

    float getSpeed() const { return speed_; }

    // -------------------------------------------------------------------------
    // 状態
    // -------------------------------------------------------------------------
    bool isPlaying() const {
        return playing_ && playing_->playing && !playing_->paused;
    }

    bool isPaused() const {
        return playing_ && playing_->paused;
    }

    float getPosition() const {
        if (!playing_ || !buffer_) return 0;
        return (float)playing_->positionF / buffer_->sampleRate;
    }

    float getDuration() const {
        return buffer_ ? buffer_->getDuration() : 0;
    }

private:
    std::shared_ptr<SoundBuffer> buffer_;
    std::shared_ptr<PlayingSound> playing_;
    float volume_ = 1.0f;
    float pan_ = 0.0f;
    float speed_ = 1.0f;
    bool loop_ = false;
};

// ---------------------------------------------------------------------------
// グローバル関数
// ---------------------------------------------------------------------------

// オーディオエンジンを初期化（setup() で自動的に呼ばれる）
inline void initAudio() {
    AudioEngine::getInstance().init();
}

// オーディオエンジンをシャットダウン
inline void shutdownAudio() {
    AudioEngine::getInstance().shutdown();
}

// FFT解析用: 最新のオーディオサンプルを取得
inline size_t getAudioAnalysisBuffer(float* outBuffer, size_t numSamples) {
    return AudioEngine::getInstance().getAnalysisBuffer(outBuffer, numSamples);
}

} // namespace trussc

namespace tc = trussc;
