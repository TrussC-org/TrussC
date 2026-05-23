// =============================================================================
// tcRenderContext.cpp - RenderContext Large Method Implementations
// Large methods moved from tcRenderContext.h for better compile times
// =============================================================================

#include <TrussC.h>

namespace trussc {

// ---------------------------------------------------------------------------
// Rounded Rectangle (circular arc corners)
// ---------------------------------------------------------------------------
void RenderContext::drawRectRounded(Vec3 pos, Vec2 size, float radius) {
    float x = pos.x, y = pos.y;
    float w = size.x, h = size.y;
    (void)pos.z;  // Outline built via beginShape; z=0 plane.

    // Normalize negative dimensions so the rest of this function can assume
    // positive w/h. Plain drawRect works with negative dims (the GPU just
    // flips the quad), but our radius clamp and arc geometry below need
    // positive values — without this, a negative w/h made `min(w,h)*0.5`
    // negative, which collapsed the radius clamp to <=0 and silently fell
    // through to drawRect, dropping the corners.
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }

    // Clamp radius to half of smaller dimension; fall through to a plain
    // rect when there's nothing to round.
    radius = std::min(radius, std::min(w, h) * 0.5f);
    if (radius <= 0) {
        drawRect(Vec3(x, y, pos.z), Vec2(w, h));
        return;
    }

    // Build the closed outline as one polyline via beginShape + appendArc,
    // sweeping clockwise (visually) around the rect. Y grows downward in
    // TrussC, so each corner's arc spans QUARTER_TAU but the angle ranges
    // start where the previous edge ends — the duplicate vertex at each
    // corner is harmless (degenerate triangle in the fan).
    //
    // Angle conventions (cos, sin in y-down screen):
    //   angle =          0 -> right         (cos=1, sin=0)
    //   angle =  QUARTER_TAU -> bottom      (cos=0, sin=+1)  [y grows down]
    //   angle =     HALF_TAU -> left        (cos=-1, sin=0)
    //   angle =  3*QTR_TAU  -> top          (cos=0, sin=-1)
    constexpr float Q  = QUARTER_TAU;
    constexpr float H  = HALF_TAU;
    constexpr float Q3 = 3.0f * Q;          // top of corner circle

    beginShape();
    // Top edge
    vertex(x + radius,         y);
    vertex(x + w - radius,     y);
    appendArc(x + w - radius,  y + radius,        radius,  Q3,    Q3 + Q); // top-right (Q3 -> 0)
    // Right edge
    vertex(x + w,              y + h - radius);
    appendArc(x + w - radius,  y + h - radius,    radius,   0,    Q);      // bottom-right
    // Bottom edge
    vertex(x + radius,         y + h);
    appendArc(x + radius,      y + h - radius,    radius,   Q,    H);      // bottom-left
    // Left edge
    vertex(x,                  y + radius);
    appendArc(x + radius,      y + radius,        radius,   H,    Q3);     // top-left
    endShape(true);
}

