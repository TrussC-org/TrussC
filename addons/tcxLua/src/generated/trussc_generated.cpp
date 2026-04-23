
// WARNING: This file is auto-generated!

#include "tcxLua.h"
#include "TrussC.h"

#include "sol/sol.hpp"
using namespace tc;

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
    // TrussC.h, LINE 337
    lua->set_function("getDpiScale", [](){ return trussc::getDpiScale(); });
    // TrussC.h, LINE 342
    lua->set_function("getFramebufferWidth", [](){ return trussc::getFramebufferWidth(); });
    // TrussC.h, LINE 346
    lua->set_function("getFramebufferHeight", [](){ return trussc::getFramebufferHeight(); });
    // TrussC.h, LINE 357
    lua->set_function("beginFrame", [](){  trussc::beginFrame(); });
    // TrussC.h, LINE 373
    // TrussC.h, LINE 376
    // TrussC.h, LINE 381
    // TrussC.h, LINE 386
    lua->set_function("clear", sol::overload(
        [](float r, float g, float b, float a){  trussc::clear(r, g, b, a); },
        [](){  trussc::clear(); },
        [](float gray, float a){  trussc::clear(gray, a); },
        [](const Color & c){  trussc::clear(c); },
        // NOTE: additional overloads provided by user,
        [](float r, float g, float b){  trussc::clear(r, g, b); }
    ));
    // TrussC.h, LINE 391
    lua->set_function("flushDeferredShaderDraws", [](){  trussc::flushDeferredShaderDraws(); });
    // TrussC.h, LINE 398
    lua->set_function("ensureSwapchainPass", [](){  trussc::ensureSwapchainPass(); });
    // TrussC.h, LINE 401
    lua->set_function("present", [](){  trussc::present(); });
    // TrussC.h, LINE 404
    lua->set_function("isInSwapchainPass", [](){ return trussc::isInSwapchainPass(); });
    // TrussC.h, LINE 407
    lua->set_function("suspendSwapchainPass", [](){  trussc::suspendSwapchainPass(); });
    // TrussC.h, LINE 410
    lua->set_function("resumeSwapchainPass", [](){  trussc::resumeSwapchainPass(); });
    // TrussC.h, LINE 417
    // TrussC.h, LINE 422
    // TrussC.h, LINE 427
    lua->set_function("setColor", sol::overload(
        [](float r, float g, float b, float a){  trussc::setColor(r, g, b, a); },
        [](float gray, float a){  trussc::setColor(gray, a); },
        [](const Color & c){  trussc::setColor(c); },
        // NOTE: additional overloads provided by user,
        [](float r, float g, float b){  trussc::setColor(r, g, b); }
    ));
    // TrussC.h, LINE 432
    lua->set_function("getColor", [](){ return trussc::getColor(); });
    // TrussC.h, LINE 437
    lua->set_function("setColorHSB", sol::overload(
        [](float h, float s, float b, float a){  trussc::setColorHSB(h, s, b, a); },
        // NOTE: additional overloads provided by user,
        [](float h, float s, float b){  trussc::setColorHSB(h, s, b); }
    ));
    // TrussC.h, LINE 442
    lua->set_function("setColorOKLab", [](float L, float a_lab, float b_lab, float alpha){  trussc::setColorOKLab(L, a_lab, b_lab, alpha); });
    // TrussC.h, LINE 447
    lua->set_function("setColorOKLCH", [](float L, float C, float H, float alpha){  trussc::setColorOKLCH(L, C, H, alpha); });
    // TrussC.h, LINE 452
    lua->set_function("fill", [](){  trussc::fill(); });
    // TrussC.h, LINE 457
    lua->set_function("noFill", [](){  trussc::noFill(); });
    // TrussC.h, LINE 462
    lua->set_function("setStrokeWeight", [](float weight){  trussc::setStrokeWeight(weight); });
    // TrussC.h, LINE 466
    lua->set_function("getStrokeWeight", [](){ return trussc::getStrokeWeight(); });
    // TrussC.h, LINE 470
    lua->set_function("setStrokeCap", [](StrokeCap cap){  trussc::setStrokeCap(cap); });
    // TrussC.h, LINE 474
    lua->set_function("getStrokeCap", [](){ return trussc::getStrokeCap(); });
    // TrussC.h, LINE 478
    lua->set_function("setStrokeJoin", [](StrokeJoin join){  trussc::setStrokeJoin(join); });
    // TrussC.h, LINE 482
    lua->set_function("getStrokeJoin", [](){ return trussc::getStrokeJoin(); });
    // TrussC.h, LINE 505
    // TrussC.h, LINE 511
    lua->set_function("setScissor", sol::overload(
        [](float x, float y, float w, float h){  trussc::setScissor(x, y, w, h); },
        [](int x, int y, int w, int h){  trussc::setScissor(x, y, w, h); }
    ));
    // TrussC.h, LINE 516
    lua->set_function("resetScissor", [](){  trussc::resetScissor(); });
    // TrussC.h, LINE 522
    lua->set_function("pushScissor", [](float x, float y, float w, float h){  trussc::pushScissor(x, y, w, h); });
    // TrussC.h, LINE 540
    lua->set_function("popScissor", [](){  trussc::popScissor(); });
    // TrussC.h, LINE 562
    lua->set_function("pushMatrix", [](){  trussc::pushMatrix(); });
    // TrussC.h, LINE 567
    lua->set_function("popMatrix", [](){  trussc::popMatrix(); });
    // TrussC.h, LINE 572
    lua->set_function("pushStyle", [](){  trussc::pushStyle(); });
    // TrussC.h, LINE 577
    lua->set_function("popStyle", [](){  trussc::popStyle(); });
    // TrussC.h, LINE 582
    lua->set_function("resetStyle", [](){  trussc::resetStyle(); });
    // TrussC.h, LINE 587
    // TrussC.h, LINE 591
    // TrussC.h, LINE 595
    lua->set_function("translate", sol::overload(
        [](Vec3 pos){  trussc::translate(pos); },
        [](float x, float y, float z){  trussc::translate(x, y, z); },
        [](float x, float y){  trussc::translate(x, y); }
    ));
    // TrussC.h, LINE 600
    // TrussC.h, LINE 637
    // TrussC.h, LINE 643
    // TrussC.h, LINE 647
    lua->set_function("rotate", sol::overload(
        [](float radians){  trussc::rotate(radians); },
        [](float x, float y, float z){  trussc::rotate(x, y, z); },
        [](const Vec3 & euler){  trussc::rotate(euler); },
        [](const Quaternion & quat){  trussc::rotate(quat); }
    ));
    // TrussC.h, LINE 605
    lua->set_function("rotateX", [](float radians){  trussc::rotateX(radians); });
    // TrussC.h, LINE 610
    lua->set_function("rotateY", [](float radians){  trussc::rotateY(radians); });
    // TrussC.h, LINE 615
    lua->set_function("rotateZ", [](float radians){  trussc::rotateZ(radians); });
    // TrussC.h, LINE 620
    // TrussC.h, LINE 652
    // TrussC.h, LINE 658
    lua->set_function("rotateDeg", sol::overload(
        [](float degrees){  trussc::rotateDeg(degrees); },
        [](float x, float y, float z){  trussc::rotateDeg(x, y, z); },
        [](const Vec3 & euler){  trussc::rotateDeg(euler); }
    ));
    // TrussC.h, LINE 624
    lua->set_function("rotateXDeg", [](float degrees){  trussc::rotateXDeg(degrees); });
    // TrussC.h, LINE 628
    lua->set_function("rotateYDeg", [](float degrees){  trussc::rotateYDeg(degrees); });
    // TrussC.h, LINE 632
    lua->set_function("rotateZDeg", [](float degrees){  trussc::rotateZDeg(degrees); });
    // TrussC.h, LINE 663
    // TrussC.h, LINE 668
    // TrussC.h, LINE 673
    lua->set_function("scale", sol::overload(
        [](float s){  trussc::scale(s); },
        [](float sx, float sy){  trussc::scale(sx, sy); },
        [](float sx, float sy, float sz){  trussc::scale(sx, sy, sz); }
    ));
    // TrussC.h, LINE 678
    lua->set_function("getCurrentMatrix", [](){ return trussc::getCurrentMatrix(); });
    // TrussC.h, LINE 683
    lua->set_function("resetMatrix", [](){  trussc::resetMatrix(); });
    // TrussC.h, LINE 688
    lua->set_function("setMatrix", [](const Mat4 & mat){  trussc::setMatrix(mat); });
    // TrussC.h, LINE 698
    lua->set_function("setBlendMode", [](BlendMode mode){  trussc::setBlendMode(mode); });
    // TrussC.h, LINE 707
    lua->set_function("getBlendMode", [](){ return trussc::getBlendMode(); });
    // TrussC.h, LINE 712
    lua->set_function("resetBlendMode", [](){  trussc::resetBlendMode(); });
    // TrussC.h, LINE 717
    lua->set_function("restoreBlendPipeline", [](){  trussc::restoreBlendPipeline(); });
    // TrussC.h, LINE 730
    lua->set_function("enable3D", [](){  trussc::enable3D(); });
    // TrussC.h, LINE 739
    lua->set_function("enable3DPerspective", [](float fovY, float nearZ, float farZ){  trussc::enable3DPerspective(fovY, nearZ, farZ); });
    // TrussC.h, LINE 860
    lua->set_function("setupScreenFov", [](float fovDeg, float nearDist, float farDist){  trussc::setupScreenFov(fovDeg, nearDist, farDist); });
    // TrussC.h, LINE 867
    lua->set_function("setupScreenPerspective", [](float fovDeg, float nearDist, float farDist){  trussc::setupScreenPerspective(fovDeg, nearDist, farDist); });
    // TrussC.h, LINE 872
    lua->set_function("setupScreenOrtho", [](){  trussc::setupScreenOrtho(); });
    // TrussC.h, LINE 878
    lua->set_function("setDefaultScreenFov", [](float fovDeg){  trussc::setDefaultScreenFov(fovDeg); });
    // TrussC.h, LINE 882
    lua->set_function("getDefaultScreenFov", [](){ return trussc::getDefaultScreenFov(); });
    // TrussC.h, LINE 887
    lua->set_function("setNearClip", [](float nearDist){  trussc::setNearClip(nearDist); });
    // TrussC.h, LINE 891
    lua->set_function("setFarClip", [](float farDist){  trussc::setFarClip(farDist); });
    // TrussC.h, LINE 895
    lua->set_function("getNearClip", [](){ return trussc::getNearClip(); });
    // TrussC.h, LINE 899
    lua->set_function("getFarClip", [](){ return trussc::getFarClip(); });
    // TrussC.h, LINE 909
    lua->set_function("worldToScreen", [](const Vec3 & worldPos){ return trussc::worldToScreen(worldPos); });
    // TrussC.h, LINE 938
    lua->set_function("screenToWorld", [](const Vec2 & screenPos, float worldZ){ return trussc::screenToWorld(screenPos, worldZ); });
    // TrussC.h, LINE 984
    lua->set_function("disable3D", [](){  trussc::disable3D(); });
    // TrussC.h, LINE 993
    // TrussC.h, LINE 997
    // TrussC.h, LINE 1001
    lua->set_function("drawRect", sol::overload(
        [](Vec3 pos, Vec2 size){  trussc::drawRect(pos, size); },
        [](Vec3 pos, float w, float h){  trussc::drawRect(pos, w, h); },
        [](float x, float y, float w, float h){  trussc::drawRect(x, y, w, h); }
    ));
    // TrussC.h, LINE 1006
    // TrussC.h, LINE 1010
    lua->set_function("drawRectRounded", sol::overload(
        [](Vec3 pos, Vec2 size, float radius){  trussc::drawRectRounded(pos, size, radius); },
        [](float x, float y, float w, float h, float radius){  trussc::drawRectRounded(x, y, w, h, radius); }
    ));
    // TrussC.h, LINE 1015
    // TrussC.h, LINE 1019
    lua->set_function("drawRectSquircle", sol::overload(
        [](Vec3 pos, Vec2 size, float radius){  trussc::drawRectSquircle(pos, size, radius); },
        [](float x, float y, float w, float h, float radius){  trussc::drawRectSquircle(x, y, w, h, radius); }
    ));
    // TrussC.h, LINE 1024
    // TrussC.h, LINE 1028
    lua->set_function("drawCircle", sol::overload(
        [](Vec3 center, float radius){  trussc::drawCircle(center, radius); },
        [](float cx, float cy, float radius){  trussc::drawCircle(cx, cy, radius); }
    ));
    // TrussC.h, LINE 1033
    // TrussC.h, LINE 1037
    // TrussC.h, LINE 1041
    lua->set_function("drawEllipse", sol::overload(
        [](Vec3 center, Vec2 radii){  trussc::drawEllipse(center, radii); },
        [](Vec3 center, float rx, float ry){  trussc::drawEllipse(center, rx, ry); },
        [](float cx, float cy, float rx, float ry){  trussc::drawEllipse(cx, cy, rx, ry); }
    ));
    // TrussC.h, LINE 1046
    // TrussC.h, LINE 1050
    // TrussC.h, LINE 1054
    lua->set_function("drawLine", sol::overload(
        [](Vec3 p1, Vec3 p2){  trussc::drawLine(p1, p2); },
        [](float x1, float y1, float x2, float y2){  trussc::drawLine(x1, y1, x2, y2); },
        [](float x1, float y1, float z1, float x2, float y2, float z2){  trussc::drawLine(x1, y1, z1, x2, y2, z2); }
    ));
    // TrussC.h, LINE 1059
    // TrussC.h, LINE 1063
    lua->set_function("drawTriangle", sol::overload(
        [](Vec3 p1, Vec3 p2, Vec3 p3){  trussc::drawTriangle(p1, p2, p3); },
        [](float x1, float y1, float x2, float y2, float x3, float y3){  trussc::drawTriangle(x1, y1, x2, y2, x3, y3); }
    ));
    // TrussC.h, LINE 1068
    // TrussC.h, LINE 1072
    lua->set_function("drawPoint", sol::overload(
        [](Vec3 pos){  trussc::drawPoint(pos); },
        [](float x, float y){  trussc::drawPoint(x, y); }
    ));
    // TrussC.h, LINE 1077
    lua->set_function("setCircleResolution", [](int res){  trussc::setCircleResolution(res); });
    // TrussC.h, LINE 1081
    lua->set_function("getCircleResolution", [](){ return trussc::getCircleResolution(); });
    // TrussC.h, LINE 1086
    lua->set_function("isFillEnabled", [](){ return trussc::isFillEnabled(); });
    // TrussC.h, LINE 1090
    lua->set_function("isStrokeEnabled", [](){ return trussc::isStrokeEnabled(); });
    // TrussC.h, LINE 1124
    // TrussC.h, LINE 1128
    // TrussC.h, LINE 1133
    // TrussC.h, LINE 1137
    // TrussC.h, LINE 1142
    // TrussC.h, LINE 1147
    lua->set_function("drawBitmapString", sol::overload(
        [](const std::string & text, Vec3 pos, bool screenFixed){  trussc::drawBitmapString(text, pos, screenFixed); },
        [](const std::string & text, float x, float y, bool screenFixed){  trussc::drawBitmapString(text, x, y, screenFixed); },
        [](const std::string & text, Vec3 pos, float scale){  trussc::drawBitmapString(text, pos, scale); },
        [](const std::string & text, float x, float y, float scale){  trussc::drawBitmapString(text, x, y, scale); },
        [](const std::string & text, Vec3 pos, Direction h, Direction v){  trussc::drawBitmapString(text, pos, h, v); },
        [](const std::string & text, float x, float y, Direction h, Direction v){  trussc::drawBitmapString(text, x, y, h, v); }
    ));
    // TrussC.h, LINE 1153
    lua->set_function("setTextAlign", [](Direction h, Direction v){  trussc::setTextAlign(h, v); });
    // TrussC.h, LINE 1158
    lua->set_function("getTextAlignH", [](){ return trussc::getTextAlignH(); });
    // TrussC.h, LINE 1162
    lua->set_function("getTextAlignV", [](){ return trussc::getTextAlignV(); });
    // TrussC.h, LINE 1167
    lua->set_function("setBitmapLineHeight", [](float h){  trussc::setBitmapLineHeight(h); });
    // TrussC.h, LINE 1172
    lua->set_function("getBitmapLineHeight", [](){ return trussc::getBitmapLineHeight(); });
    // TrussC.h, LINE 1177
    lua->set_function("getBitmapFontHeight", [](){ return trussc::getBitmapFontHeight(); });
    // TrussC.h, LINE 1182
    lua->set_function("getBitmapStringWidth", [](const std::string & text){ return trussc::getBitmapStringWidth(text); });
    // TrussC.h, LINE 1187
    lua->set_function("getBitmapStringHeight", [](const std::string & text){ return trussc::getBitmapStringHeight(text); });
    // TrussC.h, LINE 1192
    lua->set_function("getBitmapStringBBox", [](const std::string & text){ return trussc::getBitmapStringBBox(text); });
    // TrussC.h, LINE 1197
    lua->set_function("drawBitmapStringHighlight", [](const std::string & text, float x, float y, const Color & background, const Color & foreground){  trussc::drawBitmapStringHighlight(text, x, y, background, foreground); });
    // TrussC.h, LINE 1303
    lua->set_function("setWindowTitle", [](const std::string & title){  trussc::setWindowTitle(title); });
    // TrussC.h, LINE 1343
    lua->set_function("showCursor", [](){  trussc::showCursor(); });
    // TrussC.h, LINE 1348
    lua->set_function("hideCursor", [](){  trussc::hideCursor(); });
    // TrussC.h, LINE 1353
    lua->set_function("setCursor", [](Cursor cursor){  trussc::setCursor(cursor); });
    // TrussC.h, LINE 1358
    lua->set_function("getCursor", [](){ return trussc::getCursor(); });
    // TrussC.h, LINE 1364
    // TrussC.h, LINE 2518
    lua->set_function("bindCursorImage", sol::overload(
        [](Cursor cursor, int width, int height, const unsigned char * pixels, int hotspotX, int hotspotY){  trussc::bindCursorImage(cursor, width, height, pixels, hotspotX, hotspotY); },
        [](Cursor cursor, const Image & image, int hotspotX, int hotspotY){  trussc::bindCursorImage(cursor, image, hotspotX, hotspotY); }
    ));
    // TrussC.h, LINE 1380
    lua->set_function("unbindCursorImage", [](Cursor cursor){  trussc::unbindCursorImage(cursor); });
    // TrussC.h, LINE 1389
    lua->set_function("setClipboardString", [](const std::string & text){  trussc::setClipboardString(text); });
    // TrussC.h, LINE 1398
    lua->set_function("getClipboardString", [](){ return trussc::getClipboardString(); });
    // TrussC.h, LINE 1406
    lua->set_function("setWindowSize", [](int width, int height){  trussc::setWindowSize(width, height); });
    // TrussC.h, LINE 1418
    lua->set_function("setFullscreen", [](bool full){  trussc::setFullscreen(full); });
    // TrussC.h, LINE 1425
    lua->set_function("isFullscreen", [](){ return trussc::isFullscreen(); });
    // TrussC.h, LINE 1430
    lua->set_function("toggleFullscreen", [](){  trussc::toggleFullscreen(); });
    // TrussC.h, LINE 1456
    lua->set_function("setOrientation", [](Orientation mask){  trussc::setOrientation(mask); });
    // TrussC.h, LINE 1465
    lua->set_function("getWindowWidth", [](){ return trussc::getWindowWidth(); });
    // TrussC.h, LINE 1473
    lua->set_function("getWindowHeight", [](){ return trussc::getWindowHeight(); });
    // TrussC.h, LINE 1481
    lua->set_function("getWindowSize", [](){ return trussc::getWindowSize(); });
    // TrussC.h, LINE 1486
    lua->set_function("getAspectRatio", [](){ return trussc::getAspectRatio(); });
    // TrussC.h, LINE 1494
    lua->set_function("getElapsedTime", [](){ return trussc::getElapsedTime(); });
    // TrussC.h, LINE 1507
    lua->set_function("getUpdateCount", [](){ return trussc::getUpdateCount(); });
    // TrussC.h, LINE 1512
    lua->set_function("getDrawCount", [](){ return trussc::getDrawCount(); });
    // TrussC.h, LINE 1517
    lua->set_function("getFrameCount", [](){ return trussc::getFrameCount(); });
    // TrussC.h, LINE 1521
    lua->set_function("getDeltaTime", [](){ return trussc::getDeltaTime(); });
    // TrussC.h, LINE 1526
    lua->set_function("getFrameRate", [](){ return trussc::getFrameRate(); });
    // TrussC.h, LINE 1553
    lua->set_function("getSokolMemoryBytes", [](){ return trussc::getSokolMemoryBytes(); });
    // TrussC.h, LINE 1556
    lua->set_function("getSokolMemoryAllocs", [](){ return trussc::getSokolMemoryAllocs(); });
    // TrussC.h, LINE 1561
    lua->set_function("releaseSglBuffers", [](){  trussc::releaseSglBuffers(); });
    // TrussC.h, LINE 1570
    lua->set_function("getGlobalMouseX", [](){ return trussc::getGlobalMouseX(); });
    // TrussC.h, LINE 1575
    lua->set_function("getGlobalMouseY", [](){ return trussc::getGlobalMouseY(); });
    // TrussC.h, LINE 1580
    lua->set_function("getGlobalPMouseX", [](){ return trussc::getGlobalPMouseX(); });
    // TrussC.h, LINE 1585
    lua->set_function("getGlobalPMouseY", [](){ return trussc::getGlobalPMouseY(); });
    // TrussC.h, LINE 1590
    lua->set_function("isMousePressed", [](){ return trussc::isMousePressed(); });
    // TrussC.h, LINE 1595
    lua->set_function("getMouseButton", [](){ return trussc::getMouseButton(); });
    // TrussC.h, LINE 1600
    lua->set_function("isKeyPressed", [](int key){ return trussc::isKeyPressed(key); });
    // TrussC.h, LINE 1605
    lua->set_function("getMouseX", [](){ return trussc::getMouseX(); });
    // TrussC.h, LINE 1606
    lua->set_function("getMouseY", [](){ return trussc::getMouseY(); });
    // TrussC.h, LINE 1607
    lua->set_function("getMousePos", [](){ return trussc::getMousePos(); });
    // TrussC.h, LINE 1608
    lua->set_function("getGlobalMousePos", [](){ return trussc::getGlobalMousePos(); });
    // TrussC.h, LINE 1616
    lua->set_function("setTouchAsMouse", [](bool enabled){  trussc::setTouchAsMouse(enabled); });
    // TrussC.h, LINE 1617
    lua->set_function("getTouchAsMouse", [](){ return trussc::getTouchAsMouse(); });
    // TrussC.h, LINE 1624
    lua->set_function("getBackendName", [](){ return trussc::getBackendName(); });
    // TrussC.h, LINE 1638
    lua->set_function("getMemoryUsage", [](){ return trussc::getMemoryUsage(); });
    // TrussC.h, LINE 1678
    lua->set_function("getNodeCount", [](){ return trussc::getNodeCount(); });
    // TrussC.h, LINE 1679
    lua->set_function("getTextureCount", [](){ return trussc::getTextureCount(); });
    // TrussC.h, LINE 1680
    lua->set_function("getFboCount", [](){ return trussc::getFboCount(); });
    // TrussC.h, LINE 1704
    lua->set_function("setFps", [](float fps){  trussc::setFps(fps); });
    // TrussC.h, LINE 1714
    lua->set_function("setIndependentFps", [](float updateFps, float drawFps){  trussc::setIndependentFps(updateFps, drawFps); });
    // TrussC.h, LINE 1723
    lua->set_function("getFpsSettings", [](){ return trussc::getFpsSettings(); });
    // TrussC.h, LINE 1743
    lua->set_function("getFps", [](){ return trussc::getFps(); });
    // TrussC.h, LINE 1749
    lua->set_function("redraw", [](int count){  trussc::redraw(count); });
    // TrussC.h, LINE 1757
    lua->set_function("requestExitApp", [](){  trussc::requestExitApp(); });
    // TrussC.h, LINE 1763
    lua->set_function("exitApp", [](){  trussc::exitApp(); });
    // TrussC.h, LINE 1773
    lua->set_function("grabScreen", [](Pixels & outPixels){ return trussc::grabScreen(outPixels); });
    // TrussC.h, LINE 1912
    lua->set_function("registerInspectionTools", [](){  trussc::mcp::registerInspectionTools(); });
    // TrussC.h, LINE 1913
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
    // TrussC.h, LINE 2562
    // TrussC.h, LINE 2571
    // TrussC.h, LINE 2575
    // TrussC.h, LINE 2582
    // TrussC.h, LINE 2586
    // TrussC.h, LINE 2590
    lua->set_function("drawBox", sol::overload(
        [](float w, float h, float d){  trussc::drawBox(w, h, d); },
        [](float size){  trussc::drawBox(size); },
        [](Vec3 pos, float w, float h, float d){  trussc::drawBox(pos, w, h, d); },
        [](float x, float y, float z, float w, float h, float d){  trussc::drawBox(x, y, z, w, h, d); },
        [](Vec3 pos, float size){  trussc::drawBox(pos, size); },
        [](float x, float y, float z, float size){  trussc::drawBox(x, y, z, size); }
    ));
    // TrussC.h, LINE 2594
    // TrussC.h, LINE 2603
    // TrussC.h, LINE 2610
    lua->set_function("drawSphere", sol::overload(
        [](float radius, int resolution){  trussc::drawSphere(radius, resolution); },
        [](Vec3 pos, float radius, int resolution){  trussc::drawSphere(pos, radius, resolution); },
        [](float x, float y, float z, float radius, int resolution){  trussc::drawSphere(x, y, z, radius, resolution); }
    ));
    // TrussC.h, LINE 2614
    // TrussC.h, LINE 2623
    // TrussC.h, LINE 2630
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
