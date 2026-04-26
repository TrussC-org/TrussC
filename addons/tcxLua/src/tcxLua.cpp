#include "tcxLua.h"
#include "TrussC.h"

#include "sol/sol.hpp"
using namespace tc;

// WORKAROUND: to support deprecated functions in lua
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif // #ifndef _MSC_VER

// namespace tcx::lua {

bool tcxLua::canUseLuaJITFromSol2(){
    #ifdef TCXLUA_USE_LUAJIT
    return true;
    #else // TCXLUA_USE_LUAJIT
    return false;
    #endif // TCXLUA_USE_LUAJIT
}

std::shared_ptr<sol::state> tcxLua::getLuaState(){
    std::shared_ptr<sol::state> lua = std::make_shared<sol::state>();

    #ifdef TCXLUA_USE_LUAJIT
    lua->open_libraries(sol::lib::jit);
    #endif // TCXLUA_USE_LUAJIT

    lua->open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table
    );

    setBindings(lua);

    return lua;
}

std::string tcxLua::getLuaVersionEstimated(const std::shared_ptr<sol::state>& lua){
    std::string luaSource = R"LUA(
        function ___internal_estimated_lua_version()
            if ({false, [1] = true})[1] then   -- luacheck: ignore 314
                return 'LuaJIT'
            elseif 1 / 0 == 1 / '-0' then
                return 0 + '0' .. '' == '0' and 'Lua 5.4' or 'Lua 5.3'
            end
            local f = function() return function() end end
            return f() == f() and 'Lua 5.2' or 'Lua 5.1'
        end
    )LUA";
    
    lua->safe_script(luaSource);

    sol::protected_function_result result = ((*lua)["___internal_estimated_lua_version"])();

    if (result.get_type() == sol::type::string){
        return result.get<std::string>();
    }else{
        return "Unknown";
    }
}

bool tcxLua::isLuaJITUsedEstimated(const std::shared_ptr<sol::state>& lua){
    std::string luaVersionStr = getLuaVersionEstimated(lua);
    return luaVersionStr == "LuaJIT";
}

void tcxLua::setBindings(const std::shared_ptr<sol::state>& lua){
    setTrussCGeneratedBindings(lua);
    setTypeBindings(lua);
    setConstBindings(lua);
    setColorConstBindings(lua);
    setMathBindings(lua);

    lua->set_function("getElapsedTimef", &trussc::getElapsedTimef);
}

void tcxLua::setConstBindings(const std::shared_ptr<sol::state>& lua){
    auto&& l = *lua;
    l["TAU"] = TAU;
    l["PI"] = PI;
    l["HALF_TAU"] = HALF_TAU;
    l["QUARTER_TAU"] = QUARTER_TAU;
}

template<typename T>
inline void json_new_index_table_fn(Json& j, T i, const sol::table& tbl){
    int _i = 0;
    for (std::pair<sol::object, sol::object>& kv : tbl) 
    {
        if (kv.first.get_type() == sol::type::string)
        {
            std::string key = kv.first.as<std::string>();
            // std::cerr << key << std::endl;
            // std::cerr << key << ": " << kv.second() << std::endl;

            if(kv.second.get_type() == sol::type::number){
                lua_Number v = kv.second.as<lua_Number>();
                j[i][key] = v;
            }else if(kv.second.get_type() == sol::type::string){
                std::string v = kv.second.as<std::string>();
                j[i][key] = v;
            }else if(kv.second.get_type() == sol::type::nil){
                nlohmann::json j_null;
                j[i][key] = j_null;
            }else if(kv.second.get_type() == sol::type::none){
                nlohmann::json j_null;
                j[i][key] = j_null;
            }else if(kv.second.get_type() == sol::type::table){
                j[i][key] = nlohmann::json::object({});
                auto&& v = kv.second.as<sol::table>();
                json_new_index_table_fn(j[i], key, v);
            }else{
                std::cerr << "[tcxLua error] unsupported table insertion";
                std::cerr << " (debug value type id: " << (int)kv.second.get_type() << ")" << std::endl;
                assert(false);
            }
        }

        if (kv.first.get_type() == sol::type::number)
        {
            lua_Number _key = kv.first.as<lua_Number>();
            int key = _key - 1; // Lua index workaround

            // std::cerr << key << std::endl;
            // std::cerr << key << ": " << kv.second() << std::endl;

            if(kv.second.get_type() == sol::type::number){
                lua_Number v = kv.second.as<lua_Number>();
                j[i][key] = v;
            }else if(kv.second.get_type() == sol::type::string){
                std::string v = kv.second.as<std::string>();
                j[i][key] = v;
            }else if(kv.second.get_type() == sol::type::nil){
                nlohmann::json j_null;
                j[i][key] = j_null;
            }else if(kv.second.get_type() == sol::type::none){
                nlohmann::json j_null;
                j[i][key] = j_null;
            }else if(kv.second.get_type() == sol::type::table){
                j[i][key] = nlohmann::json::object({});
                auto&& v = kv.second.as<sol::table>();
                json_new_index_table_fn(j[i], key, v);
            }else{
                std::cerr << "[tcxLua error] unsupported table insertion";
                std::cerr << " (debug value type id: " << (int)kv.second.get_type() << ")" << std::endl;
                assert(false);
            }
        }

        _i++;
    }

    if(_i == 0){
        j[i] = nlohmann::json::object({});
    }
}