// ---------------------------------------------------------------------------
// Squircle Rectangle (superellipse corners, curvature continuous)
// ---------------------------------------------------------------------------
void RenderContext::drawRectSquircle(Vec3 pos, Vec2 size, float radius) {
    float x = pos.x, y = pos.y, z = pos.z;
    float w = size.x, h = size.y;

    // Normalize negative dimensions; see drawRectRounded for rationale.
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }

    radius = std::min(radius, std::min(w, h) * 0.5f);
    if (radius <= 0) {
        drawRect(Vec3(x, y, z), Vec2(w, h));
        return;
    }

    auto& writer = internal::getActiveWriter();
    // Squircle deviates from a circle by O(r), so segment count derived
    // from the inscribed circle is a conservative-enough match for visual
    // smoothness (the design doc accepts this approximation).
    int segs = std::max(2, decideArcSegments(radius, QUARTER_TAU));
    int halfSegs = segs / 2;

    // Pre-compute squircle offsets for 1/8 circle (0 to 45 degrees only)
    // Superellipse n=4: offset = sqrt(|cos/sin|) * radius
    std::vector<Vec2> offsets(halfSegs + 1);
    for (int i = 0; i <= halfSegs; i++) {
        float angle = (float)i / segs * QUARTER_TAU;  // 0 to 45 degrees
        offsets[i] = Vec2(
            std::sqrt(std::cos(angle)) * radius,
            std::sqrt(std::sin(angle)) * radius
        );
    }

    // Get offset for 0-90 degrees using symmetry at 45 degrees
    auto getOffset = [&](int i) -> Vec2 {
        if (i <= halfSegs) {
            return offsets[i];
        } else {
            // Mirror: swap x,y for 45-90 degrees
            Vec2 o = offsets[segs - i];
            return Vec2(o.y, o.x);
        }
    };

    // Corner centers
    float tlX = x + radius, tlY = y + radius;
    float trX = x + w - radius, trY = y + radius;
    float brX = x + w - radius, brY = y + h - radius;
    float blX = x + radius, blY = y + h - radius;

    // Helper to get vertex for each corner
    auto cornerVert = [&](int corner, int i) -> Vec2 {
        Vec2 o = getOffset(i);
        switch (corner) {
            case 0: return Vec2(tlX - o.x, tlY - o.y);  // top-left
            case 1: return Vec2(trX + o.y, trY - o.x);  // top-right
            case 2: return Vec2(brX + o.x, brY + o.y);  // bottom-right
            case 3: return Vec2(blX - o.y, blY + o.x);  // bottom-left
            default: return Vec2(0, 0);
        }
    };

    if (fillEnabled_) {
        float cx = x + w * 0.5f, cy = y + h * 0.5f;
        writer.begin(PrimitiveType::TriangleStrip);
        writer.color(currentR_, currentG_, currentB_, currentA_);

        for (int corner = 0; corner < 4; corner++) {
            for (int i = 0; i <= segs; i++) {
                Vec2 v = cornerVert(corner, i);
                writer.vertex(cx, cy, z);
                writer.vertex(v.x, v.y, z);
            }
        }
        // Close
        Vec2 v = cornerVert(0, 0);
        writer.vertex(cx, cy, z);
        writer.vertex(v.x, v.y, z);

        writer.end();
    }

    if (strokeEnabled_) {
        writer.begin(PrimitiveType::LineStrip);
        writer.color(currentR_, currentG_, currentB_, currentA_);

        for (int corner = 0; corner < 4; corner++) {
            for (int i = 0; i <= segs; i++) {
                Vec2 v = cornerVert(corner, i);
                writer.vertex(v.x, v.y, z);
            }
        }
        // Close
        Vec2 v = cornerVert(0, 0);
        writer.vertex(v.x, v.y, z);

        writer.end();
    }
}

