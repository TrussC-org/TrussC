
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
    // tcMath.h, LINE 1073
    lua->set_function("getRandomEngine", [](){ return trussc::internal::getRandomEngine(); });
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
    // tcLog.h, LINE 211
    lua->set_function("tcSetConsoleLogLevel", [](LogLevel level){  trussc::tcSetConsoleLogLevel(level); });
    // tcLog.h, LINE 215
    lua->set_function("tcSetFileLogLevel", [](LogLevel level){  trussc::tcSetFileLogLevel(level); });
    // tcLog.h, LINE 219
    lua->set_function("tcSetLogFile", [](const std::string & path){ return trussc::tcSetLogFile(path); });
    // tcLog.h, LINE 223
    lua->set_function("tcCloseLogFile", [](){  trussc::tcCloseLogFile(); });
    // tcLog.h, LINE 284
    lua->set_function("tcLog", [](LogLevel level){ return trussc::tcLog(level); });
    // tcLog.h, LINE 288
    lua->set_function("logVerbose", [](const std::string & module){ return trussc::logVerbose(module); });
    // tcLog.h, LINE 292
    lua->set_function("logNotice", [](const std::string & module){ return trussc::logNotice(module); });
    // tcLog.h, LINE 296
    lua->set_function("logWarning", [](const std::string & module){ return trussc::logWarning(module); });
    // tcLog.h, LINE 300
    lua->set_function("logError", [](const std::string & module){ return trussc::logError(module); });
    // tcLog.h, LINE 304
    lua->set_function("logFatal", [](const std::string & module){ return trussc::logFatal(module); });
    // tcLog.h, LINE 311
    lua->set_function("tcLogVerbose", [](const std::string & module){ return trussc::tcLogVerbose(module); });
    // tcLog.h, LINE 312
    lua->set_function("tcLogNotice", [](const std::string & module){ return trussc::tcLogNotice(module); });
    // tcLog.h, LINE 313
    lua->set_function("tcLogWarning", [](const std::string & module){ return trussc::tcLogWarning(module); });
    // tcLog.h, LINE 314
    lua->set_function("tcLogError", [](const std::string & module){ return trussc::tcLogError(module); });
    // tcLog.h, LINE 315
    lua->set_function("tcLogFatal", [](const std::string & module){ return trussc::tcLogFatal(module); });
    // TrussC.h, LINE 343
    lua->set_function("getDpiScale", [](){ return trussc::getDpiScale(); });
    // TrussC.h, LINE 348
    lua->set_function("getFramebufferWidth", [](){ return trussc::getFramebufferWidth(); });
    // TrussC.h, LINE 352
    lua->set_function("getFramebufferHeight", [](){ return trussc::getFramebufferHeight(); });
    // TrussC.h, LINE 363
    lua->set_function("beginFrame", [](){  trussc::beginFrame(); });
    // TrussC.h, LINE 379
    // TrussC.h, LINE 382
    // TrussC.h, LINE 387
    // TrussC.h, LINE 392
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
    // TrussC.h, LINE 397
    lua->set_function("flushDeferredShaderDraws", [](){  trussc::flushDeferredShaderDraws(); });
    // TrussC.h, LINE 404
    lua->set_function("ensureSwapchainPass", [](){  trussc::ensureSwapchainPass(); });
    // TrussC.h, LINE 407
    lua->set_function("present", [](){  trussc::present(); });
    // TrussC.h, LINE 410
    lua->set_function("isInSwapchainPass", [](){ return trussc::isInSwapchainPass(); });
    // TrussC.h, LINE 413
    lua->set_function("suspendSwapchainPass", [](){  trussc::suspendSwapchainPass(); });
    // TrussC.h, LINE 416
    lua->set_function("resumeSwapchainPass", [](){  trussc::resumeSwapchainPass(); });
    // TrussC.h, LINE 423
    // TrussC.h, LINE 428
    // TrussC.h, LINE 433
    lua->set_function("setColor", sol::overload(
        [](float r, float g, float b, float a){  trussc::setColor(r, g, b, a); },
        [](float gray, float a){  trussc::setColor(gray, a); },
        [](const Color & c){  trussc::setColor(c); },
        // NOTE: additional overloads provided by user,
        [](float r){  trussc::setColor(r); },
        // NOTE: additional overloads provided by user,
        [](float r, float g, float b){  trussc::setColor(r, g, b); }
    ));
    // TrussC.h, LINE 438
    lua->set_function("getColor", [](){ return trussc::getColor(); });
    // TrussC.h, LINE 443
    lua->set_function("setColorHSB", sol::overload(
        [](float h, float s, float b, float a){  trussc::setColorHSB(h, s, b, a); },
        // NOTE: additional overloads provided by user,
        [](float h, float s, float b){  trussc::setColorHSB(h, s, b); }
    ));
    // TrussC.h, LINE 448
    lua->set_function("setColorOKLab", [](float L, float a_lab, float b_lab, float alpha){  trussc::setColorOKLab(L, a_lab, b_lab, alpha); });
    // TrussC.h, LINE 453
    lua->set_function("setColorOKLCH", [](float L, float C, float H, float alpha){  trussc::setColorOKLCH(L, C, H, alpha); });
    // TrussC.h, LINE 458
    lua->set_function("fill", [](){  trussc::fill(); });
    // TrussC.h, LINE 463
    lua->set_function("noFill", [](){  trussc::noFill(); });
    // TrussC.h, LINE 468
    lua->set_function("setStrokeWeight", [](float weight){  trussc::setStrokeWeight(weight); });
    // TrussC.h, LINE 472
    lua->set_function("getStrokeWeight", [](){ return trussc::getStrokeWeight(); });
    // TrussC.h, LINE 476
    lua->set_function("setStrokeCap", [](StrokeCap cap){  trussc::setStrokeCap(cap); });
    // TrussC.h, LINE 480
    lua->set_function("getStrokeCap", [](){ return trussc::getStrokeCap(); });
    // TrussC.h, LINE 484
    lua->set_function("setStrokeJoin", [](StrokeJoin join){  trussc::setStrokeJoin(join); });
    // TrussC.h, LINE 488
    lua->set_function("getStrokeJoin", [](){ return trussc::getStrokeJoin(); });
    // TrussC.h, LINE 511
    // TrussC.h, LINE 517
    lua->set_function("setScissor", sol::overload(
        [](float x, float y, float w, float h){  trussc::setScissor(x, y, w, h); },
        [](int x, int y, int w, int h){  trussc::setScissor(x, y, w, h); }
    ));
    // TrussC.h, LINE 522
    lua->set_function("resetScissor", [](){  trussc::resetScissor(); });
    // TrussC.h, LINE 528
    lua->set_function("pushScissor", [](float x, float y, float w, float h){  trussc::pushScissor(x, y, w, h); });
    // TrussC.h, LINE 546
    lua->set_function("popScissor", [](){  trussc::popScissor(); });
    // TrussC.h, LINE 568
    lua->set_function("pushMatrix", [](){  trussc::pushMatrix(); });
    // TrussC.h, LINE 573
    lua->set_function("popMatrix", [](){  trussc::popMatrix(); });
    // TrussC.h, LINE 578
    lua->set_function("pushStyle", [](){  trussc::pushStyle(); });
    // TrussC.h, LINE 583
    lua->set_function("popStyle", [](){  trussc::popStyle(); });
    // TrussC.h, LINE 588
    lua->set_function("resetStyle", [](){  trussc::resetStyle(); });
    // TrussC.h, LINE 593
    // TrussC.h, LINE 597
    // TrussC.h, LINE 601
    lua->set_function("translate", sol::overload(
        [](Vec3 pos){  trussc::translate(pos); },
        [](float x, float y, float z){  trussc::translate(x, y, z); },
        [](float x, float y){  trussc::translate(x, y); }
    ));
    // TrussC.h, LINE 606
    // TrussC.h, LINE 643
    // TrussC.h, LINE 649
    // TrussC.h, LINE 653
    lua->set_function("rotate", sol::overload(
        [](float radians){  trussc::rotate(radians); },
        [](float x, float y, float z){  trussc::rotate(x, y, z); },
        [](const Vec3 & euler){  trussc::rotate(euler); },
        [](const Quaternion & quat){  trussc::rotate(quat); }
    ));
    // TrussC.h, LINE 611
    lua->set_function("rotateX", [](float radians){  trussc::rotateX(radians); });
    // TrussC.h, LINE 616
    lua->set_function("rotateY", [](float radians){  trussc::rotateY(radians); });
    // TrussC.h, LINE 621
    lua->set_function("rotateZ", [](float radians){  trussc::rotateZ(radians); });
    // TrussC.h, LINE 626
    // TrussC.h, LINE 658
    // TrussC.h, LINE 664
    lua->set_function("rotateDeg", sol::overload(
        [](float degrees){  trussc::rotateDeg(degrees); },
        [](float x, float y, float z){  trussc::rotateDeg(x, y, z); },
        [](const Vec3 & euler){  trussc::rotateDeg(euler); }
    ));
    // TrussC.h, LINE 630
    lua->set_function("rotateXDeg", [](float degrees){  trussc::rotateXDeg(degrees); });
    // TrussC.h, LINE 634
    lua->set_function("rotateYDeg", [](float degrees){  trussc::rotateYDeg(degrees); });
    // TrussC.h, LINE 638
    lua->set_function("rotateZDeg", [](float degrees){  trussc::rotateZDeg(degrees); });
    // TrussC.h, LINE 669
    // TrussC.h, LINE 674
    // TrussC.h, LINE 679
    lua->set_function("scale", sol::overload(
        [](float s){  trussc::scale(s); },
        [](float sx, float sy){  trussc::scale(sx, sy); },
        [](float sx, float sy, float sz){  trussc::scale(sx, sy, sz); }
    ));
    // TrussC.h, LINE 684
    lua->set_function("getCurrentMatrix", [](){ return trussc::getCurrentMatrix(); });
    // TrussC.h, LINE 689
    lua->set_function("resetMatrix", [](){  trussc::resetMatrix(); });
    // TrussC.h, LINE 694
    lua->set_function("setMatrix", [](const Mat4 & mat){  trussc::setMatrix(mat); });
    // TrussC.h, LINE 704
    lua->set_function("setBlendMode", [](BlendMode mode){  trussc::setBlendMode(mode); });
    // TrussC.h, LINE 713
    lua->set_function("getBlendMode", [](){ return trussc::getBlendMode(); });
    // TrussC.h, LINE 718
    lua->set_function("resetBlendMode", [](){  trussc::resetBlendMode(); });
    // TrussC.h, LINE 723
    lua->set_function("restoreBlendPipeline", [](){  trussc::restoreBlendPipeline(); });
    // TrussC.h, LINE 736
    lua->set_function("enable3D", [](){  trussc::enable3D(); });
    // TrussC.h, LINE 745
    lua->set_function("enable3DPerspective", [](float fovY, float nearZ, float farZ){  trussc::enable3DPerspective(fovY, nearZ, farZ); });
    // TrussC.h, LINE 866
    lua->set_function("setupScreenFov", [](float fovDeg, float nearDist, float farDist){  trussc::setupScreenFov(fovDeg, nearDist, farDist); });
    // TrussC.h, LINE 873
    lua->set_function("setupScreenPerspective", [](float fovDeg, float nearDist, float farDist){  trussc::setupScreenPerspective(fovDeg, nearDist, farDist); });
    // TrussC.h, LINE 878
    lua->set_function("setupScreenOrtho", [](){  trussc::setupScreenOrtho(); });
    // TrussC.h, LINE 884
    lua->set_function("setDefaultScreenFov", [](float fovDeg){  trussc::setDefaultScreenFov(fovDeg); });
    // TrussC.h, LINE 888
    lua->set_function("getDefaultScreenFov", [](){ return trussc::getDefaultScreenFov(); });
    // TrussC.h, LINE 893
    lua->set_function("setNearClip", [](float nearDist){  trussc::setNearClip(nearDist); });
    // TrussC.h, LINE 897
    lua->set_function("setFarClip", [](float farDist){  trussc::setFarClip(farDist); });
    // TrussC.h, LINE 901
    lua->set_function("getNearClip", [](){ return trussc::getNearClip(); });
    // TrussC.h, LINE 905
    lua->set_function("getFarClip", [](){ return trussc::getFarClip(); });
    // TrussC.h, LINE 915
    lua->set_function("worldToScreen", [](const Vec3 & worldPos){ return trussc::worldToScreen(worldPos); });
    // TrussC.h, LINE 944
    lua->set_function("screenToWorld", [](const Vec2 & screenPos, float worldZ){ return trussc::screenToWorld(screenPos, worldZ); });
    // TrussC.h, LINE 990
    lua->set_function("disable3D", [](){  trussc::disable3D(); });
    // TrussC.h, LINE 999
    // TrussC.h, LINE 1003
    // TrussC.h, LINE 1007
    lua->set_function("drawRect", sol::overload(
        [](Vec3 pos, Vec2 size){  trussc::drawRect(pos, size); },
        [](Vec3 pos, float w, float h){  trussc::drawRect(pos, w, h); },
        [](float x, float y, float w, float h){  trussc::drawRect(x, y, w, h); }
    ));
    // TrussC.h, LINE 1012
    // TrussC.h, LINE 1016
    lua->set_function("drawRectRounded", sol::overload(
        [](Vec3 pos, Vec2 size, float radius){  trussc::drawRectRounded(pos, size, radius); },
        [](float x, float y, float w, float h, float radius){  trussc::drawRectRounded(x, y, w, h, radius); }
    ));
    // TrussC.h, LINE 1021
    // TrussC.h, LINE 1025
    lua->set_function("drawRectSquircle", sol::overload(
        [](Vec3 pos, Vec2 size, float radius){  trussc::drawRectSquircle(pos, size, radius); },
        [](float x, float y, float w, float h, float radius){  trussc::drawRectSquircle(x, y, w, h, radius); }
    ));
    // TrussC.h, LINE 1030
    // TrussC.h, LINE 1034
    lua->set_function("drawCircle", sol::overload(
        [](Vec3 center, float radius){  trussc::drawCircle(center, radius); },
        [](float cx, float cy, float radius){  trussc::drawCircle(cx, cy, radius); }
    ));
    // TrussC.h, LINE 1039
    // TrussC.h, LINE 1043
    // TrussC.h, LINE 1047
    lua->set_function("drawEllipse", sol::overload(
        [](Vec3 center, Vec2 radii){  trussc::drawEllipse(center, radii); },
        [](Vec3 center, float rx, float ry){  trussc::drawEllipse(center, rx, ry); },
        [](float cx, float cy, float rx, float ry){  trussc::drawEllipse(cx, cy, rx, ry); }
    ));
    // TrussC.h, LINE 1052
    // TrussC.h, LINE 1056
    // TrussC.h, LINE 1060
    lua->set_function("drawLine", sol::overload(
        [](Vec3 p1, Vec3 p2){  trussc::drawLine(p1, p2); },
        [](float x1, float y1, float x2, float y2){  trussc::drawLine(x1, y1, x2, y2); },
        [](float x1, float y1, float z1, float x2, float y2, float z2){  trussc::drawLine(x1, y1, z1, x2, y2, z2); }
    ));
    // TrussC.h, LINE 1065
    // TrussC.h, LINE 1069
    lua->set_function("drawTriangle", sol::overload(
        [](Vec3 p1, Vec3 p2, Vec3 p3){  trussc::drawTriangle(p1, p2, p3); },
        [](float x1, float y1, float x2, float y2, float x3, float y3){  trussc::drawTriangle(x1, y1, x2, y2, x3, y3); }
    ));
    // TrussC.h, LINE 1074
    // TrussC.h, LINE 1078
    lua->set_function("drawPoint", sol::overload(
        [](Vec3 pos){  trussc::drawPoint(pos); },
        [](float x, float y){  trussc::drawPoint(x, y); }
    ));
    // TrussC.h, LINE 1083
    lua->set_function("setCircleResolution", [](int res){  trussc::setCircleResolution(res); });
    // TrussC.h, LINE 1087
    lua->set_function("getCircleResolution", [](){ return trussc::getCircleResolution(); });
    // TrussC.h, LINE 1092
    lua->set_function("isFillEnabled", [](){ return trussc::isFillEnabled(); });
    // TrussC.h, LINE 1096
    lua->set_function("isStrokeEnabled", [](){ return trussc::isStrokeEnabled(); });
    // TrussC.h, LINE 1130
    // TrussC.h, LINE 1134
    // TrussC.h, LINE 1139
    // TrussC.h, LINE 1143
    // TrussC.h, LINE 1148
    // TrussC.h, LINE 1153
    lua->set_function("drawBitmapString", sol::overload(
        [](const std::string & text, Vec3 pos, bool screenFixed){  trussc::drawBitmapString(text, pos, screenFixed); },
        [](const std::string & text, float x, float y, bool screenFixed){  trussc::drawBitmapString(text, x, y, screenFixed); },
        [](const std::string & text, Vec3 pos, float scale){  trussc::drawBitmapString(text, pos, scale); },
        [](const std::string & text, float x, float y, float scale){  trussc::drawBitmapString(text, x, y, scale); },
        [](const std::string & text, Vec3 pos, Direction h, Direction v){  trussc::drawBitmapString(text, pos, h, v); },
        [](const std::string & text, float x, float y, Direction h, Direction v){  trussc::drawBitmapString(text, x, y, h, v); }
    ));
    // TrussC.h, LINE 1159
    lua->set_function("setTextAlign", [](Direction h, Direction v){  trussc::setTextAlign(h, v); });
    // TrussC.h, LINE 1164
    lua->set_function("getTextAlignH", [](){ return trussc::getTextAlignH(); });
    // TrussC.h, LINE 1168
    lua->set_function("getTextAlignV", [](){ return trussc::getTextAlignV(); });
    // TrussC.h, LINE 1173
    lua->set_function("setBitmapLineHeight", [](float h){  trussc::setBitmapLineHeight(h); });
    // TrussC.h, LINE 1178
    lua->set_function("getBitmapLineHeight", [](){ return trussc::getBitmapLineHeight(); });
    // TrussC.h, LINE 1183
    lua->set_function("getBitmapFontHeight", [](){ return trussc::getBitmapFontHeight(); });
    // TrussC.h, LINE 1188
    lua->set_function("getBitmapStringWidth", [](const std::string & text){ return trussc::getBitmapStringWidth(text); });
    // TrussC.h, LINE 1193
    lua->set_function("getBitmapStringHeight", [](const std::string & text){ return trussc::getBitmapStringHeight(text); });
    // TrussC.h, LINE 1198
    lua->set_function("getBitmapStringBBox", [](const std::string & text){ return trussc::getBitmapStringBBox(text); });
    // TrussC.h, LINE 1203
    lua->set_function("drawBitmapStringHighlight", [](const std::string & text, float x, float y, const Color & background, const Color & foreground){  trussc::drawBitmapStringHighlight(text, x, y, background, foreground); });
    // TrussC.h, LINE 1309
    lua->set_function("setWindowTitle", [](const std::string & title){  trussc::setWindowTitle(title); });
    // TrussC.h, LINE 1349
    lua->set_function("showCursor", [](){  trussc::showCursor(); });
    // TrussC.h, LINE 1354
    lua->set_function("hideCursor", [](){  trussc::hideCursor(); });
    // TrussC.h, LINE 1359
    lua->set_function("setCursor", [](Cursor cursor){  trussc::setCursor(cursor); });
    // TrussC.h, LINE 1364
    lua->set_function("getCursor", [](){ return trussc::getCursor(); });
    // TrussC.h, LINE 1370
    // TrussC.h, LINE 2524
    lua->set_function("bindCursorImage", sol::overload(
        [](Cursor cursor, int width, int height, const unsigned char * pixels, int hotspotX, int hotspotY){  trussc::bindCursorImage(cursor, width, height, pixels, hotspotX, hotspotY); },
        [](Cursor cursor, const Image & image, int hotspotX, int hotspotY){  trussc::bindCursorImage(cursor, image, hotspotX, hotspotY); }
    ));
    // TrussC.h, LINE 1386
    lua->set_function("unbindCursorImage", [](Cursor cursor){  trussc::unbindCursorImage(cursor); });
    // TrussC.h, LINE 1395
    lua->set_function("setClipboardString", [](const std::string & text){  trussc::setClipboardString(text); });
    // TrussC.h, LINE 1404
    lua->set_function("getClipboardString", [](){ return trussc::getClipboardString(); });
    // TrussC.h, LINE 1412
    lua->set_function("setWindowSize", [](int width, int height){  trussc::setWindowSize(width, height); });
    // TrussC.h, LINE 1424
    lua->set_function("setFullscreen", [](bool full){  trussc::setFullscreen(full); });
    // TrussC.h, LINE 1431
    lua->set_function("isFullscreen", [](){ return trussc::isFullscreen(); });
    // TrussC.h, LINE 1436
    lua->set_function("toggleFullscreen", [](){  trussc::toggleFullscreen(); });
    // TrussC.h, LINE 1462
    lua->set_function("setOrientation", [](Orientation mask){  trussc::setOrientation(mask); });
    // TrussC.h, LINE 1471
    lua->set_function("getWindowWidth", [](){ return trussc::getWindowWidth(); });
    // TrussC.h, LINE 1479
    lua->set_function("getWindowHeight", [](){ return trussc::getWindowHeight(); });
    // TrussC.h, LINE 1487
    lua->set_function("getWindowSize", [](){ return trussc::getWindowSize(); });
    // TrussC.h, LINE 1492
    lua->set_function("getAspectRatio", [](){ return trussc::getAspectRatio(); });
    // TrussC.h, LINE 1500
    lua->set_function("getElapsedTime", [](){ return trussc::getElapsedTime(); });
    // TrussC.h, LINE 1513
    lua->set_function("getUpdateCount", [](){ return trussc::getUpdateCount(); });
    // TrussC.h, LINE 1518
    lua->set_function("getDrawCount", [](){ return trussc::getDrawCount(); });
    // TrussC.h, LINE 1523
    lua->set_function("getFrameCount", [](){ return trussc::getFrameCount(); });
    // TrussC.h, LINE 1527
    lua->set_function("getDeltaTime", [](){ return trussc::getDeltaTime(); });
    // TrussC.h, LINE 1532
    lua->set_function("getFrameRate", [](){ return trussc::getFrameRate(); });
    // TrussC.h, LINE 1559
    lua->set_function("getSokolMemoryBytes", [](){ return trussc::getSokolMemoryBytes(); });
    // TrussC.h, LINE 1562
    lua->set_function("getSokolMemoryAllocs", [](){ return trussc::getSokolMemoryAllocs(); });
    // TrussC.h, LINE 1567
    lua->set_function("releaseSglBuffers", [](){  trussc::releaseSglBuffers(); });
    // TrussC.h, LINE 1576
    lua->set_function("getGlobalMouseX", [](){ return trussc::getGlobalMouseX(); });
    // TrussC.h, LINE 1581
    lua->set_function("getGlobalMouseY", [](){ return trussc::getGlobalMouseY(); });
    // TrussC.h, LINE 1586
    lua->set_function("getGlobalPMouseX", [](){ return trussc::getGlobalPMouseX(); });
    // TrussC.h, LINE 1591
    lua->set_function("getGlobalPMouseY", [](){ return trussc::getGlobalPMouseY(); });
    // TrussC.h, LINE 1596
    lua->set_function("isMousePressed", [](){ return trussc::isMousePressed(); });
    // TrussC.h, LINE 1601
    lua->set_function("getMouseButton", [](){ return trussc::getMouseButton(); });
    // TrussC.h, LINE 1606
    lua->set_function("isKeyPressed", [](int key){ return trussc::isKeyPressed(key); });
    // TrussC.h, LINE 1611
    lua->set_function("getMouseX", [](){ return trussc::getMouseX(); });
    // TrussC.h, LINE 1612
    lua->set_function("getMouseY", [](){ return trussc::getMouseY(); });
    // TrussC.h, LINE 1613
    lua->set_function("getMousePos", [](){ return trussc::getMousePos(); });
    // TrussC.h, LINE 1614
    lua->set_function("getGlobalMousePos", [](){ return trussc::getGlobalMousePos(); });
    // TrussC.h, LINE 1622
    lua->set_function("setTouchAsMouse", [](bool enabled){  trussc::setTouchAsMouse(enabled); });
    // TrussC.h, LINE 1623
    lua->set_function("getTouchAsMouse", [](){ return trussc::getTouchAsMouse(); });
    // TrussC.h, LINE 1630
    lua->set_function("getBackendName", [](){ return trussc::getBackendName(); });
    // TrussC.h, LINE 1644
    lua->set_function("getMemoryUsage", [](){ return trussc::getMemoryUsage(); });
    // TrussC.h, LINE 1684
    lua->set_function("getNodeCount", [](){ return trussc::getNodeCount(); });
    // TrussC.h, LINE 1685
    lua->set_function("getTextureCount", [](){ return trussc::getTextureCount(); });
    // TrussC.h, LINE 1686
    lua->set_function("getFboCount", [](){ return trussc::getFboCount(); });
    // TrussC.h, LINE 1710
    lua->set_function("setFps", [](float fps){  trussc::setFps(fps); });
    // TrussC.h, LINE 1720
    lua->set_function("setIndependentFps", [](float updateFps, float drawFps){  trussc::setIndependentFps(updateFps, drawFps); });
    // TrussC.h, LINE 1729
    lua->set_function("getFpsSettings", [](){ return trussc::getFpsSettings(); });
    // TrussC.h, LINE 1749
    lua->set_function("getFps", [](){ return trussc::getFps(); });
    // TrussC.h, LINE 1755
    lua->set_function("redraw", [](int count){  trussc::redraw(count); });
    // TrussC.h, LINE 1763
    lua->set_function("requestExitApp", [](){  trussc::requestExitApp(); });
    // TrussC.h, LINE 1769
    lua->set_function("exitApp", [](){  trussc::exitApp(); });
    // TrussC.h, LINE 1779
    lua->set_function("grabScreen", [](Pixels & outPixels){ return trussc::grabScreen(outPixels); });
    // TrussC.h, LINE 1918
    lua->set_function("registerInspectionTools", [](){  trussc::mcp::registerInspectionTools(); });
    // TrussC.h, LINE 1919
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
    // TrussC.h, LINE 2568
    // TrussC.h, LINE 2577
    // TrussC.h, LINE 2581
    // TrussC.h, LINE 2588
    // TrussC.h, LINE 2592
    // TrussC.h, LINE 2596
    lua->set_function("drawBox", sol::overload(
        [](float w, float h, float d){  trussc::drawBox(w, h, d); },
        [](float size){  trussc::drawBox(size); },
        [](Vec3 pos, float w, float h, float d){  trussc::drawBox(pos, w, h, d); },
        [](float x, float y, float z, float w, float h, float d){  trussc::drawBox(x, y, z, w, h, d); },
        [](Vec3 pos, float size){  trussc::drawBox(pos, size); },
        [](float x, float y, float z, float size){  trussc::drawBox(x, y, z, size); }
    ));
    // TrussC.h, LINE 2600
    // TrussC.h, LINE 2609
    // TrussC.h, LINE 2616
    lua->set_function("drawSphere", sol::overload(
        [](float radius, int resolution){  trussc::drawSphere(radius, resolution); },
        [](Vec3 pos, float radius, int resolution){  trussc::drawSphere(pos, radius, resolution); },
        [](float x, float y, float z, float radius, int resolution){  trussc::drawSphere(x, y, z, radius, resolution); }
    ));
    // TrussC.h, LINE 2620
    // TrussC.h, LINE 2629
    // TrussC.h, LINE 2636
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
