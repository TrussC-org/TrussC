-- Shared by the headless tests and the windowed bindcheck harness.
local function approx(a, b) return math.abs(a - b) < 0.0001 end
local function noValues(...) assert(select('#', ...) == 0) end
local function collectTwice()
    collectgarbage('collect')
    collectgarbage('collect')
end
local function checkPixels(p)
    p:allocate(4, 4, 4)
    p:setColor(0, 0, Color(1, 0, 0))
    p:setColor(3, 0, Color(0, 1, 0))
    p:setColor(0, 3, Color(0, 0, 1))
    noValues(p:mirrorH())
    assert(p:getColor(0, 0).g == 1)
    noValues(p:mirrorV())
    assert(p:getColor(3, 0).b == 1)
    noValues(p:mirror(true, true))
    assert(p:getColor(0, 0).r == 1)
    noValues(p:crop(0, 0, 2, 2))
    assert(p:getWidth() == 2 and p:getHeight() == 2)
    assert(p:getColor(0, 0).r == 1)
    noValues(p:resize(4, 4))
    assert(p:getWidth() == 4 and p:getHeight() == 4)
    noValues(p:halve())
    assert(p:getWidth() == 2 and p:getHeight() == 2)
end
return {
    {'Mat3/Mat4.set preserve fractions; tryInvert returns bool, Mat4', function()
        local a, b = Mat3(), Mat4()
        noValues(a:set(0, 1, 0.25))
        noValues(b:set(0, 1, -0.75))
        assert(a:at(0, 1) == 0.25 and b:at(0, 1) == -0.75)
        local ok, inverse = b:tryInvert()
        assert(type(ok) == 'boolean' and ok and inverse:at(0, 1) == 0.75)
        assert(b:at(0, 1) == -0.75)
        b:set(0, 0, 0)
        local failed = b:tryInvert()
        assert(type(failed) == 'boolean' and not failed)
        return true
    end},
    {'EasyCam orbit, orientation, ortho and fluent controls', function()
        local cam = EasyCam()
        noValues(cam:setAzimuth(0.25))
        noValues(cam:setElevation(0.5))
        assert(approx(cam:getAzimuth(), 0.25) and approx(cam:getElevation(), 0.5))
        local orientation = cam:getOrientation()
        noValues(cam:setOrientation(Quaternion()))
        assert(approx(cam:getAzimuth(), 0))
        noValues(cam:setOrientation(orientation))
        assert(approx(cam:getElevation(), 0.5))
        assert(not cam:getOrtho())
        noValues(cam:enableOrtho())
        assert(cam:getOrtho())
        noValues(cam:disableOrtho())
        assert(not cam:getOrtho())
        noValues(cam:setOrtho(true))
        assert(cam:getOrtho())
        assert(cam:getOrbitButton() == 0 and cam:getPanButton() == 2)
        assert(cam:getDragModifier() == EasyCamModifier.None)
        local chained = cam:setOrbitButton(1):setPanButton(0):setDragModifier(EasyCamModifier.Shift)
        assert(chained:getOrbitButton() == 1 and cam:getOrbitButton() == 1)
        assert(cam:getPanButton() == 0 and cam:getDragModifier() == EasyCamModifier.Shift)
        return true
    end},
    {'Pixels crop/resize/halve/mirror and SaveResult', function()
        local p = Pixels()
        checkPixels(p)
        local result = p:save(bindingTestTemp .. '/pixels.png')
        assert(type(result:ok()) == 'boolean' and result:ok())
        return true
    end},
    {'Image/Pixels crop clamps coordinates beyond INT_MAX without wrapping', function()
        for _, make in ipairs({Pixels, Image}) do
            local p = make()
            p:allocate(2, 1, 4)
            p:setColor(0, 0, Color(1, 0, 0))
            p:setColor(1, 0, Color(0, 1, 0))
            p:crop(2147483647, 0, 2, 1)
            assert(p:getWidth() == 2 and p:getHeight() == 1)
            assert(p:getColor(0, 0).g == 1 and p:getColor(1, 0).g == 1)
        end
        return true
    end},
    {'Image crop/resize/halve/mirror and debug name', function()
        local image = Image()
        noValues(image:setDebugName('lua image'))
        assert(image:getDebugName() == 'lua image')
        checkPixels(image)
        local result = image:save(bindingTestTemp .. '/image.png')
        assert(type(result:ok()) == 'boolean' and result:ok())
        return true
    end},
    {'Fbo debug name', function()
        local fbo = Fbo()
        noValues(fbo:setDebugName('lua fbo'))
        assert(fbo:getDebugName() == 'lua fbo')
        return true
    end},
    {'SoundBuffer load/loadFlac, paths and mutable sample fields', function()
        local b = SoundBuffer()
        assert(b.numSamples == 0 and #b.samples == 0 and b:getPath() == '')
        for _, method in ipairs({'load', 'loadFlac'}) do
            local result = b[method](b, bindingTestSource .. '/data/samples.flac')
            assert(type(result:ok()) == 'boolean' and result:ok())
            assert(b.numSamples == 8 and #b.samples == 8)
            assert(b:getPath():find('samples.flac', 1, true))
            assert(approx(b.samples[2], 0.25))
        end
        b.samples[2] = -0.5
        assert(b.samples[2] == -0.5)
        b.numSamples = 4
        assert(b.numSamples == 4)
        for _, method in ipairs({'load', 'loadFlac'}) do
            local result = b[method](b, bindingTestTemp .. '/missing.flac')
            assert(type(result:ok()) == 'boolean' and not result:ok())
            assert(type(result.message) == 'string' and b:getPath() == '')
        end
        return true
    end},
    {'SoundBuffer sample view retains its owner through GC', function()
        local weak = setmetatable({}, {__mode = 'v'})
        local samples
        do
            local buffer = SoundBuffer()
            assert(buffer:loadFlac(bindingTestSource .. '/data/samples.flac'):ok())
            weak.buffer = buffer
            samples = buffer.samples
        end
        collectTwice()
        assert(weak.buffer ~= nil and #samples == 8 and approx(samples[2], 0.25))
        samples[2] = -0.5
        assert(weak.buffer.samples[2] == -0.5)
        -- Replacing the vector preserves the retained view of the member.
        weak.buffer.samples = SoundBuffer().samples
        assert(#samples == 0)
        samples = nil
        collectTwice()
        assert(weak.buffer == nil)
        return true
    end},
    {'EasyCam fluent methods return the owning Lua self through GC', function()
        for _, control in ipairs({
            {'setOrbitButton', 'getOrbitButton', 1},
            {'setPanButton', 'getPanButton', 0},
            {'setDragModifier', 'getDragModifier', EasyCamModifier.Shift}
        }) do
            local weak = setmetatable({}, {__mode = 'v'})
            local chained
            do
                local cam = EasyCam()
                weak.cam = cam
                chained = cam[control[1]](cam, control[3])
                assert(rawequal(chained, cam))
            end
            collectTwice()
            assert(weak.cam ~= nil and chained[control[2]](chained) == control[3])
            chained = nil
            collectTwice()
            assert(weak.cam == nil)
        end
        local cam = EasyCam():setOrbitButton(1):setPanButton(0):setDragModifier(EasyCamModifier.Shift)
        collectTwice()
        assert(cam:getOrbitButton() == 1 and cam:getPanButton() == 0)
        assert(cam:getDragModifier() == EasyCamModifier.Shift)
        return true
    end},
    {'Playing sound entries own their values after snapshot GC', function()
        if not bindingTestNativeAudio then return true end
        local engine = AudioEngine.getInstance()
        local settings = AudioSettings.new()
        settings.backend = AudioBackend.Null
        assert(engine:setup(settings))
        local sound = Sound.new()
        sound:loadTestTone(440, 1)
        sound:setLoop(true)
        assert(sound:play())
        local weak = setmetatable({}, {__mode = 'v'})
        local voice
        do
            local playing = engine:getPlayingSounds()
            assert(type(playing) == 'table' and #playing == 1)
            weak.playing = playing
            voice = playing[1]
            voice.volume = 0.314
        end
        collectTwice()
        assert(weak.playing == nil and approx(voice.volume, 0.314))
        local replacement = engine:getPlayingSounds()
        replacement[1].volume = 9
        assert(approx(voice.volume, 0.314) and voice.loop and voice.path == '')
        sound:stop()
        engine:close()
        collectTwice()
        assert(approx(voice.volume, 0.314))
        return true
    end},
    {'AudioEngine setup overloads, state, diagnostics and MicInput control', function()
        if not bindingTestNativeAudio then return true end
        local engine = AudioEngine.getInstance()
        noValues(engine:close())
        assert(not engine:isConnected() and not engine:isInitialized())
        -- The no-arg overload opens the system default; failure is allowed.
        local opened = engine:setup()
        assert(type(opened) == 'boolean' and opened == engine:isConnected())
        noValues(engine:close())
        local settings = AudioSettings.new()
        settings.backend = AudioBackend.Null
        settings.sampleRate, settings.channels = 32000, 1
        settings.bufferSize, settings.maxPolyphony = 256, 3
        assert(engine:setup(settings) == true)
        assert(engine:isConnected() and engine:isInitialized())
        assert(engine:getSampleRate() == 32000 and engine:getChannels() == 1)
        assert(engine:getBufferSize() == 256 and engine:getMaxPolyphony() == 3)
        assert(#engine:getPlayingSounds() == 0)
        local sound = Sound.new()
        sound:loadTestTone(440, 1)
        sound:setLoop(true)
        assert(sound:play())
        local playing = engine:getPlayingSounds()
        assert(#playing == 1 and playing[1].loop and playing[1].path == '')
        assert(type(playing[1].volume) == 'number')
        sound:stop()
        local stats = engine:getStats()
        assert(type(stats.droppedPlays) == 'number' and type(stats.peak) == 'number')
        local mic = MicInput()
        mic:setDevice(MicInput.listDevices(AudioBackend.Null)[1])
        assert(not mic:isConnected() and not mic:isStarting() and mic:getDeviceName() == '')
        local started = mic:setup()
        assert(type(started) == 'boolean' and started and mic:isConnected())
        assert(mic:getDeviceName() == 'NULL Capture Device')
        noValues(mic:close())
        assert(not mic:isConnected() and mic:getDeviceName() == '')
        noValues(mic:startAsync())
        assert(mic:getSampleRate() == 48000)
        assert(type(mic:isStarting()) == 'boolean')
        noValues(mic:close())
        assert(not mic:isStarting() and not mic:isConnected())
        noValues(mic:startAsync(16000))
        assert(mic:getSampleRate() == 16000)
        noValues(mic:close())
        assert(not mic:isStarting())
        noValues(engine:close())
        assert(not engine:isInitialized() and not engine:isConnected())
        return true
    end},
    {'Shader.drawVertices default and explicit instance count (unloaded)', function()
        local shader = Shader()
        noValues(shader:drawVertices(3))
        noValues(shader:drawVertices(3, 2))
        assert(not shader:isLoaded())
        return true
    end},
    {'NEVER and HOLD entries stay absent', function()
        assert(AudioEngine.audioIn == nil and AudioEngine.audioOut == nil)
        assert(AudioEngine.waitForAudioCallbacks == nil)
        assert(Texture.getViewForMip == nil and Texture.getAttachmentViewForMip == nil)
        assert(Texture.drawFlippedY == nil and Fbo.lifetimeToken == nil)
        return true
    end}
}