void tcxLua::setTypeBindings(const std::shared_ptr<sol::state>& lua){
    sol::usertype<Vec2> vec2_type = lua->new_usertype<Vec2>("Vec2",
        sol::constructors<Vec2(), Vec2(float, float), Vec2(const Vec2&)>(),
        sol::meta_function::addition,
        sol::overload(
           [](const Vec2& a, const Vec2& b){ return a + b; },
           [](const Vec2& a, float b){ return a + b; }
        ),
        sol::meta_function::subtraction,
        sol::overload(
           [](const Vec2& a, const Vec2& b){ return a - b; },
           [](const Vec2& a, float b){ return a - b; }
        ),
        sol::meta_function::multiplication,
        sol::overload(
           [](const Vec2& a, const Vec2& b){ return a * b; },
           [](const Vec2& a, float b){ return a * b; }
        ),
        sol::meta_function::division,
        sol::overload(
           [](const Vec2& a, const Vec2& b){ return a / b; },
           [](const Vec2& a, float b){ return a / b; }
        )
    );
    vec2_type["x"] = &Vec2::x;
    vec2_type["y"] = &Vec2::y;

    vec2_type["length"] = &Vec2::length;
    vec2_type["lengthSquared"] = &Vec2::lengthSquared;
    vec2_type["normalized"] = &Vec2::normalized;
    vec2_type["normalize"] = &Vec2::normalize;
    vec2_type["limit"] = &Vec2::limit;
    vec2_type["dot"] = &Vec2::dot;
    vec2_type["cross"] = &Vec2::cross;
    vec2_type["distance"] = &Vec2::distance;
    vec2_type["distanceSquared"] = &Vec2::distanceSquared;
    vec2_type["angle"] = sol::overload(
        [](Vec2& p){ return p.angle(); },
        [](Vec2& p, const Vec2& v){ return p.angle(v); }
    );
    vec2_type["rotated"] = &Vec2::rotated;
    vec2_type["rotate"] = &Vec2::rotate;
    vec2_type["lerp"] = &Vec2::lerp;
    vec2_type["perpendicular"] = &Vec2::perpendicular;
    vec2_type["reflected"] = &Vec2::reflected;
    vec2_type["fromAngle"] = &Vec2::fromAngle;

    sol::usertype<Vec3> vec3_type = lua->new_usertype<Vec3>("Vec3",
        sol::constructors<Vec3(), Vec3(float, float, float), Vec3(const Vec2&), Vec3(const Vec3&)>(),
        sol::meta_function::addition,
        sol::overload(
           [](const Vec3& a, const Vec3& b){ return a + b; },
           [](const Vec3& a, float b){ return a + b; }
        ),
        sol::meta_function::subtraction,
        sol::overload(
           [](const Vec3& a, const Vec3& b){ return a - b; },
           [](const Vec3& a, float b){ return a - b; }
        ),
        sol::meta_function::multiplication,
        sol::overload(
           [](const Vec3& a, const Vec3& b){ return a * b; },
           [](const Vec3& a, float b){ return a * b; }
        ),
        sol::meta_function::division,
        sol::overload(
           [](const Vec3& a, const Vec3& b){ return a / b; },
           [](const Vec3& a, float b){ return a / b; }
        )
    );
    vec3_type["x"] = &Vec3::x;
    vec3_type["y"] = &Vec3::y;
    vec3_type["z"] = &Vec3::z;

    vec3_type["length"] = &Vec3::length;
    vec3_type["lengthSquared"] = &Vec3::lengthSquared;
    vec3_type["normalized"] = &Vec3::normalized;
    vec3_type["normalize"] = &Vec3::normalize;
    vec3_type["limit"] = &Vec3::limit;
    vec3_type["dot"] = &Vec3::dot;
    vec3_type["cross"] = &Vec3::cross;
    vec3_type["distance"] = &Vec3::distance;
    vec3_type["distanceSquared"] = &Vec3::distanceSquared;
    vec3_type["lerp"] = &Vec3::lerp;
    vec3_type["reflected"] = &Vec3::reflected;
    vec3_type["xy"] = &Vec3::xy;

    sol::usertype<Vec4> vec4_type = lua->new_usertype<Vec4>("Vec4",
        sol::constructors<Vec4(), Vec4(float, float, float, float), Vec4(const Vec3&), Vec4(const Vec4&)>(),
        sol::meta_function::addition,
        sol::overload(
           [](const Vec4& a, const Vec4& b){ return a + b; },
           [](const Vec4& a, float b){ return a + b; }
        ),
        sol::meta_function::subtraction,
        sol::overload(
           [](const Vec4& a, const Vec4& b){ return a - b; },
           [](const Vec4& a, float b){ return a - b; }
        ),
        sol::meta_function::multiplication,
        [](const Vec4& a, float b){ return a * b; },
        sol::meta_function::division,
        [](const Vec4& a, float b){ return a / b; }
    );
    vec4_type["x"] = &Vec4::x;
    vec4_type["y"] = &Vec4::y;
    vec4_type["z"] = &Vec4::z;
    vec4_type["w"] = &Vec4::w;

    vec4_type["length"] = &Vec4::length;
    vec4_type["lengthSquared"] = &Vec4::lengthSquared;
    vec4_type["normalized"] = &Vec4::normalized;
    vec4_type["normalize"] = &Vec4::normalize;
    vec4_type["dot"] = &Vec4::dot;
    vec4_type["lerp"] = &Vec4::lerp;
    vec4_type["xy"] = &Vec4::xy;

    sol::usertype<Quaternion> quat_type = lua->new_usertype<Quaternion>("Quaternion",
        sol::constructors<Quaternion(), Quaternion(float, float, float, float), Quaternion(const Quaternion&)>(),
        sol::meta_function::multiplication,
        [](const Quaternion& a, const Quaternion& b){ return a * b; }
    );
    quat_type["w"] = &Quaternion::w;
    quat_type["x"] = &Quaternion::x;
    quat_type["y"] = &Quaternion::y;
    quat_type["z"] = &Quaternion::z;
    quat_type["identity"] = &Quaternion::identity;
    quat_type["fromAxisAngle"] = &Quaternion::fromAxisAngle;
    quat_type["fromEuler"] = sol::overload(
        [](float pitch, float yaw, float roll){ return Quaternion::fromEuler(pitch, yaw, roll); },
        [](const Vec3& euler){ return Quaternion::fromEuler(euler); }
    );
    quat_type["toEuler"] = &Quaternion::toEuler;
    quat_type["toMatrix"] = &Quaternion::toMatrix;
    quat_type["length"] = &Quaternion::length;
    quat_type["lengthSquared"] = &Quaternion::lengthSquared;
    quat_type["normalized"] = &Quaternion::normalized;
    quat_type["normalize"] = &Quaternion::normalize;
    quat_type["conjugate"] = &Quaternion::conjugate;
    quat_type["rotate"] = &Quaternion::rotate;
    quat_type["slerp"] = &Quaternion::slerp;

    sol::usertype<Mat4> mat4_type = lua->new_usertype<Mat4>("Mat4",
        sol::constructors<Mat4(),
            Mat4(float m00, float m01, float m02, float m03,
                 float m10, float m11, float m12, float m13,
                 float m20, float m21, float m22, float m23,
                 float m30, float m31, float m32, float m33),
            Mat4(const Mat4&)>(),
        sol::meta_function::multiplication,
        sol::overload(
           [](const Mat4& a, const Mat4& b){ return a * b; },
           [](const Mat4& a, const Vec3& b){ return a * b; },
           [](const Mat4& a, const Vec4& b){ return a * b; }
        )
    );
    mat4_type["at"] = [](Mat4& m, int raw, int col) { return m.at(raw, col); };
    mat4_type["set"] = [](Mat4& m, int raw, int col, int v){ m.at(raw, col) = v; }; // WORKAROUND
    mat4_type["identity"] = &Mat4::identity;
    mat4_type["fromHomography"] = &Mat4::fromHomography;
    mat4_type["translate"] = sol::overload(
        [](Mat4& m, float tx, float ty, float tz){ return m.translate(tx, ty, tz); },
        [](Mat4& m, const Vec3& t){ return m.translate(t); }
    );
    mat4_type["rotateX"] = &Mat4::rotateX;
    mat4_type["rotateY"] = &Mat4::rotateY;
    mat4_type["rotateZ"] = &Mat4::rotateZ;
    mat4_type["rotate"] = &Mat4::rotate;
    mat4_type["scale"] = sol::overload(
        [](Mat4& m, float s){ return m.scale(s); },
        [](Mat4& m, float sx, float sy, float sz){ return m.scale(sx, sy, sz); },
        [](Mat4& m, const Vec3& s){ return m.scale(s); }
    );
    mat4_type["transposed"] = &Mat4::transposed;
    mat4_type["inverted"] = &Mat4::inverted;
    mat4_type["lookAt"] = &Mat4::lookAt;
    mat4_type["ortho"] = &Mat4::ortho;
    mat4_type["perspective"] = &Mat4::perspective;
    mat4_type["frustum"] = &Mat4::frustum;
    
    sol::usertype<Color> color_type = lua->new_usertype<Color>("Color",
        sol::constructors<Color(), Color(float), Color(float, float), Color(float, float, float), Color(float, float, float, float), Color(const Color&)>(),
        sol::meta_function::addition,
        [](const Color& a, const Color& b){ return a + b; },
        sol::meta_function::subtraction,
        [](const Color& a, const Color& b){ return a - b; },
        sol::meta_function::multiplication,
        [](const Color& a, float b){ return a * b; },
        sol::meta_function::division,
        [](const Color& a, float b){ return a / b; }
    );

    color_type["r"] = &Color::r;
    color_type["g"] = &Color::g;
    color_type["b"] = &Color::b;
    color_type["a"] = &Color::a;

    color_type["fromBytes"] = &Color::fromBytes;
    color_type["fromHex"] = &Color::fromHex;
    color_type["fromHSB"] = &Color::fromHSB;
    color_type["fromOKLCH"] = &Color::fromOKLCH;
    color_type["fromOKLab"] = &Color::fromOKLab;
    color_type["fromLinear"] = &Color::fromLinear;
    color_type["toHex"] = &Color::toHex;
    color_type["toLinear"] = &Color::toLinear;
    color_type["toHSB"] = &Color::toHSB;
    color_type["toOKLab"] = &Color::toOKLab;
    color_type["toOKLCH"] = &Color::toOKLCH;
    color_type["clamped"] = &Color::clamped;
    color_type["lerpRGB"] = &Color::lerpRGB;
    color_type["lerpLinear"] = &Color::lerpLinear;
    color_type["lerpHSB"] = &Color::lerpHSB;
    color_type["lerpOKLab"] = &Color::lerpOKLab;
    color_type["lerpOKLCH"] = &Color::lerpOKLCH;
    color_type["lerp"] = &Color::lerp;

    sol::usertype<Mesh> mesh_type = lua->new_usertype<Mesh>("Mesh",
        sol::constructors<Mesh(), Mesh(const Mesh&), Mesh(Mesh&&)>()
    );

    mesh_type["setMode"] = &Mesh::setMode;
    mesh_type["getMode"] = &Mesh::getMode;
    mesh_type["addVertex"] = sol::overload(
        [](Mesh& m, float x, float y){ return m.addVertex(x, y); },
        [](Mesh& m, float x, float y, float z){ return m.addVertex(x, y, z); },
        [](Mesh& m, const Vec2& v){ return m.addVertex(v); },
        [](Mesh& m, const Vec3& v){ return m.addVertex(v); }
    );
    mesh_type["addVertices"] = &Mesh::addVertices;
    mesh_type["getVertices"] = [](Mesh& m){ return m.getVertices(); };
    mesh_type["getNumVertices"] = &Mesh::getNumVertices;
    mesh_type["addColor"] = sol::overload(
        [](Mesh& m, float x, float y, float z){ return m.addColor(x, y, z); },
        [](Mesh& m, float x, float y, float z, float w){ return m.addColor(x, y, z, w); },
        [](Mesh& m, const Color& v){ return m.addColor(v); }
    );
    mesh_type["addColors"] = &Mesh::addColors;
    mesh_type["getColors"] = [](Mesh& m){ return m.getColors(); };
    mesh_type["getNumColors"] = &Mesh::getNumColors;
    mesh_type["hasColors"] = &Mesh::hasColors;
    mesh_type["addIndex"] = &Mesh::addIndex;
    mesh_type["addIndices"] = &Mesh::addIndices;
    mesh_type["addTriangle"] = &Mesh::addTriangle;
    mesh_type["getIndices"] = [](Mesh& m){ return m.getIndices(); };
    mesh_type["hasIndices"] = &Mesh::hasIndices;
    mesh_type["addNormal"] = sol::overload(
        [](Mesh& m, float x, float y, float z){ return m.addNormal(x, y, z); },
        [](Mesh& m, const Vec3& v){ return m.addNormal(v); }
    );
    mesh_type["addNormals"] = &Mesh::addNormals;
    mesh_type["setNormal"] = &Mesh::setNormal;
    mesh_type["getNormal"] = &Mesh::getNormal;
    mesh_type["getNormals"] = [](Mesh& m){ return m.getNormals(); };
    mesh_type["getNumNormals"] = &Mesh::getNumNormals;
    mesh_type["hasNormals"] = &Mesh::hasNormals;
    mesh_type["addTexCoord"] = sol::overload(
        [](Mesh& m, float x, float y){ return m.addTexCoord(x, y); },
        [](Mesh& m, const Vec2& v){ return m.addTexCoord(v); }
    );
    mesh_type["getTexCoords"] = [](Mesh& m){ return m.getTexCoords(); };
    mesh_type["hasTexCoords"] = &Mesh::hasTexCoords;
    mesh_type["hasValidTexCoords"] = &Mesh::hasValidTexCoords;
    mesh_type["addTangent"] = sol::overload(
        [](Mesh& m, float x, float y, float z){ return m.addTangent(x, y, z); },
        [](Mesh& m, float x, float y, float z, float w){ return m.addTangent(x, y, z, w); },
        [](Mesh& m, const Vec4& v){ return m.addTangent(v); }
    );
    mesh_type["getTangents"] = [](Mesh& m){ return m.getTangents(); };
    mesh_type["getNumTangents"] = &Mesh::getNumTangents;
    mesh_type["hasTangents"] = &Mesh::hasTangents;
    mesh_type["clear"] = &Mesh::clear;
    mesh_type["clearVertices"] = &Mesh::clearVertices;
    mesh_type["clearNormals"] = &Mesh::clearNormals;
    mesh_type["clearColors"] = &Mesh::clearColors;
    mesh_type["clearIndices"] = &Mesh::clearIndices;
    mesh_type["clearTexCoords"] = &Mesh::clearTexCoords;
    mesh_type["clearTangents"] = &Mesh::clearTangents;
    mesh_type["translate"] = sol::overload(
        [](Mesh& m, float x, float y, float z){ return m.translate(x, y, z); },
        [](Mesh& m, const Vec3& v){ return m.translate(v); }
    );
    mesh_type["rotateX"] = &Mesh::rotateX;
    mesh_type["rotateY"] = &Mesh::rotateY;
    mesh_type["rotateZ"] = &Mesh::rotateZ;
    mesh_type["scale"] = sol::overload(
        [](Mesh& m, float x, float y, float z){ return m.scale(x, y, z); },
        [](Mesh& m, float s){ return m.scale(s); }
    );
    mesh_type["transform"] = &Mesh::transform;
    mesh_type["append"] = &Mesh::append;
    mesh_type["draw"] = sol::overload(
        [](Mesh& m){ return m.draw(); },
        [](Mesh& m, const Image& image){ return m.draw(image); },
        [](Mesh& m, const Texture& tex){ return m.draw(tex); }
    );
    mesh_type["drawNoLighting"] = &Mesh::drawNoLighting;
    mesh_type["drawWithLighting"] = &Mesh::drawWithLighting;
    mesh_type["drawNoLightingWithTexture"] = &Mesh::drawNoLightingWithTexture;
    mesh_type["drawWireframe"] = &Mesh::drawWireframe;

    sol::usertype<Fbo> fbo_type = lua->new_usertype<Fbo>("Fbo",
        sol::constructors<Fbo()>() // FIXME: move constructor?
    );

    fbo_type["allocate"] = sol::overload(
        [](Fbo& f, int w, int h){ return f.allocate(w, h); },
        [](Fbo& f, int w, int h, int sampleCount){ return f.allocate(w, h, sampleCount); },
        [](Fbo& f, int w, int h, int sampleCount, TextureFormat t){ return f.allocate(w, h, sampleCount, t); }
    );
    fbo_type["clear"] = &Fbo::clear;
    fbo_type["begin"] = sol::overload(
        [](Fbo& f){ return f.begin(); },
        [](Fbo& f, float r, float g, float b){ return f.begin(r, g, b); },
        [](Fbo& f, float r, float g, float b, float a){ return f.begin(r, g, b, a); }
    );
    fbo_type["clearColor"] = &Fbo::clearColor;
    fbo_type["end"] = &Fbo::end;
    fbo_type["end_fbo"] = &Fbo::end;
    fbo_type["readPixels"] = &Fbo::readPixels;
    fbo_type["readPixelsFloat"] = &Fbo::readPixelsFloat;
    fbo_type["copyTo"] = &Fbo::copyTo;
    fbo_type["getWidth"] = &Fbo::getWidth;
    fbo_type["getHeight"] = &Fbo::getHeight;
    fbo_type["getSampleCount"] = &Fbo::getSampleCount;
    fbo_type["getTextureFormat"] = &Fbo::getTextureFormat;
    fbo_type["isAllocated"] = &Fbo::isAllocated;
    fbo_type["isActive"] = &Fbo::isActive;
    fbo_type["getTexture"] = [](Fbo& f) -> Texture& { return f.getTexture(); };
    fbo_type["save"] = &Fbo::save;
    fbo_type["getColorImage"] = &Fbo::getColorImage;
    fbo_type["getTextureView"] = &Fbo::getTextureView;
    fbo_type["getSampler"] = &Fbo::getSampler;

    sol::usertype<Texture> tex_type = lua->new_usertype<Texture>("Texture",
        sol::constructors<Texture()>() // FIXME: move constructor?
    );
    tex_type["allocate"] = sol::overload(
        [](Texture& f, int w, int h){ return f.allocate(w, h); },
        [](Texture& f, int w, int h, int c){ return f.allocate(w, h, c); },
        [](Texture& f, int w, int h, int c, TextureUsage t){ return f.allocate(w, h, c, t); },
        [](Texture& f, int w, int h, int c, TextureUsage t, int s){ return f.allocate(w, h, c, t, s); },
        [](Texture& f, int w, int h, TextureFormat c){ return f.allocate(w, h, c); },
        [](Texture& f, int w, int h, TextureFormat c, TextureUsage t){ return f.allocate(w, h, c, t); },
        [](Texture& f, int w, int h, TextureFormat c, TextureUsage t, int s){ return f.allocate(w, h, c, t, s); },
        [](Texture& f, const Pixels& w){ return f.allocate(w); },
        [](Texture& f, const Pixels& w, TextureUsage c){ return f.allocate(w, c); },
        [](Texture& f, const Pixels& w, TextureUsage c, bool m){ return f.allocate(w, c, m); }
    );
    tex_type["allocateCubemap"] = sol::overload(
        [](Texture& f, int w, TextureFormat c){ return f.allocateCubemap(w, c); },
        [](Texture& f, int w, TextureFormat c, TextureUsage t){ return f.allocateCubemap(w, c, t); },
        [](Texture& f, int w, TextureFormat c, TextureUsage t, int s){ return f.allocateCubemap(w, c, t, s); }
    );
    tex_type["uploadCubemapFace"] = &Texture::uploadCubemapFace;
    tex_type["uploadCubemapMip"] = &Texture::uploadCubemapMip;
    tex_type["getCubemapFaceAttachmentView"] = &Texture::getCubemapFaceAttachmentView;
    tex_type["isCubemap"] = &Texture::isCubemap;
    tex_type["getNumMipLevels"] = &Texture::getNumMipLevels;
    tex_type["allocateCompressed"] = &Texture::allocateCompressed;
    tex_type["updateCompressed"] = &Texture::updateCompressed;
    tex_type["isCompressed"] = &Texture::isCompressed;
    tex_type["clear"] = &Texture::clear;
    tex_type["isAllocated"] = &Texture::isAllocated;
    tex_type["getWidth"] = &Texture::getWidth;
    tex_type["getHeight"] = &Texture::getHeight;
    tex_type["getChannels"] = &Texture::getChannels;
    tex_type["getUsage"] = &Texture::getUsage;
    tex_type["getSampleCount"] = &Texture::getSampleCount;
    tex_type["getPixelFormat"] = &Texture::getPixelFormat;
    tex_type["loadData"] = sol::overload(
        [](Texture& t, const Pixels& p){ return t.loadData(p); },
        [](Texture& t, const unsigned char* d, int w, int h, int c){ return t.loadData(d, w, h, c); },
        [](Texture& t, const void* d, int w, int h, int c){ return t.loadData(d, w, h, c); }
    );
    tex_type["setMinFilter"] = &Texture::setMinFilter;
    tex_type["setMagFilter"] = &Texture::setMagFilter;
    tex_type["setFilter"] = &Texture::setFilter;
    tex_type["getMinFilter"] = &Texture::getMinFilter;
    tex_type["getMagFilter"] = &Texture::getMagFilter;
    tex_type["setPremultipliedAlpha"] = &Texture::setPremultipliedAlpha;
    tex_type["isPremultipliedAlpha"] = &Texture::isPremultipliedAlpha;
    tex_type["setWrapU"] = &Texture::setWrapU;
    tex_type["setWrapV"] = &Texture::setWrapV;
    tex_type["setWrap"] = &Texture::setWrap;
    tex_type["getWrapU"] = &Texture::getWrapU;
    tex_type["getWrapV"] = &Texture::getWrapV;
    tex_type["draw"] = sol::overload(
        [](Texture& t, float x, float y){ return t.draw(x, y); },
        [](Texture& t, float x, float y, float w, float h){ return t.draw(x, y, w, h); }
    );
    tex_type["drawSubsection"] = &Texture::drawSubsection;
    tex_type["bind"] = &Texture::bind;
    tex_type["unbind"] = &Texture::unbind;
    tex_type["getImage"] = &Texture::getImage;
    tex_type["getView"] = &Texture::getView;
    tex_type["getSampler"] = &Texture::getSampler;
    tex_type["getAttachmentView"] = &Texture::getAttachmentView;

    sol::usertype<TextureFormat> tex_format_type = lua->new_usertype<TextureFormat>("TextureFormat",
        sol::meta_function::equal_to, [](TextureFormat a, TextureFormat b){ return a == b; }
    );
    tex_format_type["RGBA8"] = sol::var(TextureFormat::RGBA8);
    tex_format_type["RGBA16F"] = sol::var(TextureFormat::RGBA16F);
    tex_format_type["RGBA32F"] = sol::var(TextureFormat::RGBA32F);
    tex_format_type["R8"] = sol::var(TextureFormat::R8);
    tex_format_type["R16F"] = sol::var(TextureFormat::R16F);
    tex_format_type["R32F"] = sol::var(TextureFormat::R32F);
    tex_format_type["RG8"] = sol::var(TextureFormat::RG8);
    tex_format_type["RG16F"] = sol::var(TextureFormat::RG16F);
    tex_format_type["RG32F"] = sol::var(TextureFormat::RG16F);

    sol::usertype<Image> img_type = lua->new_usertype<Image>("Image",
        sol::constructors<Image()>() // FIXME: move constructor?
    );
    img_type["load"] = &Image::load;
    img_type["loadFromMemory"] = &Image::loadFromMemory;
    img_type["save"] = &Image::save;
    img_type["allocate"] = sol::overload(
        [](Image& t, int w, int h){ return t.allocate(w, h); },
        [](Image& t, int w, int h, int c){ return t.allocate(w, h, c); }
    );
    img_type["clear"] = &Image::clear;
    img_type["isAllocated"] = &Image::isAllocated;
    img_type["getWidth"] = &Image::getWidth;
    img_type["getHeight"] = &Image::getHeight;
    img_type["getChannels"] = &Image::getChannels;
    img_type["getPixels"] = [](Image& f) -> Pixels& { return f.getPixels(); };
    img_type["getPixelsData"] = [](Image& f) -> unsigned char* { return f.getPixelsData(); };
    img_type["getColor"] = &Image::getColor;
    img_type["setColor"] = &Image::setColor;
    img_type["update"] = &Image::update;
    img_type["setDirty"] = &Image::setDirty;
    img_type["getTexture"] = [](Image& f) -> Texture& { return f.getTexture(); };

    sol::usertype<Pixels> pix_type = lua->new_usertype<Pixels>("Pixels",
        sol::constructors<Pixels()>() // FIXME: move constructor?
    );
    pix_type["allocate"] = sol::overload(
        [](Pixels& f, int w, int h){ return f.allocate(w, h); },
        [](Pixels& f, int w, int h, int c){ return f.allocate(w, h, c); },
        [](Pixels& f, int w, int h, int c, PixelFormat t){ return f.allocate(w, h, c, t); }
    );
    pix_type["clear"] = &Pixels::clear;
    pix_type["isAllocated"] = &Pixels::isAllocated;
    pix_type["getWidth"] = &Pixels::getWidth;
    pix_type["getHeight"] = &Pixels::getHeight;
    pix_type["getChannels"] = &Pixels::getChannels;
    pix_type["getFormat"] = &Pixels::getFormat;
    pix_type["isFloat"] = &Pixels::isFloat;
    pix_type["getTotalBytes"] = &Pixels::getTotalBytes;
    pix_type["getData"] = [](Pixels& f) -> unsigned char* { return f.getData(); };
    pix_type["getDataF32"] = [](Pixels& f) -> float* { return f.getDataF32(); };
    pix_type["getDataVoid"] = [](Pixels& f) -> void* { return f.getDataVoid(); };
    pix_type["getColor"] = &Pixels::getColor;
    pix_type["setColor"] = &Pixels::setColor;
    pix_type["setFromPixels"] = &Pixels::setFromPixels;
    pix_type["setFromFloats"] = &Pixels::setFromFloats;
    pix_type["copyTo"] = &Pixels::copyTo;
    pix_type["clone"] = &Pixels::clone;
    pix_type["load"] = &Pixels::load;
    pix_type["loadHDR"] = &Pixels::loadHDR;
    pix_type["loadPlatform"] = &Pixels::loadPlatform;
    pix_type["loadFromMemory"] = &Pixels::loadFromMemory;
    pix_type["save"] = &Pixels::save;

    sol::usertype<PixelFormat> pix_format_type = lua->new_usertype<PixelFormat>("PixelFormat",
        sol::meta_function::equal_to, [](PixelFormat a, PixelFormat b){ return a == b; }
    );
    pix_format_type["U8"] = sol::var(PixelFormat::U8);
    pix_format_type["F32"] = sol::var(PixelFormat::F32);

    sol::usertype<Shader> shader_type = lua->new_usertype<Shader>("Shader",
        sol::constructors<Shader()>() // FIXME: move constructor?
    );
    shader_type["load"] = &Shader::load;
    shader_type["clear"] = &Shader::clear;
    shader_type["isLoaded"] = &Shader::isLoaded;
    shader_type["begin"] = &Shader::begin;
    shader_type["end"] = &Shader::end;
    shader_type["end_shader"] = &Shader::end;
    shader_type["setUniform"] = sol::overload(
        [](Shader& f, int s, float v){ return f.setUniform(s, v); },
        [](Shader& f, int s, const Vec2& v){ return f.setUniform(s, v); },
        [](Shader& f, int s, const Vec3& v){ return f.setUniform(s, v); },
        [](Shader& f, int s, const Vec4& v){ return f.setUniform(s, v); },
        [](Shader& f, int s, const Color& v){ return f.setUniform(s, v); },
        [](Shader& f, int s, const std::vector<float>& v){ return f.setUniform(s, v); },
        [](Shader& f, int s, const std::vector<Vec2>& v){ return f.setUniform(s, v); },
        [](Shader& f, int s, const std::vector<Vec3>& v){ return f.setUniform(s, v); },
        [](Shader& f, int s, const std::vector<Vec4>& v){ return f.setUniform(s, v); },
        [](Shader& f, int s, const void* data, size_t size){ return f.setUniform(s, data, size); }
    );
    shader_type["storeUniform"] = &Shader::storeUniform;
    shader_type["applyUniforms"] = &Shader::applyUniforms;
    shader_type["setTexture"] = sol::overload(
        [](Shader& f, int s, sg_image i, sg_sampler p){ return f.setTexture(s, i, p); },
        [](Shader& f, int s, sg_view i, sg_sampler p){ return f.setTexture(s, i, p); }
    );
    shader_type["submitVertices"] = &Shader::submitVertices;
    shader_type["executeDeferredDraw"] = &Shader::executeDeferredDraw;

    sol::usertype<EasyCam> easycam_t = lua->new_usertype<EasyCam>("EasyCam",
        sol::constructors<EasyCam()>()
    );

    easycam_t["begin"] = &EasyCam::begin;
    easycam_t["end"] = &EasyCam::end;
    easycam_t["end_cam"] = &EasyCam::end;
    easycam_t["reset"] = &EasyCam::reset;
    easycam_t["setTarget"] = sol::overload(
        [](EasyCam& m, float x, float y, float z){ return m.setTarget(x, y, z); },
        [](EasyCam& m, const Vec3& v){ return m.setTarget(v); }
    );
    easycam_t["getTarget"] = &EasyCam::getTarget;
    easycam_t["setUpAxis"] = sol::overload(
        [](EasyCam& m, float x, float y, float z){ return m.setUpAxis(x, y, z); },
        [](EasyCam& m, const Vec3& v){ return m.setUpAxis(v); }
    );
    easycam_t["getUpAxis"] = &EasyCam::getUpAxis;
    easycam_t["setDistance"] = &EasyCam::setDistance;
    easycam_t["getDistance"] = &EasyCam::getDistance;
    easycam_t["setFov"] = &EasyCam::setFov;
    easycam_t["getFov"] = &EasyCam::getFov;
    easycam_t["getPosition"] = &EasyCam::getPosition;
    easycam_t["setFovDeg"] = &EasyCam::setFovDeg;
    easycam_t["setNearClip"] = &EasyCam::setNearClip;
    easycam_t["setFarClip"] = &EasyCam::setFarClip;
    easycam_t["setSensitivity"] = &EasyCam::setSensitivity;
    easycam_t["setZoomSensitivity"] = &EasyCam::setZoomSensitivity;
    easycam_t["setPanSensitivity"] = &EasyCam::setPanSensitivity;
    easycam_t["setControlArea"] = &EasyCam::setControlArea;
    easycam_t["clearControlArea"] = &EasyCam::clearControlArea;
    easycam_t["enableMouseInput"] = &EasyCam::enableMouseInput;
    easycam_t["disableMouseInput"] = &EasyCam::disableMouseInput;
    easycam_t["isMouseInputEnabled"] = &EasyCam::isMouseInputEnabled;
    easycam_t["mousePressed"] = &EasyCam::mousePressed;
    easycam_t["mouseReleased"] = &EasyCam::mouseReleased;
    easycam_t["mouseDragged"] = &EasyCam::mouseDragged;
    easycam_t["mouseScrolled"] = &EasyCam::mouseScrolled;

    sol::usertype<Light> light_t = lua->new_usertype<Light>("Light",
        sol::constructors<Light()>()
    );
    light_t["setDirectional"] = sol::overload(
        [](Light& m, float x, float y, float z){ return m.setDirectional(x, y, z); },
        [](Light& m, const Vec3& v){ return m.setDirectional(v); }
    );
    light_t["setPoint"] = sol::overload(
        [](Light& m, float x, float y, float z){ return m.setPoint(x, y, z); },
        [](Light& m, const Vec3& v){ return m.setPoint(v); }
    );
    light_t["setSpot"] = sol::overload(
        [](Light& m, const Vec3& p, const Vec3& d){ return m.setSpot(p, d); },
        [](Light& m, const Vec3& p, const Vec3& d, float i){ return m.setSpot(p, d, i); },
        [](Light& m, const Vec3& p, const Vec3& d, float i, float o){ return m.setSpot(p, d, i, o); },
        [](Light& m, float a, float b, float c, float d, float e, float f){ return m.setSpot(a,b,c,d,e,f); },
        [](Light& m, float a, float b, float c, float d, float e, float f, float i){ return m.setSpot(a,b,c,d,e,f, i); },
        [](Light& m, float a, float b, float c, float d, float e, float f, float i, float o){ return m.setSpot(a,b,c,d,e,f, i,o); }
    );
    light_t["getSpotInnerCos"] = &Light::getSpotInnerCos;
    light_t["getSpotOuterCos"] = &Light::getSpotOuterCos;
    light_t["setProjectionTexture"] = &Light::setProjectionTexture;
    light_t["getProjectionTexture"] = &Light::getProjectionTexture;
    light_t["hasProjectionTexture"] = &Light::hasProjectionTexture;
    light_t["setLensShift"] = &Light::setLensShift;
    light_t["getLensShiftX"] = &Light::getLensShiftX;
    light_t["getLensShiftY"] = &Light::getLensShiftY;
    light_t["setProjectorAspect"] = &Light::setProjectorAspect;
    light_t["getProjectorAspect"] = &Light::getProjectorAspect;
    light_t["computeProjectorViewProj"] = sol::overload(
        [](Light& m){ return m.computeProjectorViewProj(); },
        [](Light& m, float a){ return m.computeProjectorViewProj(a); },
        [](Light& m, float a, float b){ return m.computeProjectorViewProj(a, b); }
    );
    light_t["setIesProfile"] = &Light::setIesProfile;
    light_t["getIesProfile"] = &Light::getIesProfile;
    light_t["hasIesProfile"] = &Light::hasIesProfile;
    light_t["enableShadow"] = &Light::enableShadow;
    light_t["disableShadow"] = &Light::disableShadow;
    light_t["isShadowEnabled"] = &Light::isShadowEnabled;
    light_t["getShadowResolution"] = &Light::getShadowResolution;
    light_t["setShadowBias"] = &Light::setShadowBias;
    light_t["getShadowBias"] = &Light::getShadowBias;
    light_t["getType"] = &Light::getType;
    light_t["getDirection"] = &Light::getDirection;
    light_t["getPosition"] = &Light::getPosition;
    light_t["setAmbient"] = sol::overload(
        [](Light& m, const Color& c){ return m.setAmbient(c); },
        [](Light& m, float a, float b, float c){ return m.setAmbient(a,b,c); },
        [](Light& m, float a, float b, float c, float d){ return m.setAmbient(a,b,c,d); }
    );
    light_t["setDiffuse"] = sol::overload(
        [](Light& m, const Color& c){ return m.setDiffuse(c); },
        [](Light& m, float a, float b, float c){ return m.setDiffuse(a,b,c); },
        [](Light& m, float a, float b, float c, float d){ return m.setDiffuse(a,b,c,d); }
    );
    light_t["setSpecular"] = sol::overload(
        [](Light& m, const Color& c){ return m.setSpecular(c); },
        [](Light& m, float a, float b, float c){ return m.setSpecular(a,b,c); },
        [](Light& m, float a, float b, float c, float d){ return m.setSpecular(a,b,c,d); }
    );
    light_t["getAmbient"] = &Light::getAmbient;
    light_t["getDiffuse"] = &Light::getDiffuse;
    light_t["getSpecular"] = &Light::getSpecular;
    light_t["getIntensity"] = &Light::getIntensity;
    light_t["setIntensity"] = &Light::setIntensity;

    light_t["setAttenuation"] = &Light::setAttenuation;
    light_t["getConstantAttenuation"] = &Light::getConstantAttenuation;
    light_t["getLinearAttenuation"] = &Light::getLinearAttenuation;
    light_t["getQuadraticAttenuation"] = &Light::getQuadraticAttenuation;
    light_t["enable"] = &Light::enable;
    light_t["disable"] = &Light::disable;
    light_t["isEnabled"] = &Light::isEnabled;
    light_t["calculate"] = &Light::calculate;

    sol::usertype<LightType> lighttype_t = lua->new_usertype<LightType>("LightType",
        sol::meta_function::equal_to, [](LightType a, LightType b){ return a == b; }
    );
    lighttype_t["Directional"] = sol::var(LightType::Directional);
    lighttype_t["Point"] = sol::var(LightType::Point);
    lighttype_t["Spot"] = sol::var(LightType::Spot);

    sol::usertype<Material> material_t = lua->new_usertype<Material>("Material",
        sol::constructors<Material()>()
    );
    material_t["getBaseColor"] = &Material::getBaseColor;
    material_t["setBaseColor"] = sol::overload(
        [](Material& m, const Color& c){ return m.setBaseColor(c); },
        [](Material& m, float a, float b, float c){ return m.setBaseColor(a,b,c); },
        [](Material& m, float a, float b, float c, float d){ return m.setBaseColor(a,b,c,d); }
    );
    material_t["setMetallic"] = &Material::setMetallic;
    material_t["getMetallic"] = &Material::getMetallic;
    material_t["setRoughness"] = &Material::setRoughness;
    material_t["getRoughness"] = &Material::getRoughness;
    material_t["setAo"] = &Material::setAo;
    material_t["getAo"] = &Material::getAo;
    material_t["getEmissiveStrength"] = &Material::getEmissiveStrength;
    material_t["setEmissiveStrength"] = &Material::setEmissiveStrength;
    material_t["getEmissive"] = &Material::getEmissive;
    material_t["setEmissive"] = sol::overload(
        [](Material& m, const Color& c){ return m.setEmissive(c); },
        [](Material& m, float a, float b, float c){ return m.setEmissive(a,b,c); }
    );
    material_t["setMetallic"] = sol::var(&Material::setMetallic);
    material_t["gold"] = sol::var(&Material::gold);
    material_t["silver"] = sol::var(&Material::silver);
    material_t["copper"] = sol::var(&Material::copper);
    material_t["iron"] = sol::var(&Material::iron);
    material_t["bronze"] = sol::var(&Material::bronze);
    material_t["emerald"] = sol::var(&Material::emerald);
    material_t["ruby"] = sol::var(&Material::ruby);
    // FIXME: this makes error on runtime
    // material_t["plastic"] = sol::var(sol::overload(
    //     [](const Color& c, float r){ return Material::plastic(c, r); },
    //     [](const Color& c){ return Material::plastic(c); }
    // ));
    material_t["plastic"] = sol::var(&Material::plastic);
    material_t["rubber"] = sol::var(&Material::rubber);
    material_t["fromPhong"] = sol::overload(
        [](Material& m, const Color& d, const Color& p, float s){ return m.fromPhong(d, p, s); },
        [](Material& m, const Color& d, const Color& p, float s, const Color& e){ return m.fromPhong(d, p, s, e); }
    );
    material_t["setNormalMap"] = &Material::setNormalMap;
    material_t["getNormalMap"] = &Material::getNormalMap;
    material_t["hasNormalMap"] = &Material::hasNormalMap;

    material_t["setBaseColorTexture"] = &Material::setBaseColorTexture;
    material_t["getBaseColorTexture"] = &Material::getBaseColorTexture;
    material_t["hasBaseColorTexture"] = &Material::hasBaseColorTexture;
    material_t["setMetallicRoughnessTexture"] = &Material::setMetallicRoughnessTexture;
    material_t["getMetallicRoughnessTexture"] = &Material::getMetallicRoughnessTexture;
    material_t["hasMetallicRoughnessTexture"] = &Material::hasMetallicRoughnessTexture;
    material_t["setEmissiveTexture"] = &Material::setEmissiveTexture;
    material_t["getEmissiveTexture"] = &Material::getEmissiveTexture;
    material_t["hasEmissiveTexture"] = &Material::hasEmissiveTexture;
    material_t["setOcclusionTexture"] = &Material::setOcclusionTexture;
    material_t["getOcclusionTexture"] = &Material::getOcclusionTexture;
    material_t["hasOcclusionTexture"] = &Material::hasOcclusionTexture;

    lua->set_function("loadJson", &trussc::loadJson);
    lua->set_function("saveJson", sol::overload(
        [](const Json& j, const std::string& path){ return trussc::saveJson(j, path); },
        [](const Json& j, const std::string& path, int indent){ return trussc::saveJson(j, path, indent); }
    ));
    lua->set_function("parseJson", &trussc::parseJson);
    lua->set_function("toJsonString", &trussc::toJsonString);

    sol::usertype<Json> json_t = lua->new_usertype<Json>("Json",
        sol::constructors<Json()>(),
        "get_string", [](Json& j){ return j.get<std::string>(); },
        "get_double", [](Json& j){ return j.get<double>(); },
        "get_float", [](Json& j){ return j.get<float>(); },
        "get_int", [](Json& j){ return j.get<int>(); },
        "get_bool", [](Json& j){ return j.get<bool>(); },
        "empty", &Json::empty,
        "size", &Json::size,
        sol::meta_function::index, sol::overload(
            [](Json& j, size_t i){ return j[i]; },
            [](Json& j, const std::string& s){ return j[s]; }
        ),
        sol::meta_function::new_index, sol::overload(
            [](Json& j, size_t i, bool v){ return j[i] = v; },
            [](Json& j, size_t i, int v){ return j[i] = v; },
            [](Json& j, size_t i, float v){ return j[i] = v; },
            [](Json& j, size_t i, long v){ return j[i] = v; },
            [](Json& j, size_t i, double v){ return j[i] = v; },
            [](Json& j, size_t i, const std::string& v){ return j[i] = v; },
            [](Json& j, size_t i, const Json& v){ return j[i] = v; },
            // [](Json& j, size_t i, const sol::object& v){ return j[i] = v; },
            [](Json& j, const std::string& s, bool v){ return j[s] = v; },
            [](Json& j, const std::string& s, int v){ return j[s] = v; },
            [](Json& j, const std::string& s, float v){ return j[s] = v; },
            [](Json& j, const std::string& s, long v){ return j[s] = v; },
            [](Json& j, const std::string& s, double v){ return j[s] = v; },
            [](Json& j, const std::string& s, const std::string& v){ return j[s] = v; },
            [](Json& j, const std::string& s, const Json& v){ return j[s] = v; },
            [](Json& j, size_t i, const sol::table& tbl){
                json_new_index_table_fn(j, i, tbl);
            },
            [](Json& j, const std::string& i, const sol::table& tbl){
                json_new_index_table_fn(j, i, tbl);
            }
        )
    );

    lua->set_function("loadXml", &trussc::loadXml);
    lua->set_function("parseXml", &trussc::parseXml);

    sol::usertype<Xml> xml_t = lua->new_usertype<Xml>("Xml",
        sol::constructors<Xml()>(),
        "load", &Xml::load,
        "parse", &Xml::parse,
        "save", sol::overload(
            [](Xml& x, const std::string& s){ return x.save(s); },
            [](Xml& x, const std::string& s, const std::string& i){ return x.save(s, i); }
        ),
        "toString", sol::overload(
            [](Xml& x){ return x.toString(); },
            [](Xml& x, const std::string& i){ return x.toString(i); }
        ),
        "root", [](Xml& x){ return x.root(); },
        "addRoot", &Xml::addRoot,
        "child", &Xml::child,
        "document", [](Xml& x) -> XmlDocument& { return x.document(); },
        "empty", &Xml::empty,
        "addDeclaration", sol::overload(
            [](Xml& x){ return x.addDeclaration(); },
            [](Xml& x, const std::string& a){ return x.addDeclaration(a); },
            [](Xml& x, const std::string& a, const std::string& b){ return x.addDeclaration(a, b); }
        )
    );

    sol::usertype<XmlAttribute> xmlattr_t = lua->new_usertype<XmlAttribute>("XmlAttribute",
        sol::constructors<XmlAttribute()>(),
        "set", sol::overload( // WORKAROUND
            [](XmlAttribute& x, pugi::string_view_t s){ return (x = s); },
            [](XmlAttribute& x, const pugi::char_t* s){ return (x = s); },
            [](XmlAttribute& x, int s){ return (x = s); },
            [](XmlAttribute& x, float s){ return (x = s); },
            [](XmlAttribute& x, long s){ return (x = s); }
        ),
        "value", &XmlAttribute::value
    );

    using XmlText = pugi::xml_text;

    sol::usertype<XmlText> xmltext_t = lua->new_usertype<XmlText>("XmlText",
        sol::constructors<XmlText()>(),
        "set", sol::overload( // WORKAROUND
            [](XmlText& x, pugi::string_view_t s){ return (x = s); },
            [](XmlText& x, const pugi::char_t* s){ return (x = s); },
            [](XmlText& x, int s){ return (x = s); },
            [](XmlText& x, float s){ return (x = s); },
            [](XmlText& x, long s){ return (x = s); }
        ),
        "get", &XmlText::get
    );

    sol::usertype<XmlNode> xmlnode_t = lua->new_usertype<XmlNode>("XmlNode",
        sol::constructors<XmlNode()>(),
        "append_attribute", sol::overload(
            [](XmlNode& x, pugi::string_view_t n){ return x.append_attribute(n); },
            [](XmlNode& x, const pugi::char_t* n){ return x.append_attribute(n); }
        ),
        "prepend_attribute", sol::overload(
            [](XmlNode& x, pugi::string_view_t n){ return x.prepend_attribute(n); },
            [](XmlNode& x, const pugi::char_t* n){ return x.prepend_attribute(n); }
        ),
        "append_child", sol::overload(
            [](XmlNode& x, pugi::string_view_t n){ return x.append_child(n); },
            [](XmlNode& x, const pugi::char_t* n){ return x.append_child(n); }
        ),
        "prepend_child", sol::overload(
            [](XmlNode& x, pugi::string_view_t n){ return x.prepend_child(n); },
            [](XmlNode& x, const pugi::char_t* n){ return x.prepend_child(n); }
        ),
        "attribute", sol::overload(
            [](XmlNode& x, pugi::string_view_t n){ return x.attribute(n); },
            [](XmlNode& x, const pugi::char_t* n){ return x.attribute(n); }
        ),
        "child", sol::overload(
            [](XmlNode& x, pugi::string_view_t n){ return x.child(n); },
            [](XmlNode& x, const pugi::char_t* n){ return x.child(n); }
        ),
        "text", &XmlNode::text,
        "first_child", &XmlNode::first_child,
        "last_child", &XmlNode::last_child,
        "first_attribute", &XmlNode::first_attribute,
        "last_attribute", &XmlNode::last_attribute,
        "remove_children", &XmlNode::remove_children,
        // "children", [](XmlNode& x){ return x.children(); }
        "children", [lua](XmlNode& x){ // WORKAROUND for ipairs etc
            auto&& tbl = lua->create_table();
            for(auto&& n : x.children()){
                tbl.add(n);
            }
            return tbl;
        }
    );

    sol::usertype<LogLevel> loglevel_t = lua->new_usertype<LogLevel>("LogLevel",
        sol::meta_function::equal_to, [](LogLevel a, LogLevel b){ return a == b; }
    );
    loglevel_t["Verbose"] = sol::var(LogLevel::Verbose);
    loglevel_t["Notice"] = sol::var(LogLevel::Notice);
    loglevel_t["Warning"] = sol::var(LogLevel::Warning);
    loglevel_t["Error"] = sol::var(LogLevel::Error);
    loglevel_t["Fatal"] = sol::var(LogLevel::Fatal);
    loglevel_t["Silent"] = sol::var(LogLevel::Silent);

    sol::usertype<Font> font_t = lua->new_usertype<Font>("Font",
        sol::constructors<Font(), Font(const Font&), Font(Font&&)>(),
        "load", &Font::load
    );

    sol::usertype<Rect> rect_t = lua->new_usertype<Rect>("Rect",
        sol::constructors<Rect(),
            Rect(float, float, float, float),
            Rect(float, float, float, float, float),
            Rect(const Vec2&, float, float),
            Rect(const Vec3&, float, float),
            Rect(const Rect&), Rect(Rect&&)>(),
        "x", &Rect::x,
        "y", &Rect::y,
        "width", &Rect::width,
        "height", &Rect::height,
        "getRight", &Rect::getRight,
        "getBottom", &Rect::getBottom,
        "getCenter", &Rect::getCenter,
        "getCenterX", &Rect::getCenterX,
        "getCenterY", &Rect::getCenterY,
        "contains", &Rect::contains,
        "intersects", &Rect::intersects
    );
    
    sol::usertype<Path> path_t = lua->new_usertype<Path>("Path",
        sol::constructors<Path(),
            Path(const std::vector<Vec2>&),
            Path(const std::vector<Vec3>&),
            Path(const Path&), Path(Path&&)>(),
        "addVertex", sol::overload(
            [](Path& p, float x, float y){ return p.addVertex(x, y); },
            [](Path& p, float x, float y, float z){ return p.addVertex(x, y, z); },
            [](Path& p, const Vec2& v){ return p.addVertex(v); },
            [](Path& p, const Vec3& v){ return p.addVertex(v); }
        ),
        "addVertices", sol::overload(
            [](Path& p, const std::vector<Vec2>& v){ return p.addVertices(v); },
            [](Path& p, const std::vector<Vec3>& v){ return p.addVertices(v); }
        ),
        "getVertices", [](Path& p){ return p.getVertices(); },
        "size", &Path::size,
        "empty", &Path::empty,
        sol::meta_function::index,  [](Path& p, int index){ return p[index]; },
        "clear", &Path::clear,
        "lineTo", sol::overload(
            [](Path& p, float x, float y){ return p.lineTo(x, y); },
            [](Path& p, float x, float y, float z){ return p.lineTo(x, y, z); },
            [](Path& p, const Vec2& v){ return p.lineTo(v); },
            [](Path& p, const Vec3& v){ return p.lineTo(v); }
        ),
        "bezierTo", sol::overload(
            [](Path& p, const Vec2& a, const Vec2& b, const Vec2& c){ return p.bezierTo(a, b, c); },
            [](Path& p, const Vec2& a, const Vec2& b, const Vec2& c, int d){ return p.bezierTo(a, b, c, d); },
            [](Path& p, const Vec3& a, const Vec3& b, const Vec3& c){ return p.bezierTo(a, b, c); },
            [](Path& p, const Vec3& a, const Vec3& b, const Vec3& c, int d){ return p.bezierTo(a, b, c, d); },
            [](Path& p,float a,float b,float c,float d,float e,float f){ return p.bezierTo(a,b,c,d,e,f); },
            [](Path& p,float a,float b,float c,float d,float e,float f,int g){ return p.bezierTo(a,b,c,d,e,f,g); }
        ),
        "quadBezierTo", sol::overload(
            [](Path& p, const Vec2& a, const Vec2& b){ return p.quadBezierTo(a, b); },
            [](Path& p, const Vec2& a, const Vec2& b, int d){ return p.quadBezierTo(a, b, d); },
            [](Path& p, const Vec3& a, const Vec3& b){ return p.quadBezierTo(a, b); },
            [](Path& p, const Vec3& a, const Vec3& b, int d){ return p.quadBezierTo(a, b, d); },
            [](Path& p,float a,float b,float c,float d){ return p.quadBezierTo(a,b,c,d); },
            [](Path& p,float a,float b,float c,float d,int g){ return p.quadBezierTo(a,b,c,d,g); }
        ),
        "curveTo", sol::overload(
            [](Path& p, float x, float y){ return p.curveTo(x, y); },
            [](Path& p, float x, float y, float z){ return p.curveTo(x, y, z); },
            [](Path& p, const Vec2& v){ return p.curveTo(v); },
            [](Path& p, const Vec3& v){ return p.curveTo(v); }
        ),
        "arc", sol::overload(
            [](Path& p, const Vec3& v, float a, float b, float c, float d) { return p.arc(v,a,b,c,d); },
            [](Path& p, const Vec3& v, float a, float b, float c, float d, bool e) { return p.arc(v,a,b,c,d,e); },
            [](Path& p, const Vec3& v, float a, float b, float c, float d, bool e, int f) { return p.arc(v,a,b,c,d,e,f); },
            [](Path& p, const Vec2& v, float a, float b, float c, float d, int f) { return p.arc(v,a,b,c,d,f); },
            [](Path& p, float a, float b, float c, float d, float e, float f, int g) { return p.arc(a,b,c,d,e,f,g); },
            [](Path& p, float a, float b, float c, float d, float e, float f) { return p.arc(a,b,c,d,e,f); }
        ),
        "close", &Path::close,
        "setClosed", &Path::setClosed,
        "isClosed", &Path::isClosed,
        "draw", &Path::draw,
        "getBounds", &Path::getBounds,
        "getPerimeter", &Path::getPerimeter
    );

    sol::usertype<EaseType> easetype_t = lua->new_usertype<EaseType>("EaseType",
        sol::meta_function::equal_to, [](EaseType a, EaseType b){ return a == b; },
        "Linear", sol::var(EaseType::Linear),
        "Quad", sol::var(EaseType::Quad),
        "Cubic", sol::var(EaseType::Cubic),
        "Quart", sol::var(EaseType::Quart),
        "Quint", sol::var(EaseType::Quint),
        "Sine", sol::var(EaseType::Sine),
        "Expo", sol::var(EaseType::Expo),
        "Circ", sol::var(EaseType::Circ),
        "Back", sol::var(EaseType::Back),
        "Elastic", sol::var(EaseType::Elastic),
        "Bounce", sol::var(EaseType::Bounce)
    );
    sol::usertype<EaseMode> easemode_t = lua->new_usertype<EaseMode>("EaseMode",
        sol::meta_function::equal_to, [](EaseMode a, EaseMode b){ return a == b; },
        "In", sol::var(EaseMode::In),
        "Out", sol::var(EaseMode::Out),
        "InOut", sol::var(EaseMode::InOut)
    );

    defineTween<Tween<float>, float>(lua, "TweenFloat");
    defineTween<Tween<Vec2>, Vec2>(lua, "TweenVec2");
    defineTween<Tween<Vec3>, Vec3>(lua, "TweenVec3");
    defineTween<Tween<Color>, Color>(lua, "TweenColor");
}

