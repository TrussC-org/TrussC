// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_01(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Path> t = lua->new_usertype<trussc::Path>("Path",
            sol::constructors<trussc::Path(), trussc::Path(const std::vector<trussc::Vec2> &), trussc::Path(const std::vector<trussc::Vec3> &)>(),
            sol::call_constructor, sol::constructors<trussc::Path(), trussc::Path(const std::vector<trussc::Vec2> &), trussc::Path(const std::vector<trussc::Vec3> &)>(),
            sol::meta_function::index, [](const trussc::Path& a, int b){ return a[b]; });
        t["addVertex"] = sol::overload([](trussc::Path& self, float x, float y) { return self.addVertex(x, y); }, [](trussc::Path& self, float x, float y, float z) { return self.addVertex(x, y, z); }, [](trussc::Path& self, const trussc::Vec2 & v) { return self.addVertex(v); }, [](trussc::Path& self, const trussc::Vec3 & v) { return self.addVertex(v); });
        t["addVertices"] = sol::overload([](trussc::Path& self, const std::vector<trussc::Vec2> & verts) { return self.addVertices(verts); }, [](trussc::Path& self, const std::vector<trussc::Vec3> & verts) { return self.addVertices(verts); });
        t["getVertices"] = [](trussc::Path& self) -> decltype(auto) { return self.getVertices(); };
        t["size"] = &trussc::Path::size;
        t["empty"] = &trussc::Path::empty;
        t["clear"] = &trussc::Path::clear;
        t["moveTo"] = sol::overload([](trussc::Path& self, float x, float y) { return self.moveTo(x, y); }, [](trussc::Path& self, float x, float y, float z) { return self.moveTo(x, y, z); }, [](trussc::Path& self, const trussc::Vec2 & p) { return self.moveTo(p); }, [](trussc::Path& self, const trussc::Vec3 & p) { return self.moveTo(p); });
        t["getNumSubpaths"] = &trussc::Path::getNumSubpaths;
        t["getSubpathRange"] = &trussc::Path::getSubpathRange;
        t["isSubpathClosed"] = &trussc::Path::isSubpathClosed;
        t["lineTo"] = sol::overload([](trussc::Path& self, float x, float y) { return self.lineTo(x, y); }, [](trussc::Path& self, float x, float y, float z) { return self.lineTo(x, y, z); }, [](trussc::Path& self, const trussc::Vec2 & p) { return self.lineTo(p); }, [](trussc::Path& self, const trussc::Vec3 & p) { return self.lineTo(p); });
        t["bezierTo"] = sol::overload([](trussc::Path& self, const trussc::Vec3 & cp1, const trussc::Vec3 & cp2, const trussc::Vec3 & to, int resolution) { return self.bezierTo(cp1, cp2, to, resolution); }, [](trussc::Path& self, float cx1, float cy1, float cx2, float cy2, float x, float y, int resolution) { return self.bezierTo(cx1, cy1, cx2, cy2, x, y, resolution); }, [](trussc::Path& self, const trussc::Vec2 & cp1, const trussc::Vec2 & cp2, const trussc::Vec2 & to, int resolution) { return self.bezierTo(cp1, cp2, to, resolution); });
        t["quadBezierTo"] = sol::overload([](trussc::Path& self, const trussc::Vec3 & cp, const trussc::Vec3 & to, int resolution) { return self.quadBezierTo(cp, to, resolution); }, [](trussc::Path& self, float cx, float cy, float x, float y, int resolution) { return self.quadBezierTo(cx, cy, x, y, resolution); }, [](trussc::Path& self, const trussc::Vec2 & cp, const trussc::Vec2 & to, int resolution) { return self.quadBezierTo(cp, to, resolution); });
        t["curveTo"] = sol::overload([](trussc::Path& self, const trussc::Vec3 & to, int resolution) { return self.curveTo(to, resolution); }, [](trussc::Path& self, float x, float y) { return self.curveTo(x, y); }, [](trussc::Path& self, float x, float y, float z) { return self.curveTo(x, y, z); }, [](trussc::Path& self, float x, float y, float z, int resolution) { return self.curveTo(x, y, z, resolution); }, [](trussc::Path& self, const trussc::Vec2 & to, int resolution) { return self.curveTo(to, resolution); });
        t["arc"] = sol::overload([](trussc::Path& self, const trussc::Vec3 & center, float radiusX, float radiusY, float angleBegin, float angleEnd) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec3 & center, float radiusX, float radiusY, float angleBegin, float angleEnd, bool clockwise) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd, clockwise); }, [](trussc::Path& self, const trussc::Vec3 & center, float radiusX, float radiusY, float angleBegin, float angleEnd, bool clockwise, int circleResolution) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd, clockwise, circleResolution); }, [](trussc::Path& self, float x, float y, float radiusX, float radiusY, float angleBegin, float angleEnd) { return self.arc(x, y, radiusX, radiusY, angleBegin, angleEnd); }, [](trussc::Path& self, float x, float y, float radiusX, float radiusY, float angleBegin, float angleEnd, int circleResolution) { return self.arc(x, y, radiusX, radiusY, angleBegin, angleEnd, circleResolution); }, [](trussc::Path& self, const trussc::Vec2 & center, float radiusX, float radiusY, float angleBegin, float angleEnd) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec2 & center, float radiusX, float radiusY, float angleBegin, float angleEnd, int circleResolution) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd, circleResolution); }, [](trussc::Path& self, const trussc::Vec3 & center, float radius, float angleBegin, float angleEnd) { return self.arc(center, radius, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec3 & center, float radius, float angleBegin, float angleEnd, bool clockwise) { return self.arc(center, radius, angleBegin, angleEnd, clockwise); }, [](trussc::Path& self, float x, float y, float radius, float angleBegin, float angleEnd) { return self.arc(x, y, radius, angleBegin, angleEnd); }, [](trussc::Path& self, float x, float y, float radius, float angleBegin, float angleEnd, bool clockwise) { return self.arc(x, y, radius, angleBegin, angleEnd, clockwise); }, [](trussc::Path& self, const trussc::Vec2 & center, float radius, float angleBegin, float angleEnd) { return self.arc(center, radius, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec2 & center, float radius, float angleBegin, float angleEnd, bool clockwise) { return self.arc(center, radius, angleBegin, angleEnd, clockwise); });
        t["close"] = &trussc::Path::close;
        t["setClosed"] = &trussc::Path::setClosed;
        t["isClosed"] = &trussc::Path::isClosed;
        t["reverseWinding"] = sol::overload([](trussc::Path& self, size_t i) -> decltype(auto) { return self.reverseWinding(i); }, [](trussc::Path& self) -> decltype(auto) { return self.reverseWinding(); });
        t["draw"] = &trussc::Path::draw;
        t["buildFillTriangles"] = &trussc::Path::buildFillTriangles;
        t["drawFill"] = &trussc::Path::drawFill;
        t["toFillMesh"] = &trussc::Path::toFillMesh;
        t["drawStroke"] = &trussc::Path::drawStroke;
        t["getBounds"] = &trussc::Path::getBounds;
        t["getPerimeter"] = &trussc::Path::getPerimeter;
    }
    {
        sol::usertype<trussc::KeyEventArgs> t = lua->new_usertype<trussc::KeyEventArgs>("KeyEventArgs");
        t["key"] = &trussc::KeyEventArgs::key;
        t["isRepeat"] = &trussc::KeyEventArgs::isRepeat;
        t["shift"] = &trussc::KeyEventArgs::shift;
        t["ctrl"] = &trussc::KeyEventArgs::ctrl;
        t["alt"] = &trussc::KeyEventArgs::alt;
        t["super"] = &trussc::KeyEventArgs::super;
        t["consumed"] = &trussc::KeyEventArgs::consumed;
    }
    lua->new_usertype<trussc::KinsokuLevel>("KinsokuLevel",
        sol::meta_function::equal_to, [](trussc::KinsokuLevel a, trussc::KinsokuLevel b){ return a == b; },
        "Off", sol::var(trussc::KinsokuLevel::Off),
        "PunctuationOnly", sol::var(trussc::KinsokuLevel::PunctuationOnly),
        "Standard", sol::var(trussc::KinsokuLevel::Standard));
    lua->new_usertype<trussc::AxisMode>("AxisMode",
        sol::meta_function::equal_to, [](trussc::AxisMode a, trussc::AxisMode b){ return a == b; },
        "None", sol::var(trussc::AxisMode::None),
        "Fill", sol::var(trussc::AxisMode::Fill),
        "Content", sol::var(trussc::AxisMode::Content));
    {
        sol::usertype<trussc::TcpErrorEventArgs> t = lua->new_usertype<trussc::TcpErrorEventArgs>("TcpErrorEventArgs");
        t["message"] = &trussc::TcpErrorEventArgs::message;
        t["errorCode"] = &trussc::TcpErrorEventArgs::errorCode;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
