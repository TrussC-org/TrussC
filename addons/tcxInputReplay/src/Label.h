// Shows the selected recording's filename in the toolbar.
// Hovering reveals the full path without widening the toolbar.
#pragma once
#include <TrussC.h>
namespace tcx::inputreplay {
class Label : public tc::RectNode {
public:
    Label() { enableEvents(); setSize(220, 20); }
    void setPath(const std::filesystem::path& path) { path_ = path; }
    void draw() override {
        auto name = tc::pathToUtf8(path_.filename());
        if (name.empty()) name = "No file";
        if (tc::getBitmapStringWidth(name) > getWidth() - 8) {
            do {
                auto end = name.size() - 1;
                while (end > 0 && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80) --end;
                name.erase(end);
            } while (!name.empty() && tc::getBitmapStringWidth(name + "...") > getWidth() - 8);
            name += "...";
        }
        tc::setColor(1.0f);
        tc::drawBitmapString(name, 4, getHeight() / 2, tc::Direction::Left, tc::Direction::Center);
        if (isMouseOver() && !path_.empty()) {
            const auto full = tc::pathToUtf8(path_);
            const float width = tc::getBitmapStringWidth(full) + 12;
            const float x = (std::min)(0.0f, float(tc::getWidth()) - localToGlobal(tc::Vec3(0, 0, 0)).x - width - 8);
            tc::setColor(0.12f);
            tc::drawRectRounded(x, 30, width, 20, 4);
            tc::setColor(1.0f);
            tc::drawBitmapString(full, x + 6, 40, tc::Direction::Left, tc::Direction::Center);
        }
    }
private:
    std::filesystem::path path_;
};
} // namespace tcx::inputreplay
