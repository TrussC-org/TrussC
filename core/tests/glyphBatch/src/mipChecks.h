// Included inside GlyphApp: comparison and cost probes, with no timing assertions.
    void mipComparison() {
        const char* scenes[] = {"plain12", "plain24", "plain36", "scale-half", "scale-quarter",
            "rotate2d", "scale-axis", "rotateY60", "perspective-near", "perspective-far", "fbo-window"};
        for (bool enabled : {true, false}) for (int scene = 0; scene < 11; ++scene) {
            Font sample;
            sample.setMipmaps(enabled);
            sample.setGridFit(false);
            check("comparison font loads", bool(sample.load(testFontName, scene < 3 ? 12 + scene * 12 : 24)));
            Bytes images[2];
            for (int path = 0; path < 2; ++path) {
                images[path] = render(path ? actual_ : reference_, [&] {
                    EasyCam camera;
                    if (scene == 8 || scene == 9) {
                        camera.setDistance(scene == 8 ? 200 : 400);
                        camera.begin();
                    }
                    pushMatrix();
                    translate(scene == 8 || scene == 9 ? -60 : 20, scene == 8 || scene == 9 ? 15 : 60);
                    if (scene == 3) scale(0.5f);
                    if (scene == 4 || scene == 10) scale(0.25f);
                    if (scene == 5) rotateZ(0.4f);
                    if (scene == 6) scale(1, 0.3f);
                    if (scene >= 7 && scene <= 9) rotateY(TAU / 6);
                    if (path) {
                        GlyphBatch batch;
                        batch.setFont(sample);
                        batch.add("Readable R8", tint);
                        batch.draw();
                    } else sample.drawString("Readable R8", 0, 0);
                    popMatrix();
                    if (scene == 8 || scene == 9) camera.end();
                });
                if (scene == 10) {
                    clear(0, 0, 0, 0);
                    setupScreenOrtho();
                    setColor(1);
                    setBlendMode(BlendMode::Alpha);
                    (path ? actual_ : reference_).draw(0, 0);
                    Pixels screen;
                    check("Fbo window readback", grabScreen(screen));
                    images[path].assign(screen.getData(), screen.getData() + screen.getTotalBytes());
                }
                Pixels pixels;
                pixels.setFromPixels(images[path].data(), extent, extent, 4);
                const std::string name = std::string(scenes[scene]) + (enabled ? "-mips-" : "-base-") + (path ? "batch" : "font");
                check("save comparison PNG", bool(pixels.save(comparisonDirectory / (name + ".png"))));
            }
            // Log strict byte differences too; tolerance remains 2 for GPU parity.
            compare(scenes[scene], images[0], images[1]);
        }
    }
    void mipBenchmark() {
        constexpr int glyphs = 3000;
        for (bool dirty : {false, true}) for (int path = 0; path < 2; ++path) {
            double samples[3]{};
            for (int sample = 0; sample < 3; ++sample) {
                Font fonts[3];
                const char* families[] = {TC_FONT_SANS, TC_FONT_SERIF, TC_FONT_MONO};
                for (int band = 0; band < 3; ++band) {
                    fonts[band].setGridFit(false);
                    check("benchmark font loads", bool(fonts[band].load(families[band], 16 + 8 * band)));
                }
                // Warm static glyphs before measuring. Dirty frames introduce new coverage.
                render(reference_, [&] { for (auto& font : fonts) font.drawString("R8 text", 0, 0); });
                sg_commit();
                const auto start = std::chrono::steady_clock::now();
                for (int frame = 0; frame < 8; ++frame) {
                    reference_.begin(0, 0, 0, 0);
                    setupScreenOrtho();
                    setColor(tint);
                    for (int band = 0; band < 3; ++band) {
                        const uint32_t cp = 0x400 + sample * 64 + path * 24 + frame;
                        const std::string text = dirty ? std::string{char(0xC0 | (cp >> 6)), char(0x80 | (cp & 63))} : "R";
                        GlyphBatch batch;
                        batch.setFont(fonts[band]);
                        for (int i = 0; i < glyphs / 3; ++i) {
                            pushMatrix();
                            translate(float(i % 25 * 7), float(i / 25 % 25 * 7));
                            scale(0.3f);
                            if (path) batch.add(text, tint);
                            else fonts[band].drawString(text, 0, 0);
                            popMatrix();
                        }
                        if (path) batch.draw();
                    }
                    reference_.end();
                    read(reference_); // Include completion of the same GPU work.
                    sg_commit();
                    internal::drainPendingGpuDestroys();
                }
                samples[sample] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 8;
            }
            std::sort(samples, samples + 3);
            std::printf("MIP_BENCH glyphs=%d fonts=3 dirty=%d path=%s median3_ms=%.3f samples=%.3f,%.3f,%.3f\n",
                glyphs, dirty, path ? "GlyphBatch" : "drawString", samples[1], samples[0], samples[1], samples[2]);
        }
    }

    // Fixed steps, with raw captures for independent temporal/parity analysis.
    // Checkpoints also cover endpoints and exact policy-boundary transforms.
    // Animation tolerances separate rounding noise from LOD splits: on Pi 5 (V3D)
    // the two paths' vertex/UV rounding differs by up to 3 on a single frame
    // (second difference 6), while a split LOD choice differs by 80 or more.
    static constexpr int animParityTolerance = 4;
    static constexpr int animPopTolerance = 8;
    void mipAnimation() {
        Font sample;
        sample.setGridFit(false);
        check("animation font loads", bool(sample.load(testFontName, 24)));
        const char* sweeps[] = {"scale", "rotate", "dolly"};
        for (int sweep = 0; sweep < 3; ++sweep) {
            std::ofstream output(animationDirectory / (std::string(sweeps[sweep]) + ".rgba"), std::ios::binary);
            const std::vector<float> keys = sweep == 0 ? std::vector<float>{0.30f, 0.40f, 0.50f, 0.60f}
                : sweep == 1 ? std::vector<float>{45, 60, 75} : std::vector<float>{400, 300, 200};
            std::vector<int> previous(extent * extent), older(previous.size());
            int temporalMax = 0;
            for (int frame = 0; frame < 240 + int(keys.size()); ++frame) {
                const float value = frame >= 240 ? keys[frame - 240]
                    : sweep == 0 ? 0.30f + frame * 0.00125f
                    : sweep == 1 ? 40.0f + frame * (35.0f / 239.0f)
                    : 400.0f - frame * (200.0f / 239.0f);
                Bytes images[2];
                for (int path = 0; path < 2; ++path) {
                    const auto pixels = render(path ? actual_ : reference_, [&] {
                        EasyCam camera;
                        if (sweep == 2) {
                            camera.setDistance(value);
                            camera.begin();
                        }
                        pushMatrix();
                        translate(sweep == 2 ? -60 : 20, sweep == 2 ? 15 : 60);
                        if (sweep == 0) scale(value);
                        if (sweep == 1) rotateY(value * TAU / 360.0f);
                        if (sweep == 2) rotateY(TAU / 6);
                        if (path) {
                            GlyphBatch batch;
                            batch.setFont(sample);
                            batch.add("Readable R8", tint);
                            batch.draw();
                        } else sample.drawString("Readable R8", 0, 0);
                        popMatrix();
                        if (sweep == 2) camera.end();
                    });
                    output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
                    images[path] = pixels;
                }
                const std::string label = std::string(sweeps[sweep]) + " frame " + std::to_string(frame);
                compare(label.c_str(), images[0], images[1], animParityTolerance);
                // Absolute parity image isolates path differences from shared motion shimmer.
                // Only consecutive regular frames participate; checkpoints are separate.
                if (frame < 240) {
                    for (size_t i = 0; i < previous.size(); ++i) {
                        int error = 0;
                        for (int channel = 0; channel < 4; ++channel) {
                            const size_t offset = i * 4 + channel;
                            error = std::max(error, std::abs(int(images[0][offset]) - int(images[1][offset])));
                        }
                        if (frame >= 2)
                            temporalMax = std::max(temporalMax, std::abs(error - 2 * previous[i] + older[i]));
                        older[i] = previous[i];
                        previous[i] = error;
                    }
                }
                sg_commit();
                internal::drainPendingGpuDestroys();
            }
            std::printf("MIP_ANIM %s parity_second_difference_max=%d tolerance=%d\n", sweeps[sweep], temporalMax, animPopTolerance);
            check("animation parity has no temporal pop", temporalMax <= animPopTolerance);
            check("save animation frames", bool(output));
        }
    }
