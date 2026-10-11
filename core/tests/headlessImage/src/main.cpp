// Image CPU operations must work without sg_setup(), both before app startup
// and inside runHeadlessApp (#651).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace std;
using namespace tc;

namespace {

int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

bool emptyTexture(const Texture& texture) {
    return !texture.isAllocated() && texture.getImage().id == 0 &&
           texture.getView().id == 0 && texture.getSampler().id == 0 &&
           texture.getAttachmentView().id == 0;
}

void checkTexturePaths() {
    Pixels pixels;
    pixels.allocate(4, 4, 4);
    Texture texture;
    texture.allocate(4, 4, 4, TextureUsage::Dynamic);
    texture.loadData(pixels);
    texture.loadData(pixels.getData(), 4, 4, 4);
    check("channel allocation and updates leave empty handles", emptyTexture(texture));

    texture.allocate(4, 4, TextureFormat::RGBA8, TextureUsage::RenderTarget, 1, 3);
    check("explicit-format allocation leaves empty mip views",
          emptyTexture(texture) && texture.getAttachmentViewForMip(1).id == 0 &&
          texture.getViewForMip(1).id == 0);

    texture.allocate(pixels, TextureUsage::Dynamic, true);
    texture.loadData(pixels);
    texture.setFilter(TextureFilter::Nearest);
    texture.setWrap(TextureWrap::Repeat);
    check("mipmapped allocation, update and sampler settings stay CPU-only",
          emptyTexture(texture));

    unsigned char block[8] = {};
    texture.allocateCompressed(4, 4, SG_PIXELFORMAT_BC1_RGBA, block, sizeof(block));
    texture.updateCompressed(block, sizeof(block));
    check("compressed allocation and update leave empty handles", emptyTexture(texture));

    texture.allocateCubemap(1, TextureFormat::RGBA8, TextureUsage::Dynamic);
    unsigned char faces[24] = {};
    const void* faceData[6] = {faces, faces + 4, faces + 8, faces + 12, faces + 16, faces + 20};
    texture.loadCubemapData(faceData, 4);
    check("cubemap allocation, uploads and lazy view creation stay empty",
          emptyTexture(texture) && texture.getCubemapFaceAttachmentView(0, 0).id == 0);
}

void roundTrip(bool mipmaps) {
    const auto suffix = chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() /
        ("tc_headlessImage_" + to_string(suffix));
    fs::create_directories(dir);
    const fs::path source = dir / "source.png";
    const fs::path edited = dir / "edited.png";

    Image image;
    image.allocate(2, 2, 4, mipmaps);
    const unsigned char original[] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255
    };
    check("allocate keeps CPU pixels and an empty texture",
          image.isAllocated() && image.getWidth() == 2 && image.getHeight() == 2 &&
          image.getChannels() == 4 && emptyTexture(image.getTexture()));
    if (image.isAllocated()) {
        std::memcpy(image.getPixels().getData(), original, sizeof(original));
    }
    image.setDirty();
    image.update();
    image.draw(0, 0);
    image.draw(0, 0, 10, 10);
    check("save allocated CPU pixels", image.save(source));

    Image loaded;
    const bool loadedOk = static_cast<bool>(loaded.load(source, mipmaps));
    check("load succeeds with an empty texture", loadedOk && emptyTexture(loaded.getTexture()));
    if (loadedOk) {
        check("loaded pixels match the saved image",
              loaded.getPixels().getTotalBytes() == sizeof(original) &&
              std::memcmp(loaded.getPixels().getData(), original, sizeof(original)) == 0);
        loaded.setColor(0, 0, Color(0, 0, 0, 1));
        loaded.getPixels().setColor(1, 1, Color(1, 0, 1, 1));
        loaded.setDirty();
        loaded.update();
        loaded.draw(0, 0);
        check("save edited CPU pixels", loaded.save(edited));

        Image reloaded;
        const bool reloadedOk = static_cast<bool>(reloaded.load(edited, mipmaps));
        const unsigned char expected[] = {
            0, 0, 0, 255, 0, 255, 0, 255,
            0, 0, 255, 255, 255, 0, 255, 255
        };
        check("load/edit/save preserves the exact edited pixels",
              reloadedOk && reloaded.getPixels().getTotalBytes() == sizeof(expected) &&
              std::memcmp(reloaded.getPixels().getData(), expected, sizeof(expected)) == 0 &&
              emptyTexture(reloaded.getTexture()));

        ifstream file(source, ios::binary);
        const vector<unsigned char> bytes((istreambuf_iterator<char>(file)), {});
        Image memory;
        check("loadFromMemory also keeps CPU pixels without a texture",
              memory.loadFromMemory(bytes.data(), static_cast<int>(bytes.size()), mipmaps) &&
              memory.getPixels().getTotalBytes() == sizeof(original) &&
              std::memcmp(memory.getPixels().getData(), original, sizeof(original)) == 0 &&
              emptyTexture(memory.getTexture()));
    }
    check("image work never initializes sokol_gfx", !sg_isvalid());
    fs::remove_all(dir);
}

class HeadlessImageApp : public App {
public:
    void setup() override {
        check("headless setup has no graphics context", headless::isActive() && !sg_isvalid());
        checkTexturePaths();
        roundTrip(false);
        roundTrip(true);
    }

    void update() override { requestExit(); }
};

} // namespace

TC_CORE_TEST_MAIN() {
    check("before setup: neither headless nor graphics is active",
          !headless::isActive() && !sg_isvalid());
    checkTexturePaths();
    roundTrip(false);
    roundTrip(true);
    runHeadlessApp<HeadlessImageApp>();
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
