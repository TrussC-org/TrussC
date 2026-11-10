// #1356: stable lighting scenes for before/after captures on the same backend.
Mesh normalSphere_, normalBox_;
Material normalMaterial_;
Light normalLight_;
Texture normalMap_;

void setupNormalScenes() {
    Pixels texel; texel.allocate(1, 1, 4);
    texel.setColor(0, 0, Color(0.8f, 0.6f, 0.9f));
    normalMap_.allocate(texel);
    normalSphere_ = createSphere(28, 24);
    normalBox_ = createBox(44, 44, 44);
    normalMaterial_.setBaseColor(0.6f, 0.7f, 0.8f).setRoughness(0.8f);
    normalLight_.setDirectional(-0.4f, 0.3f, -1);
    normalLight_.setAmbient(0.1f, 0.1f, 0.1f);
    normalLight_.setSpecular(0, 0, 0);
}
bool drawNormalScene(float w, float h) {
    const bool cpu = name().starts_with("cpuNormal");
    if (!cpu && !name().starts_with("pbrNormal")) return false;
    clearLights(); addLight(normalLight_);
    normalMaterial_.setNormalMap(name().ends_with("Tangent") ? &normalMap_ : nullptr);
    setMaterial(normalMaterial_);
    setColor(1, 1, 1);
    enableDepthTest();
    pushMatrix();
    translate(w / 2, h / 2, 0);
    if (name().ends_with("Rotated")) rotateY(0.6f);
    if (name().ends_with("Mirror")) scale(-1, 1, 1);
    else if (name().ends_with("NonUniform")) scale(2, 1, 1);
    else if (name().ends_with("Rotated") || name().ends_with("Tangent")) scale(2, 1, 0.5f);
    else scale(2, 2, 2);
    const Mesh& mesh = name().ends_with("Box") ? normalBox_ : normalSphere_;
    if (cpu) mesh.drawWithLighting();
    else mesh.draw();
    popMatrix();
    disableDepthTest();
    clearMaterial();
    clearLights(); addLight(light_);
    return true;
}
