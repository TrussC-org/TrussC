#pragma once

// このファイルは TrussC.h からインクルードされる

#include <vector>
#include <cmath>
#include <algorithm>

namespace trussc {

// =============================================================================
// StrokeMesh - Polylineから太さのある三角形メッシュを生成するクラス
// =============================================================================

class StrokeMesh {
public:
    // Processing / NanoVG ライクなスタイル定義
    enum CapType {
        CAP_BUTT,   // バット（標準：スパッと切る）
        CAP_ROUND,  // 丸（半円）
        CAP_SQUARE  // 四角（太さの分だけ伸ばす）
    };

    enum JoinType {
        JOIN_MITER, // マイター（鋭角に尖らせる）
        JOIN_ROUND, // 丸（角を丸める）
        JOIN_BEVEL  // ベベル（角を平らに落とす）
    };

    // =========================================================================
    // コンストラクタ
    // =========================================================================

    StrokeMesh() {
        strokeWidth_ = 2.0f;
        strokeColor_ = Color(1.0f, 1.0f, 1.0f, 1.0f);
        capType_ = CAP_BUTT;
        joinType_ = JOIN_MITER;
        miterLimit_ = 10.0f;
        bClosed_ = false;
        bDirty_ = true;

        polylines_.push_back(Path());
    }

    StrokeMesh(const Path& polyline) : StrokeMesh() {
        setShape(polyline);
    }

    // =========================================================================
    // 設定 (Settings)
    // =========================================================================

    void setWidth(float width) {
        strokeWidth_ = width;
        bDirty_ = true;
    }

    void setColor(const Color& color) {
        strokeColor_ = color;
        bDirty_ = true;
    }

    void setCapType(CapType type) {
        capType_ = type;
        bDirty_ = true;
    }

    void setJoinType(JoinType type) {
        joinType_ = type;
        bDirty_ = true;
    }

    // Miter Join の時に、どこまで尖るのを許容するか
    void setMiterLimit(float limit) {
        miterLimit_ = limit;
        bDirty_ = true;
    }

    // =========================================================================
    // データ入力 (Input)
    // =========================================================================

    void addVertex(float x, float y, float z = 0) {
        addVertex(Vec3{x, y, z});
    }

    void addVertex(const Vec3& p) {
        if (polylines_.empty()) {
            polylines_.push_back(Path());
        }
        polylines_[0].addVertex(p);
        bDirty_ = true;
    }

    void addVertex(const Vec2& p) {
        addVertex(Vec3{p.x, p.y, 0});
    }

    // 頂点と太さを同時に追加（可変幅ストローク用）
    void addVertexWithWidth(float x, float y, float width) {
        addVertexWithWidth(Vec3{x, y, 0}, width);
    }

    void addVertexWithWidth(const Vec3& p, float width) {
        if (polylines_.empty()) {
            polylines_.push_back(Path());
        }
        polylines_[0].addVertex(p);
        widths_.push_back(width);
        bDirty_ = true;
    }

    // 太さの配列を直接設定
    void setWidths(const std::vector<float>& w) {
        widths_ = w;
        bDirty_ = true;
    }

    // 既存の形状をセットして上書きする
    void setShape(const Path& polyline) {
        polylines_.clear();
        polylines_.push_back(polyline);
        widths_.clear();
        bClosed_ = polyline.isClosed();
        bDirty_ = true;
    }

    // 閉じた形状にするかどうか
    void setClosed(bool closed) {
        bClosed_ = closed;
        bDirty_ = true;
    }

    // クリア
    void clear() {
        polylines_.clear();
        polylines_.push_back(Path());
        widths_.clear();
        mesh_.clear();
        bDirty_ = true;
    }

    // =========================================================================
    // 更新と描画 (Core)
    // =========================================================================

