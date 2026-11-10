// Vertex-time style fixtures (#1381), shared by screen and FBO captures.
bool drawStrokeScene() {
    if (!name().starts_with("stroke")) return false;
    auto style = scopedStyle();
    clear(1, 1, 1, 1);
    setColor(0.08f, 0.2f, 0.35f);
    setStrokeWeight(24);
    setStrokeCap(StrokeCap::Butt);
    setStrokeJoin(StrokeJoin::Miter);
    if (is("strokeRoundStartSquareEnd")) {
        beginStroke();
        setStrokeCap(StrokeCap::Round); vertex(48, 96);
        setStrokeCap(StrokeCap::Square); vertex(208, 96);
        setStrokeCap(StrokeCap::Butt); // Changes after the last vertex do not apply.
        endStroke();
    } else if (is("strokeAlternatingMiterRoundBevel")) {
        beginStroke(); vertex(28, 144);
        setStrokeJoin(StrokeJoin::Miter); vertex(78, 48);
        setStrokeJoin(StrokeJoin::Round); vertex(128, 144);
        setStrokeJoin(StrokeJoin::Bevel); vertex(178, 48);
        vertex(228, 144);
        setStrokeJoin(StrokeJoin::Round);
        endStroke();
    } else if (is("strokeClosedMixedJoins")) {
        beginStroke();
        setStrokeCap(StrokeCap::Round);
        setStrokeJoin(StrokeJoin::Round); vertex(56, 48);
        setStrokeJoin(StrokeJoin::Miter); vertex(200, 48);
        setStrokeJoin(StrokeJoin::Bevel); vertex(200, 144);
        setStrokeJoin(StrokeJoin::Round); vertex(56, 144);
        setStrokeCap(StrokeCap::Square);
        setStrokeJoin(StrokeJoin::Bevel); vertex(56, 48); // Duplicate closing point.
        endStroke(true);
    } else if (is("strokeMidCapChangeEndOnly")) {
        beginStroke(); vertex(48, 96);
        setStrokeCap(StrokeCap::Round); vertex(128, 96);
        setStrokeCap(StrokeCap::Square); vertex(208, 96);
        endStroke();
    } else if (is("strokeConstantStyles")) {
        setStrokeWeight(10);
        for (int cap = 0; cap < 3; ++cap) for (int join = 0; join < 3; ++join) {
            setStrokeCap(static_cast<StrokeCap>(cap));
            setStrokeJoin(static_cast<StrokeJoin>(join));
            beginStroke();
            vertex(20 + join * 80, 44 + cap * 56);
            vertex(44 + join * 80, 20 + cap * 56);
            vertex(68 + join * 80, 44 + cap * 56);
            endStroke();
        }
    }
    return true;
}
