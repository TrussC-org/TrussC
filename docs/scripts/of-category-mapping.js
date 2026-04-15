/**
 * Category mapping from api-definition.yaml categories to oF comparison page categories
 *
 * YAML has 35+ fine-grained categories, but the oF comparison page uses ~23 broader categories.
 * This mapping defines how to group them.
 */

const categoryMapping = {
    // App Structure
    "lifecycle": { id: "app", name: "App Structure", name_ja: "アプリ構造", name_ko: "앱 구조", order: 1 },
    "events": { id: "app", name: "App Structure", name_ja: "アプリ構造", name_ko: "앱 구조", order: 1 },
    "window_input": { id: "app", name: "App Structure", name_ja: "アプリ構造", name_ko: "앱 구조", order: 1 },
    "window_system": { id: "app", name: "App Structure", name_ja: "アプリ構造", name_ko: "앱 구조", order: 1 },

    // Graphics
    "graphics_color": { id: "graphics", name: "Graphics", name_ja: "グラフィックス", name_ko: "그래픽스", order: 2 },
    "graphics_shapes": { id: "graphics", name: "Graphics", name_ja: "グラフィックス", name_ko: "그래픽스", order: 2 },
    "graphics_style": { id: "graphics", name: "Graphics", name_ja: "グラフィックス", name_ko: "그래픽스", order: 2 },
    "graphics_advanced": { id: "graphics", name: "Graphics", name_ja: "グラフィックス", name_ko: "그래픽스", order: 2 },

    // Transform
    "transform": { id: "transform", name: "Transform", name_ja: "変換", name_ko: "좌표변환", order: 3 },

    // Math
    "math_random": { id: "math", name: "Math", name_ja: "数学", name_ko: "수학", order: 4 },
    "math_interpolation": { id: "math", name: "Math", name_ja: "数学", name_ko: "수학", order: 4 },
    "math_trig": { id: "math", name: "Math", name_ja: "数学", name_ko: "수학", order: 4 },
    "math_general": { id: "math", name: "Math", name_ja: "数学", name_ko: "수학", order: 4 },
    "math_geometry": { id: "math", name: "Math", name_ja: "数学", name_ko: "수학", order: 4 },
    "math_3d": { id: "math", name: "Math", name_ja: "数学", name_ko: "수학", order: 4 },

    // Types (Vec, Color, Rect)
    "types_vec2": { id: "types", name: "Types", name_ja: "型", name_ko: "타입", order: 5 },
    "types_vec3": { id: "types", name: "Types", name_ja: "型", name_ko: "타입", order: 5 },
    "types_color": { id: "color", name: "Color", name_ja: "色", name_ko: "색상", order: 6 },
    "types_rect": { id: "types", name: "Types", name_ja: "型", name_ko: "타입", order: 5 },

    // Image & Texture
    "graphics_texture": { id: "image", name: "Image", name_ja: "画像", name_ko: "이미지", order: 7 },
    "graphics_pixels": { id: "image", name: "Image", name_ja: "画像", name_ko: "이미지", order: 7 },

    // Font - no direct YAML category yet, will be added

    // FBO
    "graphics_fbo": { id: "fbo", name: "FBO", name_ja: "FBO", name_ko: "FBO", order: 8 },

    // Shader
    "graphics_shader": { id: "shader", name: "Shader", name_ja: "シェーダー", name_ko: "셰이더", order: 9 },

    // Mesh
    "types_mesh": { id: "mesh", name: "Mesh", name_ja: "メッシュ", name_ko: "메쉬", order: 10 },
    "types_polyline": { id: "mesh", name: "Mesh", name_ja: "メッシュ", name_ko: "메쉬", order: 10 },
    "types_strokemesh": { id: "mesh", name: "Mesh", name_ja: "メッシュ", name_ko: "메쉬", order: 10 },

    // 3D Primitives
    "graphics_3d_setup": { id: "3d-primitives", name: "3D Primitives", name_ja: "3Dプリミティブ", name_ko: "3D Primitives", order: 11 },

    // 3D Camera
    "graphics_camera": { id: "3d-camera", name: "3D Camera", name_ja: "3Dカメラ", name_ko: "3D 카메라", order: 12 },

    // Lighting (not yet in YAML)

    // Sound
    "sound": { id: "sound", name: "Sound", name_ja: "サウンド", name_ko: "사운드", order: 13 },

    // Video (not yet in YAML)

    // GUI (not yet in YAML - uses ImGui)

    // I/O (not yet in YAML)

    // Network (not yet in YAML)

    // Scene Graph
    "scene_graph": { id: "scene", name: "Scene Graph", name_ja: "シーングラフ", name_ko: "씬 그래프", order: 14 },

    // Events (already in app)

    // Log (not yet in YAML)

    // Thread (not yet in YAML)

    // Serial (not yet in YAML)

    // Timer
    "animation": { id: "timer", name: "Timer", name_ja: "タイマー", name_ko: "타이머", order: 15 },

    // Time
    "time_frame": { id: "time", name: "Time", name_ja: "時間", name_ko: "시간", order: 16 },
    "time_elapsed": { id: "time", name: "Time", name_ja: "時間", name_ko: "시간", order: 16 },
    "time_system": { id: "time", name: "Time", name_ja: "時間", name_ko: "시간", order: 16 },
    "time_current": { id: "time", name: "Time", name_ja: "時間", name_ko: "시간", order: 16 },

    // Utility
    "utility": { id: "utility", name: "Utility", name_ja: "ユーティリティ", name_ko: "유틸리티", order: 17 },

    // Addons
    "addon_tcxlut": { id: "addons", name: "Addons", name_ja: "アドオン", name_ko: "애드온", order: 99 },
};