    void update() {
        if (!bDirty_) return;

        mesh_.clear();
        mesh_.setMode(PrimitiveMode::Triangles);

        // 頂点ごとの太さを準備（指定がなければデフォルト値で埋める）
        std::vector<float> vertWidths;
        int totalVerts = 0;
        for (auto& pl : polylines_) {
            totalVerts += pl.size();
        }

        if (widths_.empty()) {
            vertWidths.resize(totalVerts, strokeWidth_);
        } else if ((int)widths_.size() < totalVerts) {
            vertWidths = widths_;
            vertWidths.resize(totalVerts, strokeWidth_);
        } else {
            vertWidths = widths_;
        }

        int widthOffset = 0;
        for (auto& pl : polylines_) {
            if (pl.size() < 2) continue;

            if (bClosed_ && !pl.isClosed()) {
                pl.setClosed(true);
            }

            std::vector<float> plWidths(vertWidths.begin() + widthOffset,
                                        vertWidths.begin() + widthOffset + pl.size());
            appendStrokeToMesh(pl, mesh_, plWidths);
            widthOffset += pl.size();
        }

        bDirty_ = false;
    }

    void draw() {
        mesh_.draw();
    }

    // =========================================================================
    // アクセス (Accessors)
    // =========================================================================

    Mesh& getMesh() {
        return mesh_;
    }

    std::vector<Path>& getPolylines() {
        return polylines_;
    }

private:
    std::vector<Path> polylines_;
    std::vector<float> widths_;
    Mesh mesh_;

    float strokeWidth_;
    Color strokeColor_;
    CapType capType_;
    JoinType joinType_;
    float miterLimit_;
    bool bClosed_;
    bool bDirty_;

    // 法線計算
    Vec3 getNormal(const Vec3& p1, const Vec3& p2) {
        Vec3 dir = p2 - p1;
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len > 0) {
            dir.x /= len;
            dir.y /= len;
            dir.z /= len;
        }
        return Vec3{-dir.y, dir.x, 0.0f};
    }

