#include "tcApp.h"

#include "sol/sol.hpp"

void tcApp::setup() {
    lua.open_libraries(sol::lib::base);

    fbo.allocate(getWidth(), getHeight());

    reset();
}

void tcApp::reset() {
    x = getWidth() / 2;
    y = getHeight() / 2;

    script = "x + 1, y + 1";
}

void tcApp::updateFbo() {
    fbo.begin();
    clear(0, 0);
    setColor(colors::green);
    drawBitmapString("x, y = " + script, 0, 0);
    fbo.end();
}

void tcApp::update() {

    updateFbo();

    lua["x"] = x;
    lua["y"] = y;

    try{
        sol::optional<sol::error> result = lua.safe_script("x, y = " + script);
        // if (result.has_value()) {
        //     std::cerr << "Lua execution failed: "
        //             << result.value().what() << std::endl;
        // }
    }catch(const std::exception& e){
        
    }

    auto&& _x = lua["x"];
    auto&& _y = lua["y"];

    if(_x.get_type() == sol::type::number){
        x = _x.get<float>();
    }

    if(_y.get_type() == sol::type::number){
        y = _y.get<float>();
    }

    if(x > getWidth()){
        x = fmodf(x, getWidth());
    }
    if(y > getHeight()){
        y = fmodf(y, getHeight());
    }
    if(x < 0){
        x = getWidth() - fmodf(fabsf(x), getWidth());
    }
    if(y < 0){
        y = getHeight() - fmodf(fabs(y), getHeight());
    }
}

void tcApp::draw() {
    clear(0.0f);

    setColor(colors::white);
    drawCircle(x, y, 20);

    pushMatrix();
    translate(20, 20);
    scale(3);
    setColor(colors::white);
    fbo.draw(0, 0);
    popMatrix();

    setColor(colors::white);
    drawBitmapString("Type anykey! ( Type [R] to reset )", 20, getHeight() - 50);
    drawBitmapString("[M] = insert multiply", 20, getHeight() - 30);
}

void tcApp::keyPressed(int key) {
    if(key == KEY_BACKSPACE){
        if(script.size() > 0){
            script.pop_back();
        }
    }else if(key == 'r' || key == 'R'){
        reset();
    }else if('a' <= key && key <= 'z'){
        
    }else if('A' <= key && key <= 'A'){

    }else if(
        ('0' <= key && key <= '9')
        || key == '.' || key == ',' || key == ' ' || key == '='
        || key == ';' || key == ':'
        || key == '/' || key == '*'
        || key == '-' || key == '+'
        || key == 'x' || key == 'y'
        || key == 'X' || key == 'Y'
        || key == 'm' || key == 'M'){

        if(key == '='){
            key = '+';
        }else if(key == ';'){
            key = '+';
        }else if(key == ':'){
            key = '+';
        }else if(key == 'X'){
            key = 'x';
        }else if(key == 'Y'){
            key = 'y';
        }else if(key == 'm' || key == 'M'){
            key = '*';
        }

        char s[2] = {static_cast<char>(key), '\0'};
        script = script + std::string(s);
    }
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
