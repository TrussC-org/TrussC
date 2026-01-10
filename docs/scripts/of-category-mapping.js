/**
 * Category mapping from api-definition.yaml categories to oF comparison page categories
 *
 * YAML has 35+ fine-grained categories, but the oF comparison page uses ~23 broader categories.
 * This mapping defines how to group them.
 */

const categoryMapping = {
    // App Structure
    "lifecycle": { id: "app", name: "App Structure", name_ja: "アプリ構造", order: 1 },
    "events": { id: "app", name: "App Structure", name_ja: "アプリ構造", order: 1 },
    "window_input": { id: "app", name: "App Structure", name_ja: "アプリ構造", order: 1 },
    "window_system": { id: "app", name: "App Structure", name_ja: "アプリ構造", order: 1 },

    // Graphics
    "graphics_color": { id: "graphics", name: "Graphics", name_ja: "グラフィックス", order: 2 },
    "graphics_shapes": { id: "graphics", name: "Graphics", name_ja: "グラフィックス", order: 2 },
    "graphics_style": { id: "graphics", name: "Graphics", name_ja: "グラフィックス", order: 2 },
    "graphics_advanced": { id: "graphics", name: "Graphics", name_ja: "グラフィックス", order: 2 },

    // Transform
    "transform": { id: "transform", name: "Transform", name_ja: "変換", order: 3 },

    // Math
    "math_random": { id: "math", name: "Math", name_ja: "数学", order: 4 },
    "math_interpolation": { id: "math", name: "Math", name_ja: "数学", order: 4 },
    "math_trig": { id: "math", name: "Math", name_ja: "数学", order: 4 },
    "math_general": { id: "math", name: "Math", name_ja: "数学", order: 4 },
    "math_geometry": { id: "math", name: "Math", name_ja: "数学", order: 4 },
    "math_3d": { id: "math", name: "Math", name_ja: "数学", order: 4 },

    // Types (Vec, Color, Rect)
    "types_vec2": { id: "types", name: "Types", name_ja: "型", order: 5 },
    "types_vec3": { id: "types", name: "Types", name_ja: "型", order: 5 },
    "types_color": { id: "color", name: "Color", name_ja: "色", order: 6 },
    "types_rect": { id: "types", name: "Types", name_ja: "型", order: 5 },

    // Image & Texture
    "graphics_texture": { id: "image", name: "Image", name_ja: "画像", order: 7 },
    "graphics_pixels": { id: "image", name: "Image", name_ja: "画像", order: 7 },

    // Font - no direct YAML category yet, will be added

    // FBO
    "graphics_fbo": { id: "fbo", name: "FBO", name_ja: "FBO", order: 8 },

    // Shader
    "graphics_shader": { id: "shader", name: "Shader", name_ja: "シェーダー", order: 9 },

    // Mesh
    "types_mesh": { id: "mesh", name: "Mesh", name_ja: "メッシュ", order: 10 },
    "types_polyline": { id: "mesh", name: "Mesh", name_ja: "メッシュ", order: 10 },
    "types_strokemesh": { id: "mesh", name: "Mesh", name_ja: "メッシュ", order: 10 },

    // 3D Primitives
    "graphics_3d_setup": { id: "3d-primitives", name: "3D Primitives", name_ja: "3Dプリミティブ", order: 11 },

    // 3D Camera
    "graphics_camera": { id: "3d-camera", name: "3D Camera", name_ja: "3Dカメラ", order: 12 },

    // Lighting (not yet in YAML)

    // Sound
    "sound": { id: "sound", name: "Sound", name_ja: "サウンド", order: 13 },

    // Video (not yet in YAML)

    // GUI (not yet in YAML - uses ImGui)

    // I/O (not yet in YAML)

    // Network (not yet in YAML)

    // Scene Graph
    "scene_graph": { id: "scene", name: "Scene Graph", name_ja: "シーングラフ", order: 14 },

    // Events (already in app)

    // Log (not yet in YAML)

    // Thread (not yet in YAML)

    // Serial (not yet in YAML)

    // Timer
    "animation": { id: "timer", name: "Timer", name_ja: "タイマー", order: 15 },

    // Time
    "time_frame": { id: "time", name: "Time", name_ja: "時間", order: 16 },
    "time_elapsed": { id: "time", name: "Time", name_ja: "時間", order: 16 },
    "time_system": { id: "time", name: "Time", name_ja: "時間", order: 16 },
    "time_current": { id: "time", name: "Time", name_ja: "時間", order: 16 },

    // Utility
    "utility": { id: "utility", name: "Utility", name_ja: "ユーティリティ", order: 17 },

    // Addons
    "addon_tcxlut": { id: "addons", name: "Addons", name_ja: "アドオン", order: 99 },
};