    // 正規化
    Vec3 normalize(const Vec3& v) {
        float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len > 0) {
            return Vec3{v.x / len, v.y / len, v.z / len};
        }
        return v;
    }

    // 内積
    float dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // 三角形を追加
    void addTriangle(Mesh& mesh, const Vec3& a, const Vec3& b, const Vec3& c, const Color& color) {
        mesh.addVertex(a);
        mesh.addColor(color);
        mesh.addVertex(b);
        mesh.addColor(color);
        mesh.addVertex(c);
        mesh.addColor(color);
    }

    // メインのストローク生成ロジック
    void appendStrokeToMesh(const Path& pl, Mesh& targetMesh, const std::vector<float>& vertWidths) {
        const auto& verts = pl.getVertices();
        int numVerts = pl.size();
        if (numVerts < 2) return;

        bool isClosed = pl.isClosed();
        int numSegments = isClosed ? numVerts : numVerts - 1;

        auto getHalfWidth = [&](int idx) -> float {
            if (idx < 0) idx += numVerts;
            idx = idx % numVerts;
            return vertWidths[idx] * 0.5f;
        };

        // BEVEL/ROUNDの場合
        if (joinType_ == JOIN_BEVEL || joinType_ == JOIN_ROUND) {
            // 各セグメントを独立して描画
            for (int seg = 0; seg < numSegments; seg++) {
                int i0 = seg;
                int i1 = (seg + 1) % numVerts;

                Vec3 p0 = verts[i0];
                Vec3 p1 = verts[i1];
                Vec3 n = getNormal(p0, p1);

                float hw0 = getHalfWidth(i0);
                float hw1 = getHalfWidth(i1);

                Vec3 left0 = Vec3{p0.x + n.x * hw0, p0.y + n.y * hw0, p0.z};
                Vec3 right0 = Vec3{p0.x - n.x * hw0, p0.y - n.y * hw0, p0.z};
                Vec3 left1 = Vec3{p1.x + n.x * hw1, p1.y + n.y * hw1, p1.z};
                Vec3 right1 = Vec3{p1.x - n.x * hw1, p1.y - n.y * hw1, p1.z};

                addTriangle(targetMesh, left0, right0, left1, strokeColor_);
                addTriangle(targetMesh, right0, right1, left1, strokeColor_);
            }

            // 角の処理
            for (int i = 0; i < numVerts; i++) {
                bool isEndpoint = !isClosed && (i == 0 || i == numVerts - 1);
                if (isEndpoint) continue;

                int currSeg = i;
                if (!isClosed && currSeg >= numSegments) continue;

                Vec3 prev = verts[(i - 1 + numVerts) % numVerts];
                Vec3 curr = verts[i];
                Vec3 next = verts[(i + 1) % numVerts];

                Vec3 n1 = getNormal(prev, curr);
                Vec3 n2 = getNormal(curr, next);

                float hw = getHalfWidth(i);

                Vec3 d1 = normalize(Vec3{curr.x - prev.x, curr.y - prev.y, curr.z - prev.z});
                Vec3 d2 = normalize(Vec3{next.x - curr.x, next.y - curr.y, next.z - curr.z});
                float cross = d1.x * d2.y - d1.y * d2.x;

                if (std::abs(cross) < 0.0001f) continue;

                bool turnsLeft = cross < 0;

                Vec3 innerP1, innerP2;
                if (turnsLeft) {
                    innerP1 = Vec3{curr.x - n1.x * hw, curr.y - n1.y * hw, curr.z};
                    innerP2 = Vec3{curr.x - n2.x * hw, curr.y - n2.y * hw, curr.z};
                } else {
                    innerP1 = Vec3{curr.x + n1.x * hw, curr.y + n1.y * hw, curr.z};
                    innerP2 = Vec3{curr.x + n2.x * hw, curr.y + n2.y * hw, curr.z};
                }
                addTriangle(targetMesh, curr, innerP1, innerP2, strokeColor_);

                Vec3 outerP1 = turnsLeft ? Vec3{curr.x + n1.x * hw, curr.y + n1.y * hw, curr.z}
                                         : Vec3{curr.x - n1.x * hw, curr.y - n1.y * hw, curr.z};
                Vec3 outerP2 = turnsLeft ? Vec3{curr.x + n2.x * hw, curr.y + n2.y * hw, curr.z}
                                         : Vec3{curr.x - n2.x * hw, curr.y - n2.y * hw, curr.z};

                if (joinType_ == JOIN_BEVEL) {
                    addTriangle(targetMesh, curr, outerP1, outerP2, strokeColor_);
                }
                else if (joinType_ == JOIN_ROUND) {
                    int segments = std::max(8, (int)(hw * 2));

                    Vec3 dir1 = normalize(Vec3{outerP1.x - curr.x, outerP1.y - curr.y, 0});
                    Vec3 dir2 = normalize(Vec3{outerP2.x - curr.x, outerP2.y - curr.y, 0});
                    float angle1 = std::atan2(dir1.y, dir1.x);
                    float angle2 = std::atan2(dir2.y, dir2.x);

                    float deltaAngle = angle2 - angle1;
                    while (deltaAngle > PI) deltaAngle -= TAU;
                    while (deltaAngle < -PI) deltaAngle += TAU;

                    for (int j = 0; j < segments; j++) {
                        float t1 = (float)j / segments;
                        float t2 = (float)(j + 1) / segments;
                        float a1 = angle1 + deltaAngle * t1;
                        float a2 = angle1 + deltaAngle * t2;

                        Vec3 pt1 = Vec3{curr.x + std::cos(a1) * hw, curr.y + std::sin(a1) * hw, curr.z};
                        Vec3 pt2 = Vec3{curr.x + std::cos(a2) * hw, curr.y + std::sin(a2) * hw, curr.z};

                        addTriangle(targetMesh, curr, pt1, pt2, strokeColor_);
                    }
                }
            }
        }
        else {
            // MITER
            std::vector<Vec3> leftPoints;
            std::vector<Vec3> rightPoints;
            leftPoints.reserve(numVerts);
            rightPoints.reserve(numVerts);

            for (int i = 0; i < numVerts; i++) {
                Vec3 curr = verts[i];
                Vec3 leftPt, rightPt;

                float hw = getHalfWidth(i);

                int prevIdx = (i == 0) ? (isClosed ? numVerts - 1 : 0) : i - 1;
                int nextIdx = (i == numVerts - 1) ? (isClosed ? 0 : numVerts - 1) : i + 1;

                Vec3 prev = verts[prevIdx];
                Vec3 next = verts[nextIdx];

                if (!isClosed && i == 0) {
                    Vec3 normal = getNormal(curr, next);
                    leftPt = Vec3{curr.x + normal.x * hw, curr.y + normal.y * hw, curr.z};
                    rightPt = Vec3{curr.x - normal.x * hw, curr.y - normal.y * hw, curr.z};
                }
                else if (!isClosed && i == numVerts - 1) {
                    Vec3 normal = getNormal(prev, curr);
                    leftPt = Vec3{curr.x + normal.x * hw, curr.y + normal.y * hw, curr.z};
                    rightPt = Vec3{curr.x - normal.x * hw, curr.y - normal.y * hw, curr.z};
                }
                else {
                    Vec3 n1 = getNormal(prev, curr);
                    Vec3 n2 = getNormal(curr, next);
                    Vec3 avgNormal = normalize(Vec3{n1.x + n2.x, n1.y + n2.y, n1.z + n2.z});

                    Vec3 d1 = normalize(Vec3{curr.x - prev.x, curr.y - prev.y, curr.z - prev.z});
                    Vec3 d2 = normalize(Vec3{next.x - curr.x, next.y - curr.y, next.z - curr.z});
                    float cross = d1.x * d2.y - d1.y * d2.x;
                    bool turnsLeft = cross > 0;

                    float dotVal = dot(n1, avgNormal);
                    if (dotVal < 0.001f) dotVal = 0.001f;
                    float miterLength = 1.0f / dotVal;

                    if (miterLength <= miterLimit_) {
                        Vec3 miterNormal = Vec3{avgNormal.x * miterLength, avgNormal.y * miterLength, avgNormal.z * miterLength};
                        if (turnsLeft) {
                            leftPt = Vec3{curr.x + miterNormal.x * hw, curr.y + miterNormal.y * hw, curr.z};
                            rightPt = Vec3{curr.x - avgNormal.x * hw, curr.y - avgNormal.y * hw, curr.z};
                        } else {
                            leftPt = Vec3{curr.x + avgNormal.x * hw, curr.y + avgNormal.y * hw, curr.z};
                            rightPt = Vec3{curr.x - miterNormal.x * hw, curr.y - miterNormal.y * hw, curr.z};
                        }
                    } else {
                        leftPt = Vec3{curr.x + avgNormal.x * hw, curr.y + avgNormal.y * hw, curr.z};
                        rightPt = Vec3{curr.x - avgNormal.x * hw, curr.y - avgNormal.y * hw, curr.z};
                    }
                }

                leftPoints.push_back(leftPt);
                rightPoints.push_back(rightPt);
            }

            for (int i = 0; i < numVerts - 1; i++) {
                addTriangle(targetMesh, leftPoints[i], rightPoints[i], leftPoints[i + 1], strokeColor_);
                addTriangle(targetMesh, rightPoints[i], rightPoints[i + 1], leftPoints[i + 1], strokeColor_);
            }

            if (isClosed && numVerts >= 2) {
                int last = numVerts - 1;
                addTriangle(targetMesh, leftPoints[last], rightPoints[last], leftPoints[0], strokeColor_);
                addTriangle(targetMesh, rightPoints[last], rightPoints[0], leftPoints[0], strokeColor_);
            }
        }

        // Cap処理（開いた線の端）
        if (!isClosed) {
            float startHW = getHalfWidth(0);
            Vec3 startDir = normalize(Vec3{verts[1].x - verts[0].x, verts[1].y - verts[0].y, verts[1].z - verts[0].z});
            Vec3 startNormal = getNormal(verts[0], verts[1]);

            if (capType_ == CAP_SQUARE) {
                Vec3 left = Vec3{verts[0].x + startNormal.x * startHW, verts[0].y + startNormal.y * startHW, verts[0].z};
                Vec3 right = Vec3{verts[0].x - startNormal.x * startHW, verts[0].y - startNormal.y * startHW, verts[0].z};
                Vec3 extLeft = Vec3{left.x - startDir.x * startHW, left.y - startDir.y * startHW, left.z};
                Vec3 extRight = Vec3{right.x - startDir.x * startHW, right.y - startDir.y * startHW, right.z};
                addTriangle(targetMesh, left, extLeft, extRight, strokeColor_);
                addTriangle(targetMesh, left, extRight, right, strokeColor_);
            }
            else if (capType_ == CAP_ROUND) {
                int segments = std::max(8, (int)(startHW * 4));
                for (int j = 0; j < segments; j++) {
                    float a1 = PI * (float)j / segments;
                    float a2 = PI * (float)(j + 1) / segments;

                    Vec3 pt1 = Vec3{
                        verts[0].x - startNormal.x * std::cos(a1) * startHW - startDir.x * std::sin(a1) * startHW,
                        verts[0].y - startNormal.y * std::cos(a1) * startHW - startDir.y * std::sin(a1) * startHW,
                        verts[0].z
                    };
                    Vec3 pt2 = Vec3{
                        verts[0].x - startNormal.x * std::cos(a2) * startHW - startDir.x * std::sin(a2) * startHW,
                        verts[0].y - startNormal.y * std::cos(a2) * startHW - startDir.y * std::sin(a2) * startHW,
                        verts[0].z
                    };

                    addTriangle(targetMesh, verts[0], pt1, pt2, strokeColor_);
                }
            }

            // 終点
            int last = numVerts - 1;
            float endHW = getHalfWidth(last);
            Vec3 endDir = normalize(Vec3{verts[last].x - verts[last - 1].x, verts[last].y - verts[last - 1].y, verts[last].z - verts[last - 1].z});
            Vec3 endNormal = getNormal(verts[last - 1], verts[last]);

            if (capType_ == CAP_SQUARE) {
                Vec3 left = Vec3{verts[last].x + endNormal.x * endHW, verts[last].y + endNormal.y * endHW, verts[last].z};
                Vec3 right = Vec3{verts[last].x - endNormal.x * endHW, verts[last].y - endNormal.y * endHW, verts[last].z};
                Vec3 extLeft = Vec3{left.x + endDir.x * endHW, left.y + endDir.y * endHW, left.z};
                Vec3 extRight = Vec3{right.x + endDir.x * endHW, right.y + endDir.y * endHW, right.z};
                addTriangle(targetMesh, left, right, extRight, strokeColor_);
                addTriangle(targetMesh, left, extRight, extLeft, strokeColor_);
            }
            else if (capType_ == CAP_ROUND) {
                int segments = std::max(8, (int)(endHW * 4));
                for (int j = 0; j < segments; j++) {
                    float a1 = PI * (float)j / segments;
                    float a2 = PI * (float)(j + 1) / segments;

                    Vec3 pt1 = Vec3{
                        verts[last].x + endNormal.x * std::cos(a1) * endHW + endDir.x * std::sin(a1) * endHW,
                        verts[last].y + endNormal.y * std::cos(a1) * endHW + endDir.y * std::sin(a1) * endHW,
                        verts[last].z
                    };
                    Vec3 pt2 = Vec3{
                        verts[last].x + endNormal.x * std::cos(a2) * endHW + endDir.x * std::sin(a2) * endHW,
                        verts[last].y + endNormal.y * std::cos(a2) * endHW + endDir.y * std::sin(a2) * endHW,
                        verts[last].z
                    };

                    addTriangle(targetMesh, verts[last], pt1, pt2, strokeColor_);
                }
            }
        }
    }
};

} // namespace trussc