// ---------------------------------------------------------------------------
// Bitmap string drawing (base version)
// ---------------------------------------------------------------------------
void RenderContext::drawBitmapString(const std::string& text, float x, float y, bool screenFixed) {
    if (text.empty() || !internal::fontInitialized) return;

    // Calculate offset based on current alignment settings
    Vec2 offset = calcBitmapAlignOffset(text, textAlignH_, textAlignV_);

    if (screenFixed) {
        // Transform local position to world coordinates using current matrix
        // Mat4 is row-major: X' = m[0]*x + m[1]*y + m[3], Y' = m[4]*x + m[5]*y + m[7]
        Mat4 currentMat = getCurrentMatrix();
        float localX = x + offset.x;
        float localY = y + offset.y;
        float worldX = currentMat.m[0]*localX + currentMat.m[1]*localY + currentMat.m[3];
        float worldY = currentMat.m[4]*localX + currentMat.m[5]*localY + currentMat.m[7];

        // Switch to ortho projection for screen-fixed 2D drawing
        sgl_matrix_mode_projection();
        sgl_push_matrix();
        sgl_load_identity();
        sgl_ortho(0.0f, internal::currentViewW, internal::currentViewH, 0.0f, -10000.0f, 10000.0f);

        sgl_matrix_mode_modelview();
        sgl_push_matrix();
        sgl_load_identity();
        sgl_translate(worldX, worldY, 0.0f);

        sgl_load_pipeline((internal::inFboPass && internal::currentFboBlendPipeline.id != 0) ? internal::currentFboBlendPipeline : internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(internal::fontView, internal::fontSampler);

        sgl_begin_quads();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);

        const float charW = bitmapfont::CHAR_TEX_WIDTH;
        const float charH = bitmapfont::CHAR_TEX_HEIGHT;
        float cursorX = 0;
        float cursorY = 0;

        for (char c : text) {
            if (c == '\n') { cursorX = 0; cursorY += style_.bitmapLineHeight; continue; }
            if (c == '\t') { cursorX += charW * 8; continue; }
            if (c < 32) continue;

            float u, v;
            bitmapfont::getCharTexCoord(c, u, v);
            float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
            float v2 = v + bitmapfont::TEX_CHAR_HEIGHT;

            sgl_v2f_t2f(cursorX, cursorY, u, v);
            sgl_v2f_t2f(cursorX + charW, cursorY, u2, v);
            sgl_v2f_t2f(cursorX + charW, cursorY + charH, u2, v2);
            sgl_v2f_t2f(cursorX, cursorY + charH, u, v2);

            cursorX += charW;
        }

        sgl_end();
        sgl_disable_texture();
        if (internal::blendPipelinesInitialized) sgl_load_pipeline(internal::blendPipelines[static_cast<int>(internal::currentBlendMode)]);

        // Restore matrices
        sgl_pop_matrix();
        sgl_matrix_mode_projection();
        sgl_pop_matrix();
        sgl_matrix_mode_modelview();
    } else {
        pushMatrix();
        translate(x + offset.x, y + offset.y);

        sgl_load_pipeline((internal::inFboPass && internal::currentFboBlendPipeline.id != 0) ? internal::currentFboBlendPipeline : internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(internal::fontView, internal::fontSampler);

        sgl_begin_quads();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);

        const float charW = bitmapfont::CHAR_TEX_WIDTH;
        const float charH = bitmapfont::CHAR_TEX_HEIGHT;
        float cursorX = 0;
        float cursorY = 0;

        for (char c : text) {
            if (c == '\n') { cursorX = 0; cursorY += style_.bitmapLineHeight; continue; }
            if (c == '\t') { cursorX += charW * 8; continue; }
            if (c < 32) continue;

            float u, v;
            bitmapfont::getCharTexCoord(c, u, v);
            float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
            float v2 = v + bitmapfont::TEX_CHAR_HEIGHT;

            sgl_v2f_t2f(cursorX, cursorY, u, v);
            sgl_v2f_t2f(cursorX + charW, cursorY, u2, v);
            sgl_v2f_t2f(cursorX + charW, cursorY + charH, u2, v2);
            sgl_v2f_t2f(cursorX, cursorY + charH, u, v2);

            cursorX += charW;
        }

        sgl_end();
        sgl_disable_texture();
        if (internal::blendPipelinesInitialized) sgl_load_pipeline(internal::blendPipelines[static_cast<int>(internal::currentBlendMode)]);

        popMatrix();
    }
}

// ---------------------------------------------------------------------------
// Bitmap string drawing (scale version)
// ---------------------------------------------------------------------------
void RenderContext::drawBitmapString(const std::string& text, float x, float y, float scale) {
    if (text.empty() || !internal::fontInitialized) return;

    // Calculate offset based on current alignment settings
    Vec2 offset = calcBitmapAlignOffset(text, textAlignH_, textAlignV_);

    pushMatrix();
    translate(x + offset.x * scale, y + offset.y * scale);
    sgl_scale(scale, scale, 1.0f);

    sgl_load_pipeline((internal::inFboPass && internal::currentFboBlendPipeline.id != 0) ? internal::currentFboBlendPipeline : internal::fontPipeline);
    sgl_enable_texture();
    sgl_texture(internal::fontView, internal::fontSampler);

    sgl_begin_quads();
    sgl_c4f(currentR_, currentG_, currentB_, currentA_);

    const float charW = bitmapfont::CHAR_TEX_WIDTH;
    const float charH = bitmapfont::CHAR_TEX_HEIGHT;
    float cursorX = 0;
    float cursorY = 0;  // Top-aligned

    for (char c : text) {
        if (c == '\n') {
            cursorX = 0;
            cursorY += style_.bitmapLineHeight;
            continue;
        }
        if (c == '\t') {
            cursorX += charW * 8;
            continue;
        }
        if (c < 32) continue;

        float u, v;
        bitmapfont::getCharTexCoord(c, u, v);
        float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
        float v2 = v + bitmapfont::TEX_CHAR_HEIGHT;

        float px = cursorX;
        float py = cursorY;

        sgl_v2f_t2f(px, py, u, v);
        sgl_v2f_t2f(px + charW, py, u2, v);
        sgl_v2f_t2f(px + charW, py + charH, u2, v2);
        sgl_v2f_t2f(px, py + charH, u, v2);

        cursorX += charW;
    }

    sgl_end();
    sgl_disable_texture();
    internal::restoreCurrentPipeline();

    popMatrix();
}

