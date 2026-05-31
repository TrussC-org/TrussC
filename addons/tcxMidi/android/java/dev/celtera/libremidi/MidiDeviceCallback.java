// =============================================================================
// MidiDeviceCallback.java - Java glue required by libremidi's Android (AMidi)
// backend. libremidi opens MIDI devices through the Java MidiManager and calls
// back into native code via this class's onDeviceOpened() native method (the
// JNI impl, Java_dev_celtera_libremidi_MidiDeviceCallback_onDeviceOpened, lives
// in libremidi's compiled .so). The package/class name MUST stay exactly
// dev.celtera.libremidi.MidiDeviceCallback or the native lookup fails.
//
// Bundled by TrussC's addon Java aggregation (tcxMidi/android/java/**). Vendored
// verbatim from libremidi (BSD-2-Clause, https://github.com/celtera/libremidi).
// =============================================================================
package dev.celtera.libremidi;

import android.media.midi.MidiDevice;
import android.media.midi.MidiManager;
import android.util.Log;

public class MidiDeviceCallback implements MidiManager.OnDeviceOpenedListener {
    private static final String TAG = "libremidi";
    private long nativePtr;
    private boolean isOutput;

    public MidiDeviceCallback(long ptr, boolean output) {
        nativePtr = ptr;
        isOutput = output;
    }

    @Override
    public void onDeviceOpened(MidiDevice device) {
        if (device == null) {
            Log.e(TAG, "Failed to open MIDI device");
            return;
        }
        Log.i(TAG, "MIDI device opened successfully");
        onDeviceOpened(device, nativePtr, isOutput);
    }

    // Native method - implemented in libremidi's native library.
    private native void onDeviceOpened(MidiDevice device, long targetPtr, boolean isOutput);
}
