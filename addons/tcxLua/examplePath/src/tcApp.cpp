#include "tcApp.h"

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    std::string setupLuaSource = R"LUA(
        N = 12
        R = 150

        function mod(n, d)
            assert(d ~= 0)
            r = math.fmod(n, d)
            if r == 0 or n * d >= 0 then
                return r
            else
                return r + d
            end
        end
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
            function drawShape(p, d0)
                clear(0.12, 1.0)

                path = Path.new()

                local v1 = Vec2.new(R, 0)
                local v2 = Vec2.new(R * 0.5, 0)
                for i = 1, N do
                    local d = deg2rad((360 / N) * i)
                    if mod(i, 2) == 0 then
                        path:lineTo(p + v1:rotated(d0 + d))
                    else
                        path:lineTo(p + v2:rotated(d0 + d))
                    end
                end
                path:close()

                path:draw()
            end

            function draw()
                local t = getElapsedTime()
                local d0 = t * 3.0;
                local x = getMouseX()
                local y = getMouseY()
                local p = Vec2.new(x, y)

                setColor(colors.gray)
                drawShape(Vec2.new(300, 300), d0)

                setColor(colors.white)
                drawShape(p, d0)
            end
        )LUA";

        lua->script(drawLuaSource);

        isFirstDraw = false;
    }

    lua->script("draw()");
    // ((*lua)["draw"])();
}

void tcApp::keyPressed(int key) {}
void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(Vec2 pos, int button) {}
void tcApp::mouseReleased(Vec2 pos, int button) {}
void tcApp::mouseMoved(Vec2 pos) {}
void tcApp::mouseDragged(Vec2 pos, int button) {}
void tcApp::mouseScrolled(Vec2 delta) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
