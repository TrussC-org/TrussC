#include "tcApp.h"

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    std::string setupLuaSource = R"LUA(
        -- NOTE: TweenFloat, TweenVec2, TweenVec3, TweenColor can be used

        tweens = {
            TweenFloat.new(0, 1, 1.0, EaseType.Linear, EaseMode.Out),
            TweenFloat.new(0, 1, 1.0, EaseType.Sine, EaseMode.Out),
            TweenFloat.new(0, 1, 1.0, EaseType.Cubic, EaseMode.Out),
            TweenFloat.new(0, 1, 1.0, EaseType.Bounce, EaseMode.Out)
        }

        for _, tween in ipairs(tweens) do
            tween:loop(-1)
            tween:start()
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
            function draw()
                clear(0.12, 1.0)

                local O = 100
                local D = 300

                local i = 0
                for _, tween in ipairs(tweens) do
                    v = tween:getValue()
                    drawCircle(v * D + O, 50 * i + 100, 20)
                    i = i + 1
                end
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
