#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("ChipSound Example");
    createSounds();
}

// Helper to create and add a sound button
SoundButton::Ptr makeButton(float x, float y, float w, float h,
                            const string& label, Sound sound, bool isLoop = false) {
    auto btn = make_shared<SoundButton>();
    btn->x = x;
    btn->y = y;
    btn->setSize(w, h);
    btn->label = label;
    btn->sound = sound;
    btn->isLoop = isLoop;
    return btn;
}

void tcApp::createSounds() {
    float x = margin;
    float y = 55;

    // =========================================================================
    // Section 1: Simple Notes (6 types)
    // =========================================================================
    vector<pair<string, Wave>> waveTypes = {
        {"Sin", Wave::Sin},
        {"Square", Wave::Square},
        {"Triangle", Wave::Triangle},
        {"Sawtooth", Wave::Sawtooth},
        {"Noise", Wave::Noise},
        {"Pink", Wave::PinkNoise}
    };

    for (const auto& [name, wave] : waveTypes) {
        ChipSoundNote note;
        note.wave = wave;
        note.hz = 440;
        note.duration = 0.3f;
        note.volume = 0.4f;

        auto btn = makeButton(x, y, buttonWidth, buttonHeight, name, note.build());
        addChild(btn);
        allButtons.push_back(btn);
        x += buttonWidth + margin;
    }

    // =========================================================================
    // Section 2: Chords (8 types)
    // =========================================================================
    x = margin;
    y += buttonHeight + margin + 20;

    auto makeChord = [](vector<float> frequencies, Wave wave = Wave::Square) {
        ChipSoundBundle bundle;
        for (float hz : frequencies) {
            ChipSoundNote note;
            note.wave = wave;
            note.hz = hz;
            note.duration = 0.4f;
            note.volume = 0.3f;
            bundle.add(note, 0.0f);
        }
        return bundle.build();
    };

    // Row 1
    auto addChordButton = [&](const string& label, Sound sound) {
        auto btn = makeButton(x, y, buttonWidth, buttonHeight, label, sound);
        addChild(btn);
        allButtons.push_back(btn);
        x += buttonWidth + margin;
    };

    addChordButton("C Major", makeChord({261.63f, 329.63f, 392.00f}));
    addChordButton("A Minor", makeChord({220.0f, 261.63f, 329.63f}));
    addChordButton("Power", makeChord({261.63f, 392.00f}));
    addChordButton("Octave", makeChord({440.0f, 880.0f}));

    // Row 2
    x = margin;
    y += buttonHeight + margin;

    addChordButton("Fifth", makeChord({440.0f, 660.0f}));
    addChordButton("Dissonant", makeChord({440.0f, 466.16f}));
    addChordButton("Thick", makeChord({261.63f, 329.63f, 392.00f, 523.25f}));

    // Mixed waves chord
    {
        ChipSoundBundle bundle;
        ChipSoundNote n1{Wave::Sin, 261.63f, 0.4f, 0.3f};
        ChipSoundNote n2{Wave::Square, 329.63f, 0.4f, 0.25f};
        ChipSoundNote n3{Wave::Triangle, 392.00f, 0.4f, 0.3f};
        bundle.add(n1, 0.0f);
        bundle.add(n2, 0.0f);
        bundle.add(n3, 0.0f);
        addChordButton("Mixed", bundle.build());
    }

    // =========================================================================
    // Section 3: Effects (8 types)
    // =========================================================================
    x = margin;
    y += buttonHeight + margin + 20;

    auto addEffectButton = [&](const string& label, Sound sound) {
        auto btn = makeButton(x, y, buttonWidth, buttonHeight, label, sound);
        addChild(btn);
        allButtons.push_back(btn);
        x += buttonWidth + margin;
    };

    // Detune
    {
        ChipSoundBundle bundle;
        ChipSoundNote n1{Wave::Square, 440.0f, 0.3f, 0.3f};
        ChipSoundNote n2{Wave::Square, 443.0f, 0.3f, 0.3f};
        n1.attack = 0.01f; n1.decay = 0.05f; n1.sustain = 0.6f; n1.release = 0.1f;
        n2.attack = 0.01f; n2.decay = 0.05f; n2.sustain = 0.6f; n2.release = 0.1f;
        bundle.add(n1, 0.0f);
        bundle.add(n2, 0.0f);
        addEffectButton("Detune", bundle.build());
    }

    // Arpeggio
    {
        ChipSoundBundle bundle;
        float times[] = {0.0f, 0.05f, 0.1f, 0.15f};
        float freqs[] = {261.63f, 329.63f, 392.00f, 523.25f};
        for (int i = 0; i < 4; i++) {
            ChipSoundNote n{Wave::Square, freqs[i], 0.15f, 0.35f};
            n.attack = 0.005f; n.decay = 0.02f; n.sustain = 0.5f; n.release = 0.08f;
            bundle.add(n, times[i]);
        }
        addEffectButton("Arpeggio", bundle.build());
    }

    // Rise
    {
        ChipSoundBundle bundle;
        for (int i = 0; i < 8; i++) {
            float hz = 200.0f * pow(2.0f, i / 8.0f);
            ChipSoundNote n{Wave::Square, hz, 0.08f, 0.35f};
            n.attack = 0.005f; n.decay = 0.01f; n.sustain = 0.8f; n.release = 0.02f;
            bundle.add(n, i * 0.06f);
        }
        addEffectButton("Rise", bundle.build());
    }

    // Fall
    {
        ChipSoundBundle bundle;
        for (int i = 0; i < 8; i++) {
            float hz = 800.0f * pow(0.5f, i / 8.0f);
            ChipSoundNote n{Wave::Square, hz, 0.08f, 0.35f};
            n.attack = 0.005f; n.decay = 0.01f; n.sustain = 0.8f; n.release = 0.02f;
            bundle.add(n, i * 0.06f);
        }
        addEffectButton("Fall", bundle.build());
    }

    // Row 2 of effects
    x = margin;
    y += buttonHeight + margin;

    // Hit
    {
        ChipSoundBundle bundle;
        ChipSoundNote hit{Wave::Noise, 0, 0.08f, 0.5f};
        hit.attack = 0.001f; hit.decay = 0.02f; hit.sustain = 0.3f; hit.release = 0.05f;
        bundle.add(hit, 0.0f);
        for (int i = 0; i < 4; i++) {
            float hz = 200.0f * pow(0.7f, (float)i);
            ChipSoundNote n{Wave::Square, hz, 0.03f, 0.3f};
            n.attack = 0.001f; n.decay = 0.01f; n.sustain = 0.5f; n.release = 0.02f;
            bundle.add(n, i * 0.015f);
        }
        addEffectButton("Hit", bundle.build());
    }

    // Explosion
    {
        ChipSoundBundle bundle;
        ChipSoundNote boom{Wave::Noise, 0, 0.3f, 0.6f};
        boom.attack = 0.005f; boom.decay = 0.1f; boom.sustain = 0.4f; boom.release = 0.15f;
        bundle.add(boom, 0.0f);
        ChipSoundNote rumble{Wave::Square, 60.0f, 0.25f, 0.3f};
        rumble.attack = 0.01f; rumble.decay = 0.08f; rumble.sustain = 0.3f; rumble.release = 0.1f;
        bundle.add(rumble, 0.0f);
        addEffectButton("Explosion", bundle.build());
    }

    // Laser
    {
        ChipSoundBundle bundle;
        for (int i = 0; i < 10; i++) {
            float hz = 1200.0f * pow(0.85f, (float)i);
            ChipSoundNote n{Wave::Square, hz, 0.025f, 0.35f};
            n.attack = 0.001f; n.decay = 0.005f; n.sustain = 0.8f; n.release = 0.01f;
            bundle.add(n, i * 0.02f);
        }
        ChipSoundNote tail{Wave::Noise, 0, 0.05f, 0.15f};
        tail.attack = 0.01f; tail.decay = 0.02f; tail.sustain = 0.2f; tail.release = 0.02f;
        bundle.add(tail, 0.15f);
        addEffectButton("Laser", bundle.build());
    }

    // Jump
    {
        ChipSoundBundle bundle;
        for (int i = 0; i < 5; i++) {
            float hz = 150.0f * pow(1.3f, (float)i);
            ChipSoundNote n{Wave::Square, hz, 0.03f, 0.3f};
            n.attack = 0.002f; n.decay = 0.01f; n.sustain = 0.7f; n.release = 0.01f;
            bundle.add(n, i * 0.025f);
        }
        for (int i = 0; i < 5; i++) {
            float hz = 150.0f * pow(1.3f, 4.0f - i);
            ChipSoundNote n{Wave::Square, hz, 0.03f, 0.3f};
            n.attack = 0.002f; n.decay = 0.01f; n.sustain = 0.7f; n.release = 0.01f;
            bundle.add(n, 0.125f + i * 0.025f);
        }
        addEffectButton("Jump", bundle.build());
    }

    // =========================================================================
    // Section 4: Melodies (2 loops)
    // =========================================================================
    x = margin;
    y += buttonHeight + margin + 20;

    // Fanfare
    {
        ChipSoundBundle bundle;
        float notes[] = {261.63f, 329.63f, 392.00f, 523.25f, 523.25f};
        float times[] = {0.0f, 0.15f, 0.3f, 0.45f, 0.6f};
        float durs[]  = {0.12f, 0.12f, 0.12f, 0.12f, 0.25f};
        for (int i = 0; i < 5; i++) {
            ChipSoundNote n{Wave::Square, notes[i], durs[i], 0.35f};
            n.attack = 0.01f; n.decay = 0.02f; n.sustain = 0.7f; n.release = 0.03f;
            bundle.add(n, times[i]);
        }
        bundle.add({Wave::Silent, 0, 0.3f}, 0.85f);

        Sound sound = bundle.build();
        sound.setLoop(true);
        auto btn = makeButton(x, y, buttonWidth * 1.4f, buttonHeight, "Fanfare (Loop)", sound, true);
        addChild(btn);
        allButtons.push_back(btn);
        x += buttonWidth * 1.4f + margin;
    }

    // 8-bit BGM
    {
        ChipSoundBundle bundle;
        float beatLen = 0.25f;
        float noteLen = 0.2f;

        float bassNotes[] = {130.81f, 130.81f, 146.83f, 146.83f};
        for (int i = 0; i < 4; i++) {
            ChipSoundNote n{Wave::Square, bassNotes[i], noteLen, 0.25f};
            n.attack = 0.01f; n.decay = 0.05f; n.sustain = 0.5f; n.release = 0.04f;
            bundle.add(n, i * beatLen);
        }

        float melNotes[] = {523.25f, 587.33f, 659.25f, 587.33f};
        for (int i = 0; i < 4; i++) {
            ChipSoundNote n{Wave::Triangle, melNotes[i], noteLen, 0.3f};
            n.attack = 0.01f; n.decay = 0.03f; n.sustain = 0.6f; n.release = 0.04f;
            bundle.add(n, i * beatLen);
        }
        bundle.add({Wave::Silent, 0, 0.01f}, 0.99f);

        Sound sound = bundle.build();
        sound.setLoop(true);
        auto btn = makeButton(x, y, buttonWidth * 1.4f, buttonHeight, "8bit BGM (Loop)", sound, true);
        addChild(btn);
        allButtons.push_back(btn);
    }
}

void tcApp::draw() {
    clear(0.1f);

    // Title
    setColor(1.0f);
    drawBitmapString("=== ChipSound Example ===", margin, 25);

    // Section labels
    setColor(0.8f, 0.8f, 0.4f);
    drawBitmapString("Simple Notes", margin, 55 - 15);
    drawBitmapString("Chords", margin, 55 + buttonHeight + margin + 20 - 15);
    drawBitmapString("Effects", margin, 55 + (buttonHeight + margin) * 3 + 40 - 15);
    drawBitmapString("Melodies (click to toggle loop)", margin, 55 + (buttonHeight + margin) * 5 + 60 - 15);

    // Instructions
    setColor(0.5f);
    drawBitmapString("Click buttons to play sounds. Melodies toggle on/off.", margin, getWindowHeight() - 25);
}