// Additional entries that exist in oF but not yet in TrussC YAML
// These will be added to the mapping JSON with of_equivalent but no tc equivalent
const ofOnlyEntries = [
    // Font
    { category: "font", name: "Font", name_ja: "フォント", order: 7.5, entries: [
        { of: "ofTrueTypeFont", tc: "Font", notes: "" },
        { of: "font.load(\"font.ttf\", size)", tc: "font.load(\"font.ttf\", size)", notes: "Same" },
        { of: "font.drawString(text, x, y)", tc: "font.drawString(text, x, y)", notes: "Same" },
    ]},
    // Video
    { category: "video", name: "Video", name_ja: "ビデオ", order: 13.5, entries: [
        { of: "ofVideoGrabber", tc: "VideoGrabber", notes: "" },
        { of: "grabber.setup(w, h)", tc: "grabber.setup(w, h)", notes: "" },
        { of: "grabber.update()", tc: "grabber.update()", notes: "" },
        { of: "grabber.draw(x, y)", tc: "grabber.draw(x, y)", notes: "" },
        { of: "grabber.isFrameNew()", tc: "grabber.isFrameNew()", notes: "" },
    ]},
    // GUI
    { category: "gui", name: "GUI", name_ja: "GUI", order: 18, entries: [
        { of: "ofxGui / ofxImGui", tc: "ImGui (built-in)", notes: "Included by default" },
    ]},
    // I/O
    { category: "io", name: "I/O", name_ja: "I/O", order: 19, entries: [
        { of: "ofSystemLoadDialog()", tc: "systemLoadDialog()", notes: "" },
        { of: "ofSystemSaveDialog()", tc: "systemSaveDialog()", notes: "" },
        { of: "ofLoadJson(path)", tc: "loadJson(path)", notes: "nlohmann/json" },
        { of: "-", tc: "loadXml(path)", notes: "pugixml" },
    ]},
    // Network
    { category: "network", name: "Network", name_ja: "ネットワーク", order: 20, entries: [
        { of: "ofxTCPClient", tc: "TcpClient", notes: "" },
        { of: "ofxTCPServer", tc: "TcpServer", notes: "" },
        { of: "ofxUDPManager", tc: "UdpSocket", notes: "" },
        { of: "ofxOsc", tc: "OscReceiver / OscSender", notes: "" },
    ]},
    // Log
    { category: "log", name: "Log", name_ja: "ログ", order: 21, entries: [
        { of: "ofLog()", tc: "logNotice()", notes: "" },
        { of: "ofLogWarning()", tc: "logWarning()", notes: "" },
        { of: "ofLogError()", tc: "logError()", notes: "" },
    ]},
    // Thread
    { category: "thread", name: "Thread", name_ja: "スレッド", order: 22, entries: [
        { of: "ofThread", tc: "std::thread + MainThreadRunner", notes: "Safe sync" },
        { of: "-", tc: "MainThreadRunner::run(func)", notes: "Execute on main thread" },
    ]},
    // Serial
    { category: "serial", name: "Serial", name_ja: "シリアル", order: 23, entries: [
        { of: "ofSerial", tc: "Serial", notes: "" },
        { of: "serial.setup(port, baud)", tc: "serial.setup(port, baud)", notes: "Same" },
        { of: "serial.readBytes(...)", tc: "serial.readBytes(...)", notes: "Same" },
        { of: "serial.writeBytes(...)", tc: "serial.writeBytes(...)", notes: "Same" },
    ]},
];

module.exports = { categoryMapping, ofOnlyEntries };