struct Colors{};

void tcxLua::setColorConstBindings(const std::shared_ptr<sol::state>& lua){
    auto&& l = *lua;
    sol::usertype<Colors> colors_type = lua->new_usertype<Colors>("colors");
    colors_type["white"] = sol::var(colors::white);
    colors_type["black"] = sol::var(colors::black);
    colors_type["red"] = sol::var(colors::red);
    colors_type["green"] = sol::var(colors::green);
    colors_type["blue"] = sol::var(colors::blue);
    colors_type["yellow"] = sol::var(colors::yellow);
    colors_type["cyan"] = sol::var(colors::cyan);
    colors_type["magenta"] = sol::var(colors::magenta);
    colors_type["transparent"] = sol::var(colors::transparent);
    colors_type["gray"] = sol::var(colors::gray);
    colors_type["darkGray"] = sol::var(colors::darkGray);
    colors_type["dimGray"] = sol::var(colors::dimGray);
    colors_type["lightGray"] = sol::var(colors::lightGray);
    colors_type["silver"] = sol::var(colors::silver);
    colors_type["gainsboro"] = sol::var(colors::gainsboro);
    colors_type["whiteSmoke"] = sol::var(colors::whiteSmoke);
    colors_type["darkRed"] = sol::var(colors::darkRed);
    colors_type["fireBrick"] = sol::var(colors::fireBrick);
    colors_type["crimson"] = sol::var(colors::crimson);
    colors_type["indianRed"] = sol::var(colors::indianRed);
    colors_type["lightCoral"] = sol::var(colors::lightCoral);
    colors_type["salmon"] = sol::var(colors::salmon);
    colors_type["darkSalmon"] = sol::var(colors::darkSalmon);
    colors_type["lightSalmon"] = sol::var(colors::lightSalmon);
    colors_type["orange"] = sol::var(colors::orange);
    colors_type["darkOrange"] = sol::var(colors::darkOrange);
    colors_type["orangeRed"] = sol::var(colors::orangeRed);
    colors_type["tomato"] = sol::var(colors::tomato);
    colors_type["coral"] = sol::var(colors::coral);
    colors_type["gold"] = sol::var(colors::gold);
    colors_type["goldenRod"] = sol::var(colors::goldenRod);
    colors_type["darkGoldenRod"] = sol::var(colors::darkGoldenRod);
    colors_type["paleGoldenRod"] = sol::var(colors::paleGoldenRod);
    colors_type["lightGoldenRodYellow"] = sol::var(colors::lightGoldenRodYellow);
    colors_type["khaki"] = sol::var(colors::khaki);
    colors_type["darkKhaki"] = sol::var(colors::darkKhaki);
    colors_type["lime"] = sol::var(colors::lime);
    colors_type["limeGreen"] = sol::var(colors::limeGreen);
    colors_type["lightGreen"] = sol::var(colors::lightGreen);
    colors_type["paleGreen"] = sol::var(colors::paleGreen);
    colors_type["darkGreen"] = sol::var(colors::darkGreen);
    colors_type["forestGreen"] = sol::var(colors::forestGreen);
    colors_type["seaGreen"] = sol::var(colors::seaGreen);
    colors_type["mediumSeaGreen"] = sol::var(colors::mediumSeaGreen);
    colors_type["darkSeaGreen"] = sol::var(colors::darkSeaGreen);
    colors_type["lightSeaGreen"] = sol::var(colors::lightSeaGreen);
    colors_type["springGreen"] = sol::var(colors::springGreen);
    colors_type["mediumSpringGreen"] = sol::var(colors::mediumSpringGreen);
    colors_type["greenYellow"] = sol::var(colors::greenYellow);
    colors_type["yellowGreen"] = sol::var(colors::yellowGreen);
    colors_type["chartreuse"] = sol::var(colors::chartreuse);
    colors_type["lawnGreen"] = sol::var(colors::lawnGreen);
    colors_type["olive"] = sol::var(colors::olive);
    colors_type["oliveDrab"] = sol::var(colors::oliveDrab);
    colors_type["darkOliveGreen"] = sol::var(colors::darkOliveGreen);
    colors_type["aqua"] = sol::var(colors::aqua);
    colors_type["aquamarine"] = sol::var(colors::aquamarine);
    colors_type["mediumAquaMarine"] = sol::var(colors::mediumAquaMarine);
    colors_type["darkCyan"] = sol::var(colors::darkCyan);
    colors_type["teal"] = sol::var(colors::teal);
    colors_type["lightCyan"] = sol::var(colors::lightCyan);
    colors_type["turquoise"] = sol::var(colors::turquoise);
    colors_type["mediumTurquoise"] = sol::var(colors::mediumTurquoise);
    colors_type["darkTurquoise"] = sol::var(colors::darkTurquoise);
    colors_type["paleTurquoise"] = sol::var(colors::paleTurquoise);
    colors_type["navy"] = sol::var(colors::navy);
    colors_type["darkBlue"] = sol::var(colors::darkBlue);
    colors_type["mediumBlue"] = sol::var(colors::mediumBlue);
    colors_type["royalBlue"] = sol::var(colors::royalBlue);
    colors_type["steelBlue"] = sol::var(colors::steelBlue);
    colors_type["blueSteel"] = sol::var(colors::blueSteel);
    colors_type["lightSteelBlue"] = sol::var(colors::lightSteelBlue);
    colors_type["dodgerBlue"] = sol::var(colors::dodgerBlue);
    colors_type["deepSkyBlue"] = sol::var(colors::deepSkyBlue);
    colors_type["skyBlue"] = sol::var(colors::skyBlue);
    colors_type["lightSkyBlue"] = sol::var(colors::lightSkyBlue);
    colors_type["lightBlue"] = sol::var(colors::lightBlue);
    colors_type["powderBlue"] = sol::var(colors::powderBlue);
    colors_type["cornflowerBlue"] = sol::var(colors::cornflowerBlue);
    colors_type["cadetBlue"] = sol::var(colors::cadetBlue);
    colors_type["midnightBlue"] = sol::var(colors::midnightBlue);
    colors_type["aliceBlue"] = sol::var(colors::aliceBlue);
    colors_type["purple"] = sol::var(colors::purple);
    colors_type["darkMagenta"] = sol::var(colors::darkMagenta);
    colors_type["darkViolet"] = sol::var(colors::darkViolet);
    colors_type["blueViolet"] = sol::var(colors::blueViolet);
    colors_type["indigo"] = sol::var(colors::indigo);
    colors_type["slateBlue"] = sol::var(colors::slateBlue);
    colors_type["darkSlateBlue"] = sol::var(colors::darkSlateBlue);
    colors_type["mediumSlateBlue"] = sol::var(colors::mediumSlateBlue);
    colors_type["mediumPurple"] = sol::var(colors::mediumPurple);
    colors_type["darkOrchid"] = sol::var(colors::darkOrchid);
    colors_type["mediumOrchid"] = sol::var(colors::mediumOrchid);
    colors_type["orchid"] = sol::var(colors::orchid);
    colors_type["violet"] = sol::var(colors::violet);
    colors_type["plum"] = sol::var(colors::plum);
    colors_type["thistle"] = sol::var(colors::thistle);
    colors_type["lavender"] = sol::var(colors::lavender);
    colors_type["fuchsia"] = sol::var(colors::fuchsia);
    colors_type["pink"] = sol::var(colors::pink);
    colors_type["lightPink"] = sol::var(colors::lightPink);
    colors_type["hotPink"] = sol::var(colors::hotPink);
    colors_type["deepPink"] = sol::var(colors::deepPink);
    colors_type["mediumVioletRed"] = sol::var(colors::mediumVioletRed);
    colors_type["paleVioletRed"] = sol::var(colors::paleVioletRed);
    colors_type["brown"] = sol::var(colors::brown);
    colors_type["maroon"] = sol::var(colors::maroon);
    colors_type["saddleBrown"] = sol::var(colors::saddleBrown);
    colors_type["sienna"] = sol::var(colors::sienna);
    colors_type["chocolate"] = sol::var(colors::chocolate);
    colors_type["peru"] = sol::var(colors::peru);
    colors_type["sandyBrown"] = sol::var(colors::sandyBrown);
    colors_type["burlyWood"] = sol::var(colors::burlyWood);
    colors_type["tan"] = sol::var(colors::tan);
    colors_type["rosyBrown"] = sol::var(colors::rosyBrown);
    colors_type["snow"] = sol::var(colors::snow);
    colors_type["honeyDew"] = sol::var(colors::honeyDew);
    colors_type["mintCream"] = sol::var(colors::mintCream);
    colors_type["azure"] = sol::var(colors::azure);
    colors_type["ghostWhite"] = sol::var(colors::ghostWhite);
    colors_type["floralWhite"] = sol::var(colors::floralWhite);
    colors_type["ivory"] = sol::var(colors::ivory);
    colors_type["antiqueWhite"] = sol::var(colors::antiqueWhite);
    colors_type["linen"] = sol::var(colors::linen);
    colors_type["lavenderBlush"] = sol::var(colors::lavenderBlush);
    colors_type["mistyRose"] = sol::var(colors::mistyRose);
    colors_type["oldLace"] = sol::var(colors::oldLace);
    colors_type["seaShell"] = sol::var(colors::seaShell);
    colors_type["beige"] = sol::var(colors::beige);
    colors_type["cornsilk"] = sol::var(colors::cornsilk);
    colors_type["lemonChiffon"] = sol::var(colors::lemonChiffon);
    colors_type["lightYellow"] = sol::var(colors::lightYellow);
    colors_type["wheat"] = sol::var(colors::wheat);
    colors_type["moccasin"] = sol::var(colors::moccasin);
    colors_type["peachPuff"] = sol::var(colors::peachPuff);
    colors_type["papayaWhip"] = sol::var(colors::papayaWhip);
    colors_type["blanchedAlmond"] = sol::var(colors::blanchedAlmond);
    colors_type["bisque"] = sol::var(colors::bisque);
    colors_type["navajoWhite"] = sol::var(colors::navajoWhite);
    colors_type["slateGray"] = sol::var(colors::slateGray);
    colors_type["lightSlateGray"] = sol::var(colors::lightSlateGray);
    colors_type["darkSlateGray"] = sol::var(colors::darkSlateGray);
}

