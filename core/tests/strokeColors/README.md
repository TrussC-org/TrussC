# Stroke vertex colors (#915)

`allCoreTests strokeColors` checks captured colors/widths and direct, uniformly
colored StrokeMesh input without a window.

`allCoreTests strokeColors --gpu [output-directory]` reads back pixels for all
nine join/cap combinations. It checks vertex colors, solid joint/cap colors,
segment interpolation, and last-to-first interpolation for closed strokes.
It also checks the uniform-color drawStroke and Path::drawStroke paths.

With an output directory, it writes multicolor, closed, single-color, and
single-color API PNGs. For a before/after comparison, record the same fixture
against unchanged production code using `--gpu <directory> --record-only`,
then run it normally after rebuilding the change. Record-only mode prints
samples without asserting the new color behavior. Single-color PNGs should
match exactly between the two runs.

Linux display runs use private Xvfb and Mesa software rendering. Coordinates
and frame completion determine the assertions; there are no timing checks.
