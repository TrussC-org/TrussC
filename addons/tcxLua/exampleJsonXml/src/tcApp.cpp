#include "tcApp.h"

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    std::string setupLuaSource = R"LUA(
        setWindowTitle("jsonXmlExample")
        tcSetConsoleLogLevel(LogLevel.Verbose)

        messages = {}

        function addMessage(msg)
            table.insert(messages, msg)
            -- Remove old messages to fit on screen
            while #messages > 35 do
                table.remove(messages, 1)
            end
        end

        function testJson()
            addMessage("--- JSON Test ---")

            -- Create JSON
            local j = Json.new()
            j["name"] = "TrussC"
            -- print(j["name"])
            j["version"] = 0.1
            -- print(j["version"])
            j["features"] = {"graphics", "audio", "events"}

            local settings = Json.new() -- WORKAROUND
            settings["width"] = 1024
            settings["height"] = 768
            settings["fullscreen"] = false
            j["settings"] = settings

            -- Convert to string
            local jsonStr = toJsonString(j, 2)
            addMessage("Created JSON:")

            -- Display each line
            for line in jsonStr:gmatch("([^\n]*)\n?") do
                addMessage("  " .. line)
            end

            -- Save to file
            -- local path = "/tmp/trussc_test.json"
            local path = "trussc_test.json"
            if saveJson(j, path) then
                addMessage("Saved to: " .. path)
            end

            -- Load from file
            local loaded = loadJson(path)
            if not loaded:empty() then
                addMessage("Loaded back:")
                addMessage("  name: " .. loaded["name"]:get_string())
                addMessage("  version: " .. loaded["version"]:get_double())
                addMessage("  features count: " .. loaded["features"]:size())
            end

            addMessage("")
        end

        function testXml()
            addMessage("--- XML Test ---")

            -- Create XML
            local xml = Xml.new()
            xml:addDeclaration()

            local root = xml:addRoot("project")
            root:append_attribute("name"):set("TrussC")

            local info = root:append_child("info")
            info:append_child("version"):text():set("0.1")
            info:append_child("author"):text():set("TrussC Team")

            local features = root:append_child("features")
            features:append_child("feature"):text():set("graphics")
            features:append_child("feature"):text():set("audio")
            features:append_child("feature"):text():set("events")

            -- Convert to string
            local xmlStr = xml:toString()
            addMessage("Created XML:")

            -- Display each line
            for line in xmlStr:gmatch("([^\n]*)\n?") do
                addMessage("  " .. line)
            end

            -- Save to file
            local path = "/tmp/trussc_test.xml"
            if xml:save(path) then
                addMessage("Saved to: " .. path)
            end

            -- Load from file
            local loaded = loadXml(path)
            if not loaded:empty() then
                addMessage("Loaded back:")
                local loadedRoot = loaded:root()
                addMessage("  project name: " .. loadedRoot:attribute("name"):value())
                addMessage("  version: " .. loadedRoot:child("info"):child("version"):text():get())

                local children = loadedRoot:child("features"):children("feature")
                addMessage(" features count = " .. #children)
                for k, v in ipairs(children) do
                    addMessage(" - feature " .. v:text():get())
                end
            end

            addMessage("")
        end

        addMessage("=== JSON/XML Example ===")
        addMessage("")
        addMessage("Press 'j' to test JSON")
        addMessage("Press 'x' to test XML")
        addMessage("")
    )LUA";

    sol::optional<sol::error> result = lua->safe_script(setupLuaSource);
	if (result.has_value()) {
		std::cerr << "Lua execution failed: "
		          << result.value().what() << std::endl;
	}
}

void tcApp::update() {

}

void tcApp::draw() {
    if(isFirstDraw){
        std::string drawLuaSource = R"LUA(
            function draw()
                clear(0.12, 1.0)

                setColor(1.0, 1.0)
                local y = 20
                for k, msg in ipairs(messages) do
                    drawBitmapString(msg, 20, y, true)
                    y = y + 15
                end
            end
        )LUA";

        lua->script(drawLuaSource);

        isFirstDraw = false;
    }

    // lua->script("draw()");
    ((*lua)["draw"])();
}

void tcApp::keyPressed(int key) {
    if(isFirstKey){
        std::string drawKeySource = R"LUA(
            function keyPressed(key)
                local c = string.char(key)
                -- print("keyPressed: " .. c)
                if c == "j" or c == "J" then
                    testJson()
                elseif c == "x" or c == "X" then
                    testXml()
                end
            end
        )LUA";

        lua->script(drawKeySource);

        isFirstKey = false;
    }

    lua->script("keyPressed(" + std::to_string(key) + ")");
    // ((*lua)["keyPressed"])(key);
}

void tcApp::addMessage(const std::string& msg){
    ((*lua)["addMessage"])(msg);
}

void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(const MouseEventArgs& e) {}
void tcApp::mouseReleased(const MouseEventArgs& e) {}
void tcApp::mouseMoved(const MouseMoveEventArgs& e) {}
void tcApp::mouseDragged(const MouseDragEventArgs& e) {}
void tcApp::mouseScrolled(const ScrollEventArgs& e) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