void tcxLua::setMathBindings(const std::shared_ptr<sol::state>& lua){
    auto&& l = *lua;
    lua->set_function("sin", [](float v){ return sin(v); });
    lua->set_function("cos", [](float v){ return cos(v); });
    lua->set_function("tan", [](float v){ return tan(v); });
    lua->set_function("atan", [](float v){ return atan(v); });
    lua->set_function("atan2", [](float a, float b){ return atan2(a, b); });
    lua->set_function("atanh", [](float v){ return atanh(v); });
    lua->set_function("sqrt", [](float v){ return sqrt(v); });
    lua->set_function("tanh", [](float v){ return tanh(v); });
    lua->set_function("acos", [](float v){ return acos(v); });
    lua->set_function("asin", [](float v){ return asin(v); });

    lua->set_function("exp", [](float v){ return exp(v); });
    lua->set_function("exp2", [](float v){ return exp2(v); });
    lua->set_function("abs", [](float v){ return fabs(v); });

    lua->set_function("log", [](float v){ return log(v); });
    lua->set_function("log2", [](float v){ return log2(v); });
    lua->set_function("log10", [](float v){ return log10(v); });

    lua->set_function("ceil", [](float v){ return ceil(v); });
    lua->set_function("floor", [](float v){ return floor(v); });
    lua->set_function("round", [](float v){ return round(v); });
    lua->set_function("trunc", [](float v){ return trunc(v); });

    lua->set_function("pow", [](float a, float b){ return pow(a, b); });
    lua->set_function("remainder", [](float a, float b){ return remainder(a, b); });

    lua->set_function("fmod", [](float a, float b){ return fmod(a, b); });
    lua->set_function("max", [](float a, float b){ return std::max(a, b); });
    lua->set_function("min", [](float a, float b){ return std::min(a, b); });
}

// } // namespace tcx::lua

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif // #ifndef _MSC_VER
