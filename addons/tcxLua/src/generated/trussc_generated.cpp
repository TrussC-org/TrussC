
// WARNING: This file is auto-generated!

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

void tcxLua::setTrussCGeneratedBindings(const std::shared_ptr<sol::state>& lua){

    // tcMath.h, LINE 987
    lua->set_function("deg2rad", [](float deg){ return trussc::deg2rad(deg); });
    // tcMath.h, LINE 990
    lua->set_function("rad2deg", [](float rad){ return trussc::rad2deg(rad); });
    // tcMath.h, LINE 993
    lua->set_function("clamp", [](float value, float min, float max){ return trussc::clamp(value, min, max); });
    // tcMath.h, LINE 998
    lua->set_function("remap", [](float value, float inMin, float inMax, float outMin, float outMax){ return trussc::remap(value, inMin, inMax, outMin, outMax); });
    // tcMath.h, LINE 1003
    lua->set_function("sign", [](float value){ return trussc::sign(value); });
    // tcMath.h, LINE 1006
    lua->set_function("fract", [](float value){ return trussc::fract(value); });
    // tcMath.h, LINE 1009
    lua->set_function("sq", [](float value){ return trussc::sq(value); });
    // tcMath.h, LINE 1012
    // tcMath.h, LINE 1026
    // tcMath.h, LINE 1030
    lua->set_function("dist", sol::overload(
        [](float x1, float y1, float x2, float y2){ return trussc::dist(x1, y1, x2, y2); },
        [](const Vec2 & a, const Vec2 & b){ return trussc::dist(a, b); },
        [](const Vec3 & a, const Vec3 & b){ return trussc::dist(a, b); }
    ));
    // tcMath.h, LINE 1019
    // tcMath.h, LINE 1027
    // tcMath.h, LINE 1031
    lua->set_function("distSquared", sol::overload(
        [](float x1, float y1, float x2, float y2){ return trussc::distSquared(x1, y1, x2, y2); },
        [](const Vec2 & a, const Vec2 & b){ return trussc::distSquared(a, b); },
        [](const Vec3 & a, const Vec3 & b){ return trussc::distSquared(a, b); }
    ));
    // tcMath.h, LINE 1040
    lua->set_function("wrap", [](float value, float min, float max){ return trussc::wrap(value, min, max); });
    // tcMath.h, LINE 1051
    lua->set_function("angleDifference", [](float angle1, float angle2){ return trussc::angleDifference(angle1, angle2); });
    // tcMath.h, LINE 1060
    lua->set_function("angleDifferenceDeg", [](float deg1, float deg2){ return trussc::angleDifferenceDeg(deg1, deg2); });
    // tcMath.h, LINE 1080
    // tcMath.h, LINE 1086
    // tcMath.h, LINE 1092
    lua->set_function("random", sol::overload(
        [](){ return trussc::random(); },
        [](float max){ return trussc::random(max); },
        [](float min, float max){ return trussc::random(min, max); }
    ));
    // tcMath.h, LINE 1098
    // tcMath.h, LINE 1104
    lua->set_function("randomInt", sol::overload(
        [](int max){ return trussc::randomInt(max); },
        [](int min, int max){ return trussc::randomInt(min, max); }
    ));
    // tcMath.h, LINE 1110
    lua->set_function("randomSeed", [](unsigned int seed){  trussc::randomSeed(seed); });
    // tcLog.h, LINE 35
    lua->set_function("logLevelToString", [](LogLevel level){ return trussc::logLevelToString(level); });
    // tcLog.h, LINE 209
    lua->set_function("tcSetConsoleLogLevel", [](LogLevel level){  trussc::tcSetConsoleLogLevel(level); });
    // tcLog.h, LINE 213
    lua->set_function("tcSetFileLogLevel", [](LogLevel level){  trussc::tcSetFileLogLevel(level); });
    // tcLog.h, LINE 217
    lua->set_function("tcSetLogFile", [](const std::string & path){ return trussc::tcSetLogFile(path); });
    // tcLog.h, LINE 221
    lua->set_function("tcCloseLogFile", [](){  trussc::tcCloseLogFile(); });
    // tcLog.h, LINE 282
    lua->set_function("tcLog", [](LogLevel level){ return trussc::tcLog(level); });
    // tcLog.h, LINE 286
    lua->set_function("logVerbose", [](const std::string & module){ return trussc::logVerbose(module); });
    // tcLog.h, LINE 290
    lua->set_function("logNotice", [](const std::string & module){ return trussc::logNotice(module); });
    // tcLog.h, LINE 294
    lua->set_function("logWarning", [](const std::string & module){ return trussc::logWarning(module); });
    // tcLog.h, LINE 298
    lua->set_function("logError", [](const std::string & module){ return trussc::logError(module); });
    // tcLog.h, LINE 302
    lua->set_function("logFatal", [](const std::string & module){ return trussc::logFatal(module); });
    // tcLog.h, LINE 309
    lua->set_function("tcLogVerbose", [](const std::string & module){ return trussc::tcLogVerbose(module); });
    // tcLog.h, LINE 310
    lua->set_function("tcLogNotice", [](const std::string & module){ return trussc::tcLogNotice(module); });
    // tcLog.h, LINE 311
    lua->set_function("tcLogWarning", [](const std::string & module){ return trussc::tcLogWarning(module); });
    // tcLog.h, LINE 312
    lua->set_function("tcLogError", [](const std::string & module){ return trussc::tcLogError(module); });
    // tcLog.h, LINE 313
    lua->set_function("tcLogFatal", [](const std::string & module){ return trussc::tcLogFatal(module); });
    // TrussC.h, LINE 351
    lua->set_function("getDpiScale", [](){ return trussc::getDpiScale(); });
    // TrussC.h, LINE 356
    lua->set_function("getFramebufferWidth", [](){ return trussc::getFramebufferWidth(); });
    // TrussC.h, LINE 360
    lua->set_function("getFramebufferHeight", [](){ return trussc::getFramebufferHeight(); });
    // TrussC.h, LINE 371
    lua->set_function("beginFrame", [](){  trussc::beginFrame(); });
    // TrussC.h, LINE 387
    // TrussC.h, LINE 390
    // TrussC.h, LINE 395
    // TrussC.h, LINE 400
    lua->set_function("clear", sol::overload(
        [](float r, float g, float b, float a){  trussc::clear(r, g, b, a); },
        [](){  trussc::clear(); },
        [](float gray, float a){  trussc::clear(gray, a); },
        [](const Color & c){  trussc::clear(c); },
        // NOTE: additional overloads provided by user,
        [](float r){  trussc::clear(r); },
        // NOTE: additional overloads provided by user,
        [](float r, float g, float b){  trussc::clear(r, g, b); }
    ));
    // TrussC.h, LINE 405
    lua->set_function("flushDeferredShaderDraws", [](){  trussc::flushDeferredShaderDraws(); });
    // TrussC.h, LINE 412
    lua->set_function("ensureSwapchainPass", [](){  trussc::ensureSwapchainPass(); });
    // TrussC.h, LINE 415
    lua->set_function("present", [](){  trussc::present(); });
    // TrussC.h, LINE 418
    lua->set_function("isInSwapchainPass", [](){ return trussc::isInSwapchainPass(); });
    // TrussC.h, LINE 421
    lua->set_function("suspendSwapchainPass", [](){  trussc::suspendSwapchainPass(); });
    // TrussC.h, LINE 424
    lua->set_function("resumeSwapchainPass", [](){  trussc::resumeSwapchainPass(); });
    // TrussC.h, LINE 431
    // TrussC.h, LINE 436
    // TrussC.h, LINE 441
    lua->set_function("setColor", sol::overload(
        [](float r, float g, float b, float a){  trussc::setColor(r, g, b, a); },
        [](float gray, float a){  trussc::setColor(gray, a); },
        [](const Color & c){  trussc::setColor(c); },
        // NOTE: additional overloads provided by user,
        [](float r){  trussc::setColor(r); },
        // NOTE: additional overloads provided by user,
        [](float r, float g, float b){  trussc::setColor(r, g, b); }
    ));
    // TrussC.h, LINE 446
    lua->set_function("getColor", [](){ return trussc::getColor(); });
    // TrussC.h, LINE 451
    lua->set_function("setColorHSB", sol::overload(
        [](float h, float s, float b, float a){  trussc::setColorHSB(h, s, b, a); },
        // NOTE: additional overloads provided by user,
        [](float h, float s, float b){  trussc::setColorHSB(h, s, b); }
    ));
    // TrussC.h, LINE 456
    lua->set_function("setColorOKLab", [](float L, float a_lab, float b_lab, float alpha){  trussc::setColorOKLab(L, a_lab, b_lab, alpha); });
    // TrussC.h, LINE 461
    lua->set_function("setColorOKLCH", [](float L, float C, float H, float alpha){  trussc::setColorOKLCH(L, C, H, alpha); });
    // TrussC.h, LINE 466
    lua->set_function("fill", [](){  trussc::fill(); });
    // TrussC.h, LINE 471
    lua->set_function("noFill", [](){  trussc::noFill(); });
    // TrussC.h, LINE 476
    lua->set_function("setStrokeWeight", [](float weight){  trussc::setStrokeWeight(weight); });
    // TrussC.h, LINE 480
    lua->set_function("getStrokeWeight", [](){ return trussc::getStrokeWeight(); });
    // TrussC.h, LINE 484
    lua->set_function("setStrokeCap", [](StrokeCap cap){  trussc::setStrokeCap(cap); });
    // TrussC.h, LINE 488
    lua->set_function("getStrokeCap", [](){ return trussc::getStrokeCap(); });
    // TrussC.h, LINE 492
    lua->set_function("setStrokeJoin", [](StrokeJoin join){  trussc::setStrokeJoin(join); });
    // TrussC.h, LINE 496
    lua->set_function("getStrokeJoin", [](){ return trussc::getStrokeJoin(); });
    // TrussC.h, LINE 519
    // TrussC.h, LINE 525
    lua->set_function("setScissor", sol::overload(
        [](float x, float y, float w, float h){  trussc::setScissor(x, y, w, h); },
        [](int x, int y, int w, int h){  trussc::setScissor(x, y, w, h); }
    ));
    // TrussC.h, LINE 530
    lua->set_function("resetScissor", [](){  trussc::resetScissor(); });
    // TrussC.h, LINE 536
    lua->set_function("pushScissor", [](float x, float y, float w, float h){  trussc::pushScissor(x, y, w, h); });
    // TrussC.h, LINE 554
    lua->set_function("popScissor", [](){  trussc::popScissor(); });
    // TrussC.h, LINE 576
    lua->set_function("pushMatrix", [](){  trussc::pushMatrix(); });
    // TrussC.h, LINE 581
    lua->set_function("popMatrix", [](){  trussc::popMatrix(); });
    // TrussC.h, LINE 586
    lua->set_function("pushStyle", [](){  trussc::pushStyle(); });
    // TrussC.h, LINE 591
    lua->set_function("popStyle", [](){  trussc::popStyle(); });
    // TrussC.h, LINE 596
    lua->set_function("resetStyle", [](){  trussc::resetStyle(); });
    // TrussC.h, LINE 601
    // TrussC.h, LINE 605
    // TrussC.h, LINE 609
    lua->set_function("translate", sol::overload(
        [](Vec3 pos){  trussc::translate(pos); },
        [](float x, float y, float z){  trussc::translate(x, y, z); },
        [](float x, float y){  trussc::translate(x, y); }
    ));
    // TrussC.h, LINE 614
    // TrussC.h, LINE 651
    // TrussC.h, LINE 657
    // TrussC.h, LINE 661
    lua->set_function("rotate", sol::overload(
        [](float radians){  trussc::rotate(radians); },
        [](float x, float y, float z){  trussc::rotate(x, y, z); },
        [](const Vec3 & euler){  trussc::rotate(euler); },
        [](const Quaternion & quat){  trussc::rotate(quat); }
    ));
    // TrussC.h, LINE 619
    lua->set_function("rotateX", [](float radians){  trussc::rotateX(radians); });
    // TrussC.h, LINE 624
    lua->set_function("rotateY", [](float radians){  trussc::rotateY(radians); });
    // TrussC.h, LINE 629
    lua->set_function("rotateZ", [](float radians){  trussc::rotateZ(radians); });
    // TrussC.h, LINE 634
    // TrussC.h, LINE 666
    // TrussC.h, LINE 672
    lua->set_function("rotateDeg", sol::overload(
        [](float degrees){  trussc::rotateDeg(degrees); },
        [](float x, float y, float z){  trussc::rotateDeg(x, y, z); },
        [](const Vec3 & euler){  trussc::rotateDeg(euler); }
    ));
    // TrussC.h, LINE 638
    lua->set_function("rotateXDeg", [](float degrees){  trussc::rotateXDeg(degrees); });
    // TrussC.h, LINE 642
    lua->set_function("rotateYDeg", [](float degrees){  trussc::rotateYDeg(degrees); });
    // TrussC.h, LINE 646
    lua->set_function("rotateZDeg", [](float degrees){  trussc::rotateZDeg(degrees); });
    // TrussC.h, LINE 677
    // TrussC.h, LINE 682
    // TrussC.h, LINE 687
    lua->set_function("scale", sol::overload(
        [](float s){  trussc::scale(s); },
        [](float sx, float sy){  trussc::scale(sx, sy); },
        [](float sx, float sy, float sz){  trussc::scale(sx, sy, sz); }
    ));
    // TrussC.h, LINE 692
    lua->set_function("getCurrentMatrix", [](){ return trussc::getCurrentMatrix(); });
    // TrussC.h, LINE 697
    lua->set_function("resetMatrix", [](){  trussc::resetMatrix(); });
    // TrussC.h, LINE 702
    lua->set_function("setMatrix", [](const Mat4 & mat){  trussc::setMatrix(mat); });
    // TrussC.h, LINE 712
    lua->set_function("setBlendMode", [](BlendMode mode){  trussc::setBlendMode(mode); });
    // TrussC.h, LINE 721
    lua->set_function("getBlendMode", [](){ return trussc::getBlendMode(); });
    // TrussC.h, LINE 726
    lua->set_function("resetBlendMode", [](){  trussc::resetBlendMode(); });
    // TrussC.h, LINE 731
    lua->set_function("restoreBlendPipeline", [](){  trussc::restoreBlendPipeline(); });
    // TrussC.h, LINE 744
    lua->set_function("enable3D", [](){  trussc::enable3D(); });
    // TrussC.h, LINE 753
    lua->set_function("enable3DPerspective", [](float fovY, float nearZ, float farZ){  trussc::enable3DPerspective(fovY, nearZ, farZ); });
    // TrussC.h, LINE 874
    lua->set_function("setupScreenFov", [](float fovDeg, float nearDist, float farDist){  trussc::setupScreenFov(fovDeg, nearDist, farDist); });
    // TrussC.h, LINE 881
    lua->set_function("setupScreenPerspective", [](float fovDeg, float nearDist, float farDist){  trussc::setupScreenPerspective(fovDeg, nearDist, farDist); });
    // TrussC.h, LINE 886
    lua->set_function("setupScreenOrtho", [](){  trussc::setupScreenOrtho(); });
    // TrussC.h, LINE 892
    lua->set_function("setDefaultScreenFov", [](float fovDeg){  trussc::setDefaultScreenFov(fovDeg); });
    // TrussC.h, LINE 896
    lua->set_function("getDefaultScreenFov", [](){ return trussc::getDefaultScreenFov(); });
    // TrussC.h, LINE 901
    lua->set_function("setNearClip", [](float nearDist){  trussc::setNearClip(nearDist); });
    // TrussC.h, LINE 905
    lua->set_function("setFarClip", [](float farDist){  trussc::setFarClip(farDist); });
    // TrussC.h, LINE 909
    lua->set_function("getNearClip", [](){ return trussc::getNearClip(); });
    // TrussC.h, LINE 913
    lua->set_function("getFarClip", [](){ return trussc::getFarClip(); });
    // TrussC.h, LINE 923
    lua->set_function("worldToScreen", [](const Vec3 & worldPos){ return trussc::worldToScreen(worldPos); });
    // TrussC.h, LINE 952
    lua->set_function("screenToWorld", [](const Vec2 & screenPos, float worldZ){ return trussc::screenToWorld(screenPos, worldZ); });
    // TrussC.h, LINE 998
    lua->set_function("disable3D", [](){  trussc::disable3D(); });
    // TrussC.h, LINE 1007
    // TrussC.h, LINE 1011
    // TrussC.h, LINE 1015
    lua->set_function("drawRect", sol::overload(
        [](Vec3 pos, Vec2 size){  trussc::drawRect(pos, size); },
        [](Vec3 pos, float w, float h){  trussc::drawRect(pos, w, h); },
        [](float x, float y, float w, float h){  trussc::drawRect(x, y, w, h); }
    ));
    // TrussC.h, LINE 1020
    // TrussC.h, LINE 1024
    lua->set_function("drawRectRounded", sol::overload(
        [](Vec3 pos, Vec2 size, float radius){  trussc::drawRectRounded(pos, size, radius); },
        [](float x, float y, float w, float h, float radius){  trussc::drawRectRounded(x, y, w, h, radius); }
    ));
    // TrussC.h, LINE 1029
    // TrussC.h, LINE 1033
    lua->set_function("drawRectSquircle", sol::overload(
        [](Vec3 pos, Vec2 size, float radius){  trussc::drawRectSquircle(pos, size, radius); },
        [](float x, float y, float w, float h, float radius){  trussc::drawRectSquircle(x, y, w, h, radius); }
    ));
    // TrussC.h, LINE 1038
    // TrussC.h, LINE 1042
    lua->set_function("drawCircle", sol::overload(
        [](Vec3 center, float radius){  trussc::drawCircle(center, radius); },
        [](float cx, float cy, float radius){  trussc::drawCircle(cx, cy, radius); }
    ));
    // TrussC.h, LINE 1047
    // TrussC.h, LINE 1051
    // TrussC.h, LINE 1055
    lua->set_function("drawEllipse", sol::overload(
        [](Vec3 center, Vec2 radii){  trussc::drawEllipse(center, radii); },
        [](Vec3 center, float rx, float ry){  trussc::drawEllipse(center, rx, ry); },
        [](float cx, float cy, float rx, float ry){  trussc::drawEllipse(cx, cy, rx, ry); }
    ));
    // TrussC.h, LINE 1060
    // TrussC.h, LINE 1063
    lua->set_function("drawArc", sol::overload(
        [](Vec3 center, float radius, float angleBegin, float angleEnd){  trussc::drawArc(center, radius, angleBegin, angleEnd); },
        [](float x, float y, float radius, float angleBegin, float angleEnd){  trussc::drawArc(x, y, radius, angleBegin, angleEnd); }
    ));
    // TrussC.h, LINE 1068
    // TrussC.h, LINE 1071
    // TrussC.h, LINE 1074
    lua->set_function("drawBezier", sol::overload(
        [](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3){  trussc::drawBezier(p0, p1, p2, p3); },
        [](Vec3 p0, Vec3 p1, Vec3 p2){  trussc::drawBezier(p0, p1, p2); },
        [](const std::vector <Vec3>& controlPoints){  trussc::drawBezier(controlPoints); }
    ));
    // TrussC.h, LINE 1080
    // TrussC.h, LINE 1084
    // TrussC.h, LINE 1088
    lua->set_function("drawCurve", sol::overload(
        [](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3){  trussc::drawCurve(p0, p1, p2, p3); },
        [](const std::vector <Vec3>& points){  trussc::drawCurve(points); },
        [](const std::vector <Vec3>& points, bool closed){  trussc::drawCurve(points, closed); }
    ));
    // TrussC.h, LINE 1093
    // TrussC.h, LINE 1097
    // TrussC.h, LINE 1101
    lua->set_function("drawLine", sol::overload(
        [](Vec3 p1, Vec3 p2){  trussc::drawLine(p1, p2); },
        [](float x1, float y1, float x2, float y2){  trussc::drawLine(x1, y1, x2, y2); },
        [](float x1, float y1, float z1, float x2, float y2, float z2){  trussc::drawLine(x1, y1, z1, x2, y2, z2); }
    ));
    // TrussC.h, LINE 1106
    // TrussC.h, LINE 1110
    lua->set_function("drawTriangle", sol::overload(
        [](Vec3 p1, Vec3 p2, Vec3 p3){  trussc::drawTriangle(p1, p2, p3); },
        [](float x1, float y1, float x2, float y2, float x3, float y3){  trussc::drawTriangle(x1, y1, x2, y2, x3, y3); }
    ));
    // TrussC.h, LINE 1115
    // TrussC.h, LINE 1119
    lua->set_function("drawPoint", sol::overload(
        [](Vec3 pos){  trussc::drawPoint(pos); },
        [](float x, float y){  trussc::drawPoint(x, y); }
    ));
    // TrussC.h, LINE 1127
    lua->set_function("setCurveTolerance", [](float pixels){  trussc::setCurveTolerance(pixels); });
    // TrussC.h, LINE 1130
    lua->set_function("setCurveResolution", [](int n){  trussc::setCurveResolution(n); });
    // TrussC.h, LINE 1133
    lua->set_function("getCurveTolerance", [](){ return trussc::getCurveTolerance(); });
    // TrussC.h, LINE 1136
    lua->set_function("getCurveResolution", [](){ return trussc::getCurveResolution(); });
    // TrussC.h, LINE 1139
    lua->set_function("getCurveMode", [](){ return trussc::getCurveMode(); });
    // TrussC.h, LINE 1147
    lua->set_function("setCircleResolution", [](int res){  trussc::setCircleResolution(res); });
    // TrussC.h, LINE 1152
    lua->set_function("getCircleResolution", [](){ return trussc::getCircleResolution(); });
    // TrussC.h, LINE 1157
    lua->set_function("isFillEnabled", [](){ return trussc::isFillEnabled(); });
    // TrussC.h, LINE 1161
    lua->set_function("isStrokeEnabled", [](){ return trussc::isStrokeEnabled(); });
    // TrussC.h, LINE 1208
    // TrussC.h, LINE 1672
    lua->set_function("getFrameCount", sol::overload(
        [](){ return trussc::getFrameCount(); },
        [](){ return trussc::getFrameCount(); }
    ));
    // TrussC.h, LINE 1209
    lua->set_function("ensureFontAtlas", [](int rows){  trussc::ensureFontAtlas(rows); });
    // TrussC.h, LINE 1280
    lua->set_function("ensureFontAtlasForText", [](const std::string & text){  trussc::ensureFontAtlasForText(text); });
    // TrussC.h, LINE 1288
    // TrussC.h, LINE 1292
    // TrussC.h, LINE 1297
    // TrussC.h, LINE 1301
    // TrussC.h, LINE 1306
    // TrussC.h, LINE 1311
    lua->set_function("drawBitmapString", sol::overload(
        [](const std::string & text, Vec3 pos, bool screenFixed){  trussc::drawBitmapString(text, pos, screenFixed); },
        [](const std::string & text, float x, float y, bool screenFixed){  trussc::drawBitmapString(text, x, y, screenFixed); },
        [](const std::string & text, Vec3 pos, float scale){  trussc::drawBitmapString(text, pos, scale); },
        [](const std::string & text, float x, float y, float scale){  trussc::drawBitmapString(text, x, y, scale); },
        [](const std::string & text, Vec3 pos, Direction h, Direction v){  trussc::drawBitmapString(text, pos, h, v); },
        [](const std::string & text, float x, float y, Direction h, Direction v){  trussc::drawBitmapString(text, x, y, h, v); },
        // NOTE: additional overloads provided by user,
        [](const std::string& s, float x, float y){  trussc::drawBitmapString(s, x, y); }
    ));
    // TrussC.h, LINE 1317
    lua->set_function("setTextAlign", [](Direction h, Direction v){  trussc::setTextAlign(h, v); });
    // TrussC.h, LINE 1322
    lua->set_function("getTextAlignH", [](){ return trussc::getTextAlignH(); });
    // TrussC.h, LINE 1326
    lua->set_function("getTextAlignV", [](){ return trussc::getTextAlignV(); });
    // TrussC.h, LINE 1331
    lua->set_function("setBitmapLineHeight", [](float h){  trussc::setBitmapLineHeight(h); });
    // TrussC.h, LINE 1336
    lua->set_function("getBitmapLineHeight", [](){ return trussc::getBitmapLineHeight(); });
    // TrussC.h, LINE 1341
    lua->set_function("getBitmapFontHeight", [](){ return trussc::getBitmapFontHeight(); });
    // TrussC.h, LINE 1346
    lua->set_function("getBitmapStringWidth", [](const std::string & text){ return trussc::getBitmapStringWidth(text); });
    // TrussC.h, LINE 1351
    lua->set_function("getBitmapStringHeight", [](const std::string & text){ return trussc::getBitmapStringHeight(text); });
    // TrussC.h, LINE 1356
    lua->set_function("getBitmapStringBBox", [](const std::string & text){ return trussc::getBitmapStringBBox(text); });
    // TrussC.h, LINE 1361
    lua->set_function("drawBitmapStringHighlight", sol::overload(
        [](const std::string & text, float x, float y, const Color & background, const Color & foreground){  trussc::drawBitmapStringHighlight(text, x, y, background, foreground); },
        // NOTE: additional overloads provided by user,
        [](const std::string& s, float x, float y){  trussc::drawBitmapStringHighlight(s, x, y); }
    ));
    // TrussC.h, LINE 1475
    lua->set_function("setWindowTitle", [](const std::string & title){  trussc::setWindowTitle(title); });
    // TrussC.h, LINE 1515
    lua->set_function("showCursor", [](){  trussc::showCursor(); });
    // TrussC.h, LINE 1520
    lua->set_function("hideCursor", [](){  trussc::hideCursor(); });
    // TrussC.h, LINE 1525
    lua->set_function("setCursor", [](Cursor cursor){  trussc::setCursor(cursor); });
    // TrussC.h, LINE 1530
    lua->set_function("getCursor", [](){ return trussc::getCursor(); });
    // TrussC.h, LINE 1536
    // TrussC.h, LINE 2660
    lua->set_function("bindCursorImage", sol::overload(
        [](Cursor cursor, int width, int height, const unsigned char * pixels, int hotspotX, int hotspotY){  trussc::bindCursorImage(cursor, width, height, pixels, hotspotX, hotspotY); },
        [](Cursor cursor, const Image & image, int hotspotX, int hotspotY){  trussc::bindCursorImage(cursor, image, hotspotX, hotspotY); }
    ));
    // TrussC.h, LINE 1552
    lua->set_function("unbindCursorImage", [](Cursor cursor){  trussc::unbindCursorImage(cursor); });
    // TrussC.h, LINE 1561
    lua->set_function("setClipboardString", [](const std::string & text){  trussc::setClipboardString(text); });
    // TrussC.h, LINE 1570
    lua->set_function("getClipboardString", [](){ return trussc::getClipboardString(); });
    // TrussC.h, LINE 1578
    lua->set_function("setWindowSize", [](int width, int height){  trussc::setWindowSize(width, height); });
    // TrussC.h, LINE 1590
    lua->set_function("setFullscreen", [](bool full){  trussc::setFullscreen(full); });
    // TrussC.h, LINE 1597
    lua->set_function("isFullscreen", [](){ return trussc::isFullscreen(); });
    // TrussC.h, LINE 1602
    lua->set_function("toggleFullscreen", [](){  trussc::toggleFullscreen(); });
    // TrussC.h, LINE 1632
    lua->set_function("setOrientation", [](Orientation mask){  trussc::setOrientation(mask); });
    // TrussC.h, LINE 1639
    lua->set_function("getWindowWidth", [](){ return trussc::getWindowWidth(); });
    // TrussC.h, LINE 1647
    lua->set_function("getWindowHeight", [](){ return trussc::getWindowHeight(); });
    // TrussC.h, LINE 1655
    lua->set_function("getWindowSize", [](){ return trussc::getWindowSize(); });
    // TrussC.h, LINE 1660
    lua->set_function("getAspectRatio", [](){ return trussc::getAspectRatio(); });
    // TrussC.h, LINE 1669
    lua->set_function("getElapsedTime", [](){ return trussc::getElapsedTime(); });
    // TrussC.h, LINE 1670
    lua->set_function("getUpdateCount", [](){ return trussc::getUpdateCount(); });
    // TrussC.h, LINE 1671
    lua->set_function("getDrawCount", [](){ return trussc::getDrawCount(); });
    // TrussC.h, LINE 1673
    lua->set_function("getDeltaTime", [](){ return trussc::getDeltaTime(); });
    // TrussC.h, LINE 1674
    lua->set_function("getFrameRate", [](){ return trussc::getFrameRate(); });
    // TrussC.h, LINE 1681
    lua->set_function("getSokolMemoryBytes", [](){ return trussc::getSokolMemoryBytes(); });
    // TrussC.h, LINE 1684
    lua->set_function("getSokolMemoryAllocs", [](){ return trussc::getSokolMemoryAllocs(); });
    // TrussC.h, LINE 1689
    lua->set_function("releaseSglBuffers", [](){  trussc::releaseSglBuffers(); });
    // TrussC.h, LINE 1698
    lua->set_function("getGlobalMouseX", [](){ return trussc::getGlobalMouseX(); });
    // TrussC.h, LINE 1703
    lua->set_function("getGlobalMouseY", [](){ return trussc::getGlobalMouseY(); });
    // TrussC.h, LINE 1708
    lua->set_function("getGlobalPMouseX", [](){ return trussc::getGlobalPMouseX(); });
    // TrussC.h, LINE 1713
    lua->set_function("getGlobalPMouseY", [](){ return trussc::getGlobalPMouseY(); });
    // TrussC.h, LINE 1718
    lua->set_function("isMousePressed", [](){ return trussc::isMousePressed(); });
    // TrussC.h, LINE 1723
    lua->set_function("getMouseButton", [](){ return trussc::getMouseButton(); });
    // TrussC.h, LINE 1728
    lua->set_function("isKeyPressed", [](int key){ return trussc::isKeyPressed(key); });
    // TrussC.h, LINE 1737
    lua->set_function("isShiftPressed", [](){ return trussc::isShiftPressed(); });
    // TrussC.h, LINE 1738
    lua->set_function("isControlPressed", [](){ return trussc::isControlPressed(); });
    // TrussC.h, LINE 1739
    lua->set_function("isAltPressed", [](){ return trussc::isAltPressed(); });
    // TrussC.h, LINE 1740
    lua->set_function("isSuperPressed", [](){ return trussc::isSuperPressed(); });
    // TrussC.h, LINE 1743
    lua->set_function("getMouseX", [](){ return trussc::getMouseX(); });
    // TrussC.h, LINE 1744
    lua->set_function("getMouseY", [](){ return trussc::getMouseY(); });
    // TrussC.h, LINE 1745
    lua->set_function("getMousePos", [](){ return trussc::getMousePos(); });
    // TrussC.h, LINE 1746
    lua->set_function("getGlobalMousePos", [](){ return trussc::getGlobalMousePos(); });
    // TrussC.h, LINE 1754
    lua->set_function("setTouchAsMouse", [](bool enabled){  trussc::setTouchAsMouse(enabled); });
    // TrussC.h, LINE 1755
    lua->set_function("getTouchAsMouse", [](){ return trussc::getTouchAsMouse(); });
    // TrussC.h, LINE 1762
    lua->set_function("getBackendName", [](){ return trussc::getBackendName(); });
    // TrussC.h, LINE 1776
    lua->set_function("getMemoryUsage", [](){ return trussc::getMemoryUsage(); });
    // TrussC.h, LINE 1816
    lua->set_function("getNodeCount", [](){ return trussc::getNodeCount(); });
    // TrussC.h, LINE 1817
    lua->set_function("getTextureCount", [](){ return trussc::getTextureCount(); });
    // TrussC.h, LINE 1818
    lua->set_function("getFboCount", [](){ return trussc::getFboCount(); });
    // TrussC.h, LINE 1842
    lua->set_function("setFps", [](float fps){  trussc::setFps(fps); });
    // TrussC.h, LINE 1852
    lua->set_function("setIndependentFps", [](float updateFps, float drawFps){  trussc::setIndependentFps(updateFps, drawFps); });
    // TrussC.h, LINE 1861
    lua->set_function("getFpsSettings", [](){ return trussc::getFpsSettings(); });
    // TrussC.h, LINE 1881
    lua->set_function("getFps", [](){ return trussc::getFps(); });
    // TrussC.h, LINE 1887
    lua->set_function("redraw", [](int count){  trussc::redraw(count); });
    // TrussC.h, LINE 1895
    lua->set_function("requestExitApp", [](){  trussc::requestExitApp(); });
    // TrussC.h, LINE 1901
    lua->set_function("exitApp", [](){  trussc::exitApp(); });
    // TrussC.h, LINE 1911
    lua->set_function("grabScreen", [](Pixels & outPixels){ return trussc::grabScreen(outPixels); });
    // TrussC.h, LINE 2054
    lua->set_function("registerInspectionTools", [](){  trussc::mcp::registerInspectionTools(); });
    // TrussC.h, LINE 2055
    lua->set_function("registerDebuggerTools", [](){  trussc::mcp::registerDebuggerTools(); });
    // tcPrimitives.h, LINE 11
    lua->set_function("createPlane", [](float width, float height, int cols, int rows){ return trussc::createPlane(width, height, cols, rows); });
    // tcPrimitives.h, LINE 47
    // tcPrimitives.h, LINE 102
    lua->set_function("createBox", sol::overload(
        [](float width, float height, float depth){ return trussc::createBox(width, height, depth); },
        [](float size){ return trussc::createBox(size); }
    ));
    // tcPrimitives.h, LINE 109
    lua->set_function("createSphere", [](float radius, int resolution){ return trussc::createSphere(radius, resolution); });
    // tcPrimitives.h, LINE 168
    lua->set_function("createCylinder", [](float radius, float height, int resolution){ return trussc::createCylinder(radius, height, resolution); });
    // tcPrimitives.h, LINE 255
    lua->set_function("createCone", [](float radius, float height, int resolution){ return trussc::createCone(radius, height, resolution); });
    // tcPrimitives.h, LINE 327
    lua->set_function("createIcoSphere", [](float radius, int subdivisions){ return trussc::createIcoSphere(radius, subdivisions); });
    // tcPrimitives.h, LINE 430
    lua->set_function("createTorus", [](float radius, float tubeRadius, int sides, int rings){ return trussc::createTorus(radius, tubeRadius, sides, rings); });
    // TrussC.h, LINE 2704
    // TrussC.h, LINE 2713
    // TrussC.h, LINE 2717
    // TrussC.h, LINE 2724
    // TrussC.h, LINE 2728
    // TrussC.h, LINE 2732
    lua->set_function("drawBox", sol::overload(
        [](float w, float h, float d){  trussc::drawBox(w, h, d); },
        [](float size){  trussc::drawBox(size); },
        [](Vec3 pos, float w, float h, float d){  trussc::drawBox(pos, w, h, d); },
        [](float x, float y, float z, float w, float h, float d){  trussc::drawBox(x, y, z, w, h, d); },
        [](Vec3 pos, float size){  trussc::drawBox(pos, size); },
        [](float x, float y, float z, float size){  trussc::drawBox(x, y, z, size); }
    ));
    // TrussC.h, LINE 2736
    // TrussC.h, LINE 2745
    // TrussC.h, LINE 2752
    lua->set_function("drawSphere", sol::overload(
        [](float radius, int resolution){  trussc::drawSphere(radius, resolution); },
        [](Vec3 pos, float radius, int resolution){  trussc::drawSphere(pos, radius, resolution); },
        [](float x, float y, float z, float radius, int resolution){  trussc::drawSphere(x, y, z, radius, resolution); }
    ));
    // TrussC.h, LINE 2756
    // TrussC.h, LINE 2765
    // TrussC.h, LINE 2772
    lua->set_function("drawCone", sol::overload(
        [](float radius, float height, int resolution){  trussc::drawCone(radius, height, resolution); },
        [](Vec3 pos, float radius, float height, int resolution){  trussc::drawCone(pos, radius, height, resolution); },
        [](float x, float y, float z, float radius, float height, int resolution){  trussc::drawCone(x, y, z, radius, height, resolution); }
    ));
    // tc3DGraphics.h, LINE 25
    lua->set_function("addLight", [](Light & light){  trussc::addLight(light); });
    // tc3DGraphics.h, LINE 37
    lua->set_function("removeLight", [](Light & light){  trussc::removeLight(light); });
    // tc3DGraphics.h, LINE 46
    lua->set_function("clearLights", [](){  trussc::clearLights(); });
    // tc3DGraphics.h, LINE 51
    lua->set_function("getNumLights", [](){ return trussc::getNumLights(); });
    // tc3DGraphics.h, LINE 60
    lua->set_function("setMaterial", [](Material & material){  trussc::setMaterial(material); });
    // tc3DGraphics.h, LINE 65
    lua->set_function("clearMaterial", [](){  trussc::clearMaterial(); });
    // tc3DGraphics.h, LINE 76
    lua->set_function("beginShadowPass", [](Light & light){  trussc::beginShadowPass(light); });
    // tc3DGraphics.h, LINE 85
    lua->set_function("endShadowPass", [](){  trussc::endShadowPass(); });
    // tc3DGraphics.h, LINE 90
    lua->set_function("shadowDraw", [](const Mesh & mesh){  trussc::shadowDraw(mesh); });
    // tc3DGraphics.h, LINE 98
    // tc3DGraphics.h, LINE 102
    lua->set_function("setCameraPosition", sol::overload(
        [](const Vec3 & pos){  trussc::setCameraPosition(pos); },
        [](float x, float y, float z){  trussc::setCameraPosition(x, y, z); }
    ));
    // tc3DGraphics.h, LINE 106
    lua->set_function("getCameraPosition", [](){ return trussc::getCameraPosition(); });


}

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif // #ifndef _MSC_VER