// ---------------------------------------------------------------------------
// Bitmap string drawing (Direction version)
// ---------------------------------------------------------------------------
void RenderContext::drawBitmapString(const std::string& text, float x, float y,
                                      Direction h, Direction v, bool screenFixed) {
    if (text.empty() || !internal::fontInitialized) return;

    // Calculate offset
    Vec2 offset = calcBitmapAlignOffset(text, h, v);

    if (screenFixed) {
        // Transform local position to world coordinates using current matrix
        Mat4 currentMat = getCurrentMatrix();
        float localX = x + offset.x;
        float localY = y + offset.y;
        float worldX = currentMat.m[0]*localX + currentMat.m[1]*localY + currentMat.m[3];
        float worldY = currentMat.m[4]*localX + currentMat.m[5]*localY + currentMat.m[7];

        // Switch to ortho projection for screen-fixed 2D drawing
        sgl_matrix_mode_projection();
        sgl_push_matrix();
        sgl_load_identity();
        sgl_ortho(0.0f, internal::currentViewW, internal::currentViewH, 0.0f, -10000.0f, 10000.0f);

        sgl_matrix_mode_modelview();
        sgl_push_matrix();
        sgl_load_identity();
        sgl_translate(worldX, worldY, 0.0f);

        sgl_load_pipeline((internal::inFboPass && internal::currentFboBlendPipeline.id != 0) ? internal::currentFboBlendPipeline : internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(internal::fontView, internal::fontSampler);

        sgl_begin_quads();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);

        const float charW = bitmapfont::CHAR_TEX_WIDTH;
        const float charH = bitmapfont::CHAR_TEX_HEIGHT;
        float cursorX = 0;
        float cursorY = 0;

        for (char c : text) {
            if (c == '\n') { cursorX = 0; cursorY += style_.bitmapLineHeight; continue; }
            if (c == '\t') { cursorX += charW * 8; continue; }
            if (c < 32) continue;

            float u, v_;
            bitmapfont::getCharTexCoord(c, u, v_);
            float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
            float v2 = v_ + bitmapfont::TEX_CHAR_HEIGHT;

            sgl_v2f_t2f(cursorX, cursorY, u, v_);
            sgl_v2f_t2f(cursorX + charW, cursorY, u2, v_);
            sgl_v2f_t2f(cursorX + charW, cursorY + charH, u2, v2);
            sgl_v2f_t2f(cursorX, cursorY + charH, u, v2);

            cursorX += charW;
        }

        sgl_end();
        sgl_disable_texture();
        internal::restoreCurrentPipeline();

        // Restore matrices
        sgl_pop_matrix();  // modelview
        sgl_matrix_mode_projection();
        sgl_pop_matrix();
        sgl_matrix_mode_modelview();
    } else {
        // Non-screenFixed: use current transformation
        pushMatrix();
        translate(x + offset.x, y + offset.y);

        sgl_load_pipeline((internal::inFboPass && internal::currentFboBlendPipeline.id != 0) ? internal::currentFboBlendPipeline : internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(internal::fontView, internal::fontSampler);

        sgl_begin_quads();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);

        const float charW = bitmapfont::CHAR_TEX_WIDTH;
        const float charH = bitmapfont::CHAR_TEX_HEIGHT;
        float cursorX = 0;
        float cursorY = 0;

        for (char c : text) {
            if (c == '\n') { cursorX = 0; cursorY += style_.bitmapLineHeight; continue; }
            if (c == '\t') { cursorX += charW * 8; continue; }
            if (c < 32) continue;

            float u, v_;
            bitmapfont::getCharTexCoord(c, u, v_);
            float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
            float v2 = v_ + bitmapfont::TEX_CHAR_HEIGHT;

            sgl_v2f_t2f(cursorX, cursorY, u, v_);
            sgl_v2f_t2f(cursorX + charW, cursorY, u2, v_);
            sgl_v2f_t2f(cursorX + charW, cursorY + charH, u2, v2);
            sgl_v2f_t2f(cursorX, cursorY + charH, u, v2);

            cursorX += charW;
        }

        sgl_end();
        sgl_disable_texture();
        internal::restoreCurrentPipeline();

        popMatrix();
    }
}

} // namespace trussc
