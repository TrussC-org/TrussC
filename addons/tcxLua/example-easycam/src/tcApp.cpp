#include "tcApp.h"

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    std::string setupLuaSource = R"LUA(
        setWindowTitle("easyCamExample");

        showHelp = true

        -- Camera initial settings
        cam = EasyCam.new()
        cam:setDistance(600)
        cam:setTarget(0, 0, 0)
        cam:enableMouseInput()  -- Auto-subscribe to mouse events

        -- Generate meshes
        boxMesh = createBox(100)
        sphereMesh = createSphere(50, 24)
        coneMesh = createCone(50, 100, 24)
        cylinderMesh = createCylinder(50, 100, 24)

        -- Light settings (bright ambient for clear visibility)
        light = Light.new()
        light:setDirectional(Vec3.new(-0.7, -1.0, -0.4));  -- Asymmetric angle
        light:setAmbient(0.8, 0.8, 0.85)
        light:setDiffuse(0.6, 0.6, 0.55)
        light:setSpecular(0.6, 0.6, 0.6)

        -- Material settings
        matRed = Material.plastic(Color.new(0.9, 0.2, 0.2), 0.5)
        matOrange = Material.plastic(Color.new(1.0, 0.6, 0.2), 0.5)
        matBlue = Material.plastic(Color.new(0.2, 0.4, 0.9), 0.5)
        matCyan = Material.plastic(Color.new(0.2, 0.8, 0.8), 0.5)
        matYellow = Material.plastic(Color.new(1.0, 0.9, 0.2), 0.5)
        matMagenta = Material.plastic(Color.new(0.9, 0.2, 0.8), 0.5)
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
                clear(0.08, 1.0)

                -- --- 3D drawing (camera enabled) ---
                cam:begin()

                -- Enable lighting
                addLight(light)
                setCameraPosition(cam:getPosition())
                setColor(1.0, 1.0, 1.0, 1.0)

                -- Right: Red cone
                pushMatrix()
                translate(150, 0, 0)
                setMaterial(matRed)
                coneMesh:draw()
                popMatrix()

                -- Left: Orange sphere
                pushMatrix()
                translate(-150, 0, 0)
                setMaterial(matOrange)
                sphereMesh:draw()
                popMatrix()

                -- Bottom: Blue box
                pushMatrix()
                translate(0, 150, 0)
                setMaterial(matBlue)
                boxMesh:draw()
                popMatrix()

                -- Top: Cyan cylinder
                pushMatrix()
                translate(0, -150, 0)
                setMaterial(matCyan)
                cylinderMesh:draw()
                popMatrix()

                -- Front: Yellow box
                pushMatrix()
                translate(0, 0, 150)
                setMaterial(matYellow)
                boxMesh:draw()
                popMatrix()

                -- Back: Magenta box
                pushMatrix()
                translate(0, 0, -150)
                setMaterial(matMagenta)
                boxMesh:draw()
                popMatrix()

                -- End lighting
                clearMaterial()
                clearLights()

                -- Draw grid
                -- setColor(0.4, 0.4, 0.4)
                -- drawGrid(400, 10)

                cam:end_cam()

                -- --- 2D drawing (UI) ---
                setColor(1.0, 1.0)

                if showHelp then
                    local ss = ""
                    ss = ss .. "FPS: " .. math.floor(getFrameRate()) .. "\n"
                    ss = ss .. "MOUSE INPUT: " .. (cam:isMouseInputEnabled() and "ON" or "OFF") .. "\n"
                    ss = ss .. "Distance: " .. math.floor(cam:getDistance()) .. "\n"
                    ss = ss .. "\n"
                    ss = ss .. "Controls:\n"
                    ss = ss .. "  LEFT DRAG: rotate camera\n"
                    ss = ss .. "  MIDDLE DRAG: pan camera\n"
                    ss = ss .. "  SCROLL: zoom in/out\n"
                    ss = ss .. "Keys:\n"
                    ss = ss .. "  c: toggle mouse input\n"
                    ss = ss .. "  r: reset camera\n"
                    ss = ss .. "  f: toggle fullscreen\n"
                    ss = ss .. "  h: toggle this help\n"
                    drawBitmapString(ss, 20, 20, true);
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
                if c == "f" or c == "F" then
                    toggleFullscreen()
                elseif c == "c" or c == "C" then
                    if cam:isMouseInputEnabled() then
                        cam:disableMouseInput()
                    else
                        cam:enableMouseInput()
                    end
                elseif c == "r" or c == "R" then
                    cam:reset()
                    cam:setDistance(600)
                elseif c == "h" or c == "H" then
                    showHelp = not showHelp
                end
            end
        )LUA";

        lua->script(drawKeySource);

        isFirstKey = false;
    }

    // lua->script("keyPressed(" + std::to_string(key) + ")");
    ((*lua)["keyPressed"])(key);

}
void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(Vec2 pos, int button) {}
void tcApp::mouseReleased(Vec2 pos, int button) {}
void tcApp::mouseMoved(Vec2 pos) {}
void tcApp::mouseDragged(Vec2 pos, int button) {}
void tcApp::mouseScrolled(Vec2 delta) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