// Type category mapping for of-mapping.json types section
const typeCategoryMapping = {
    "math-vector": { id: "math-vector", name: "Math / Vector", name_ja: "数学 / ベクトル", name_ko: "수학 / 벡터", order: 1 },
    "color-types": { id: "color-types", name: "Color", name_ja: "カラー", name_ko: "색상", order: 2 },
    "geometry-types": { id: "geometry-types", name: "Geometry", name_ja: "ジオメトリ", name_ko: "지오메트리", order: 3 },
    "image-texture-types": { id: "image-texture-types", name: "Image / Texture", name_ja: "画像 / テクスチャ", name_ko: "이미지 / 텍스처", order: 4 },
    "audio-types": { id: "audio-types", name: "Audio", name_ja: "オーディオ", name_ko: "오디오", order: 5 },
    "text-types": { id: "text-types", name: "Text", name_ja: "テキスト", name_ko: "텍스트", order: 6 },
    "file-io-types": { id: "file-io-types", name: "File I/O", name_ja: "ファイルI/O", name_ko: "파일 I/O", order: 7 },
    "camera-types": { id: "camera-types", name: "3D Camera", name_ja: "3Dカメラ", name_ko: "3D 카메라", order: 8 },
};

// Additional entries that exist in oF but not yet in TrussC YAML
// These will be added to the mapping JSON with of_equivalent but no tc equivalent
const ofOnlyEntries = [
    // Font
    { category: "font", name: "Font", name_ja: "フォント", name_ko: "서체", order: 7.5, entries: [
        { of: "ofTrueTypeFont", tc: "Font", notes: "", notes_ja: "", notes_ko: "" },
        { of: "font.load(\"font.ttf\", size)", tc: "font.load(\"font.ttf\", size)", notes: "Same", notes_ja: "Same", notes_ko: "" },
        { of: "font.drawString(text, x, y)", tc: "font.drawString(text, x, y)", notes: "Same", notes_ja: "Same", notes_ko: "" },
    ]},
    // Video
    { category: "video", name: "Video", name_ja: "ビデオ", name_ko: "비디오", order: 13.5, entries: [
        { of: "ofVideoGrabber", tc: "VideoGrabber", notes: "", notes_ja: "", notes_ko: "" },
        { of: "grabber.setup(w, h)", tc: "grabber.setup(w, h)", notes: "", notes_ja: "", notes_ko: "" },
        { of: "grabber.update()", tc: "grabber.update()", notes: "", notes_ja: "", notes_ko: "" },
        { of: "grabber.draw(x, y)", tc: "grabber.draw(x, y)", notes: "", notes_ja: "", notes_ko: "" },
        { of: "grabber.isFrameNew()", tc: "grabber.isFrameNew()", notes: "", notes_ja: "", notes_ko: "" },
    ]},
    // GUI
    { category: "gui", name: "GUI", name_ja: "GUI", name_ko: "GUI", order: 18, entries: [
        { of: "ofxGui / ofxImGui", tc: "ImGui (built-in)", notes: "Included by default", notes_ja: "Included by default", notes_ko: "" },
    ]},
    // I/O
    { category: "io", name: "I/O", name_ja: "I/O", name_ko: "I/O", order: 19, entries: [
        { of: "ofSystemLoadDialog()", tc: "systemLoadDialog()", notes: "", notes_ja: "", notes_ko: "" },
        { of: "ofSystemSaveDialog()", tc: "systemSaveDialog()", notes: "", notes_ja: "", notes_ko: "" },
        { of: "ofLoadJson(path)", tc: "loadJson(path)", notes: "nlohmann/json", notes_ja: "nlohmann/json", notes_ko: "" },
        { of: "-", tc: "loadXml(path)", notes: "pugixml", notes_ja: "pugixml", notes_ko: "" },
    ]},
    // Network
    { category: "network", name: "Network", name_ja: "ネットワーク", name_ko: "네트워크", order: 20, entries: [
        { of: "ofxTCPClient", tc: "TcpClient", notes: "", notes_ja: "", notes_ko: "" },
        { of: "ofxTCPServer", tc: "TcpServer", notes: "", notes_ja: "", notes_ko: "" },
        { of: "ofxUDPManager", tc: "UdpSocket", notes: "", notes_ja: "", notes_ko: "" },
        { of: "ofxOsc", tc: "OscReceiver / OscSender", notes: "", notes_ja: "", notes_ko: "" },
    ]},
    // Log
    { category: "log", name: "Log", name_ja: "ログ", name_ko: "로그", order: 21, entries: [
        { of: "ofLog()", tc: "logNotice()", notes: "", notes_ja: "", notes_ko: "" },
        { of: "ofLogWarning()", tc: "logWarning()", notes: "", notes_ja: "", notes_ko: "" },
        { of: "ofLogError()", tc: "logError()", notes: "", notes_ja: "", notes_ko: "" },
    ]},
    // Thread
    { category: "thread", name: "Thread", name_ja: "スレッド", name_ko: "스레드", order: 22, entries: [
        { of: "ofThread", tc: "std::thread + MainThreadRunner", notes: "Safe sync", notes_ja: "Safe sync", notes_ko: "" },
        { of: "-", tc: "MainThreadRunner::run(func)", notes: "Execute on main thread", notes_ja: "Execute on main thread", notes_ko: "" },
    ]},
    // Serial
    { category: "serial", name: "Serial", name_ja: "シリアル", name_ko: "시리얼통신", order: 23, entries: [
        { of: "ofSerial", tc: "Serial", notes: "", notes_ja: "", notes_ko: "" },
        { of: "serial.setup(port, baud)", tc: "serial.setup(port, baud)", notes: "Same", notes_ja: "Same", notes_ko: "" },
        { of: "serial.readBytes(...)", tc: "serial.readBytes(...)", notes: "Same", notes_ja: "Same", notes_ko: "" },
        { of: "serial.writeBytes(...)", tc: "serial.writeBytes(...)", notes: "Same", notes_ja: "Same", notes_ko: "" },
    ]},
];

module.exports = { categoryMapping, typeCategoryMapping, ofOnlyEntries };
