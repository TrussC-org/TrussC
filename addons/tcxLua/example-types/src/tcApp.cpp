#include "tcApp.h"

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    std::string setupLuaSource = R"LUA(
        function strq(q)
            return "Quaternion(".. q.w .. ", ".. q.x .. ", ".. q.y .. ", ".. q.z .. ")"
        end

        function strv2(v)
            return "Vec2(".. v.x .. ", ".. v.y .. ")"
        end

        function strv3(v)
            return "Vec3(".. v.x .. ", ".. v.y .. ", ".. v.z .. ")"
        end

        function strc(c)
            return "Color(".. c.r .. ", ".. c.g .. ", ".. c.b .. ", ".. c.a .. ")"
        end

        local c = Color.new(1.0, 0.0, 0.0)
        print("c = " .. strc(c))
        print("yellow = " .. strc(colors.yellow))
        print("magenta = " .. strc(colors.magenta))

        local v1 = Vec2.new(1.0, 2.0)
        local v2 = Vec2.new(3.0, 4.0)
        print("v1 = " .. strv2(v1))
        print("v2 = " .. strv2(v2))
        print("v2:distance(v1) = " .. v2:distance(v1))
        print("v1:angle() = " .. v1:angle())
        print("v1:angle(v2) = " .. v1:angle(v2))
        print("v1 + v2 = " .. strv2(v1 + v2))
        print("v1 - v2 = " .. strv2(v1 - v2))
        print("v1.x = 5.0")
        v1.x = 5.0
        print("v1 = " .. strv2(v1))

        local quat = Quaternion.new(1.0, 2.0, 3.0, 4.0)
        print("quat = " .. strq(quat))

        local quat_ident = Quaternion.identity()
        print("quat_ident = " .. strq(quat_ident))

        local quat_from_euler = Quaternion.fromEuler(1.0, 2.0, 3.0)
        print("quat_from_euler = " .. strq(quat_from_euler))
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

                -- Rotating box
                noFill()
                p = Vec3.new(getWindowWidth() / 2, getWindowHeight() / 2, 0)
                translate(p)
                rotate(getElapsedTimef() * 0.1, getElapsedTimef() * 0.15, 0);
                drawBox(200.0)
            end
        )LUA";

        lua->script(drawLuaSource);

        isFirstDraw = false;
    }

    // lua->script("draw()");
    ((*lua)["draw"])();
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
