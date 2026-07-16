// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_15(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::LayoutMod> t = lua->new_usertype<trussc::LayoutMod>("LayoutMod",
            sol::constructors<trussc::LayoutMod(), trussc::LayoutMod(trussc::LayoutDirection), trussc::LayoutMod(trussc::LayoutDirection, float)>(),
            sol::call_constructor, sol::constructors<trussc::LayoutMod(), trussc::LayoutMod(trussc::LayoutDirection), trussc::LayoutMod(trussc::LayoutDirection, float)>());
        t["getDirection"] = &trussc::LayoutMod::getDirection;
        t["setDirection"] = &trussc::LayoutMod::setDirection;
        t["getSpacing"] = &trussc::LayoutMod::getSpacing;
        t["setSpacing"] = &trussc::LayoutMod::setSpacing;
        t["getCrossAxis"] = &trussc::LayoutMod::getCrossAxis;
        t["setCrossAxis"] = &trussc::LayoutMod::setCrossAxis;
        t["getMainAxis"] = &trussc::LayoutMod::getMainAxis;
        t["setMainAxis"] = &trussc::LayoutMod::setMainAxis;
        t["getPaddingLeft"] = &trussc::LayoutMod::getPaddingLeft;
        t["getPaddingTop"] = &trussc::LayoutMod::getPaddingTop;
        t["getPaddingRight"] = &trussc::LayoutMod::getPaddingRight;
        t["getPaddingBottom"] = &trussc::LayoutMod::getPaddingBottom;
        t["setPadding"] = sol::overload([](trussc::LayoutMod& self, float padding) -> decltype(auto) { return self.setPadding(padding); }, [](trussc::LayoutMod& self, float vertical, float horizontal) -> decltype(auto) { return self.setPadding(vertical, horizontal); }, [](trussc::LayoutMod& self, float top, float right, float bottom, float left) -> decltype(auto) { return self.setPadding(top, right, bottom, left); });
        t["setPaddingLeft"] = &trussc::LayoutMod::setPaddingLeft;
        t["setPaddingTop"] = &trussc::LayoutMod::setPaddingTop;
        t["setPaddingRight"] = &trussc::LayoutMod::setPaddingRight;
        t["setPaddingBottom"] = &trussc::LayoutMod::setPaddingBottom;
        t["updateLayout"] = &trussc::LayoutMod::updateLayout;
    }
    {
        sol::usertype<trussc::StrokeMesh> t = lua->new_usertype<trussc::StrokeMesh>("StrokeMesh",
            sol::constructors<trussc::StrokeMesh(), trussc::StrokeMesh(const trussc::Path &)>(),
            sol::call_constructor, sol::constructors<trussc::StrokeMesh(), trussc::StrokeMesh(const trussc::Path &)>());
        t["setWidth"] = &trussc::StrokeMesh::setWidth;
        t["setColor"] = &trussc::StrokeMesh::setColor;
        t["setCapType"] = &trussc::StrokeMesh::setCapType;
        t["setJoinType"] = &trussc::StrokeMesh::setJoinType;
        t["setMiterLimit"] = &trussc::StrokeMesh::setMiterLimit;
        t["addVertex"] = sol::overload([](trussc::StrokeMesh& self, float x, float y) -> decltype(auto) { return self.addVertex(x, y); }, [](trussc::StrokeMesh& self, float x, float y, float z) -> decltype(auto) { return self.addVertex(x, y, z); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p) -> decltype(auto) { return self.addVertex(p); }, [](trussc::StrokeMesh& self, const trussc::Vec2 & p) -> decltype(auto) { return self.addVertex(p); });
        t["addVertexWithWidth"] = sol::overload([](trussc::StrokeMesh& self, float x, float y, float width) -> decltype(auto) { return self.addVertexWithWidth(x, y, width); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p, float width) -> decltype(auto) { return self.addVertexWithWidth(p, width); });
        t["setWidths"] = &trussc::StrokeMesh::setWidths;
        t["setShape"] = &trussc::StrokeMesh::setShape;
        t["setClosed"] = &trussc::StrokeMesh::setClosed;
        t["clear"] = &trussc::StrokeMesh::clear;
        t["update"] = &trussc::StrokeMesh::update;
        t["draw"] = &trussc::StrokeMesh::draw;
        t["getMesh"] = &trussc::StrokeMesh::getMesh;
        t["getPolylines"] = &trussc::StrokeMesh::getPolylines;
    }
    {
        sol::usertype<trussc::RectNode> t = lua->new_usertype<trussc::RectNode>("RectNode");
        t["mousePressed"] = &trussc::RectNode::mousePressed;
        t["mouseReleased"] = &trussc::RectNode::mouseReleased;
        t["mouseDragged"] = &trussc::RectNode::mouseDragged;
        t["mouseScrolled"] = &trussc::RectNode::mouseScrolled;
        t["getWidth"] = &trussc::RectNode::getWidth;
        t["getHeight"] = &trussc::RectNode::getHeight;
        t["getSize"] = &trussc::RectNode::getSize;
        t["setWidth"] = &trussc::RectNode::setWidth;
        t["setHeight"] = &trussc::RectNode::setHeight;
        t["setSize"] = sol::overload([](trussc::RectNode& self, float w, float h) { return self.setSize(w, h); }, [](trussc::RectNode& self, float size) { return self.setSize(size); }, [](trussc::RectNode& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setRect"] = &trussc::RectNode::setRect;
        t["setClipping"] = &trussc::RectNode::setClipping;
        t["isClipping"] = &trussc::RectNode::isClipping;
        t["getLeft"] = &trussc::RectNode::getLeft;
        t["getRight"] = &trussc::RectNode::getRight;
        t["getTop"] = &trussc::RectNode::getTop;
        t["getBottom"] = &trussc::RectNode::getBottom;
        t["hitTest"] = [](trussc::RectNode& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["draw"] = &trussc::RectNode::draw;
    }
    {
        sol::usertype<trussc::Tween<trussc::Vec2>> t = lua->new_usertype<trussc::Tween<trussc::Vec2>>("Tween_Vec2",
            sol::constructors<trussc::Tween<trussc::Vec2>(), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType, trussc::EaseMode)>(),
            sol::call_constructor, sol::constructors<trussc::Tween<trussc::Vec2>(), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType, trussc::EaseMode)>());
    }
    lua->new_usertype<trussc::PrimitiveMode>("PrimitiveMode",
        sol::meta_function::equal_to, [](trussc::PrimitiveMode a, trussc::PrimitiveMode b){ return a == b; },
        "Triangles", sol::var(trussc::PrimitiveMode::Triangles),
        "TriangleStrip", sol::var(trussc::PrimitiveMode::TriangleStrip),
        "TriangleFan", sol::var(trussc::PrimitiveMode::TriangleFan),
        "Lines", sol::var(trussc::PrimitiveMode::Lines),
        "LineStrip", sol::var(trussc::PrimitiveMode::LineStrip),
        "LineLoop", sol::var(trussc::PrimitiveMode::LineLoop),
        "Points", sol::var(trussc::PrimitiveMode::Points));
    {
        sol::usertype<trussc::ShaderVertex> t = lua->new_usertype<trussc::ShaderVertex>("ShaderVertex");
        t["x"] = &trussc::ShaderVertex::x;
        t["y"] = &trussc::ShaderVertex::y;
        t["z"] = &trussc::ShaderVertex::z;
        t["u"] = &trussc::ShaderVertex::u;
        t["v"] = &trussc::ShaderVertex::v;
        t["r"] = &trussc::ShaderVertex::r;
        t["g"] = &trussc::ShaderVertex::g;
        t["b"] = &trussc::ShaderVertex::b;
        t["a"] = &trussc::ShaderVertex::a;
    }
    {
        sol::usertype<trussc::EventListener> t = lua->new_usertype<trussc::EventListener>("EventListener",
            sol::constructors<trussc::EventListener()>(),
            sol::call_constructor, sol::constructors<trussc::EventListener()>());
        t["disconnect"] = &trussc::EventListener::disconnect;
        t["isConnected"] = &trussc::EventListener::isConnected;
    }
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    {
        sol::usertype<trussc::CurveStyle> t = lua->new_usertype<trussc::CurveStyle>("CurveStyle");
        t["mode"] = &trussc::CurveStyle::mode;
        t["tolerance"] = &trussc::CurveStyle::tolerance;
        t["resolution"] = &trussc::CurveStyle::resolution;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
