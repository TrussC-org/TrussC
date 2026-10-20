#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <limits>
#include <utility>
#include <vector>
#include <filesystem>
#include <type_traits>
#include <tc/utils/tcLog.h>

namespace tcx::hap { class MovParser; struct HapBudgetTestAccess; }

namespace tcx::hap::detail {
// Bound (security): file-driven frame storage must remain finite before decode or copy. Overflow rejects the whole frame and is counted by its reader/decoder.
constexpr size_t movFrameBytes = 128u * 1024 * 1024;

inline bool movDimensions(uint32_t width, uint32_t height) {
    // Bound (security): recorded images obey the video 8192-axis and 128 MiB RGBA bounds. Overflow rejects the frame and is counted by its reader/decoder.
    return width && height && width <= 8192 && height <= 8192 && uint64_t(width) * height * 4 <= movFrameBytes;
}

template<class T> bool resizeMovBuffer(std::vector<T>& dst, size_t count) {
    if (count > dst.max_size()) return false;
    try {
        if (count > dst.capacity()) {
            // Exact replacement avoids implementation-dependent reserve slack.
            std::vector<T> next(count);
            dst.swap(next);
        } else dst.resize(count);
        return true;
    } catch (const std::exception&) { return false; }
}

// A read-only table view. The file owns the full index, including recordings
// longer than one million entries. Copies share page caches and their source
// stream; these views are used only on the player's reading thread.
struct MovTableSource {
    // Bound (cache): a reusable 8 KiB window buffers streamed file reads. Overflow replaces the read window.
    std::array<char, 8192> io{};
    std::ifstream file;
    bool good = true;
    explicit MovTableSource(const std::filesystem::path& path) {
        file.rdbuf()->pubsetbuf(io.data(), static_cast<std::streamsize>(io.size()));
        file.open(path, std::ios::binary);
    }
};

template<class T> class MovTable {
    std::shared_ptr<MovTableSource> source_;
    uint64_t offset_ = 0;
    size_t count_ = 0;
    uint32_t stride_ = 0;
    // Bound (security): only 256 entries per external table are resident; all tables and track descriptors share the parser's 64 MiB budget. Overflow pages without loss; I/O failures reject the affected track through the parser result.
    struct Page {
        size_t index = SIZE_MAX;
        std::array<uint8_t, 256 * 12> bytes{};
    };
    mutable std::shared_ptr<Page> cache_;
    uint64_t number(size_t at, size_t length) const {
        uint64_t value = 0;
        for (size_t i = 0; i < length; ++i) value = (value << 8) | cache_->bytes[at + i];
        return value;
    }
public:
    bool open(const std::shared_ptr<MovTableSource>& source, uint64_t offset, size_t count, uint32_t stride) {
        clear();
        if (!count) return true;
        source_ = source;
        if (!source_ || !source_->file) return false;
        offset_ = offset; count_ = count; stride_ = stride;
        return true;
    }
    void clear() { source_.reset(); cache_.reset(); count_ = 0; }
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool good() const { return !source_ || source_->good; }
    bool read(size_t index, T& value) const {
        if (index >= count_ || !good()) return false;
        if (!cache_) {
            try { cache_ = std::make_shared<Page>(); }
            catch (const std::exception&) {
                source_->good = false;
                trussc::logError("MovParser") << "MOV table page allocation failed";
                return false;
            }
        }
        const size_t page = index / 256;
        if (page != cache_->index) {
            auto& file = source_->file;
            file.clear();
            file.seekg(static_cast<std::streamoff>(offset_ + page * 256 * stride_));
            const size_t length = std::min(size_t(256), count_ - page * 256) * stride_;
            if (!file.read(reinterpret_cast<char*>(cache_->bytes.data()), static_cast<std::streamsize>(length))) {
                source_->good = false;
                trussc::logError("MovParser") << "MOV table page read failed";
                return false;
            }
            cache_->index = page;
        }
        const size_t at = (index % 256) * stride_;
        if constexpr (std::is_same_v<T, std::pair<uint32_t, uint32_t>>)
            value = {uint32_t(number(at, 4)), uint32_t(number(at + 4, 4))};
        else value = static_cast<T>(number(at, stride_));
        return true;
    }
};

// Lazily derive sample offsets from the paged on-disk tables. Sequential access
// is linear; sparse cursor checkpoints bound a seek to one interval. Their
// exact storage is admitted against the parser's metadata/entry budgets before
// allocation; very long tracks use a larger interval rather than losing samples.
template<class Sample> class MovSamples {
    MovTable<uint32_t> sizes_;
    MovTable<uint64_t> offsets_;
    MovTable<std::pair<uint32_t, uint32_t>> chunks_;
    uint32_t constant_ = 0, pcmBytes_ = 0;
    uint64_t limit_ = 0, pcmFrames_ = 0;
    size_t count_ = 0;
    double duration_ = 0;
    uint32_t timescale_ = 0, rate_ = 0;
    struct Cursor {
        size_t chunk = 0, stsc = 0, entries = 0;
        uint64_t raw = 0, left = 0, offset = 0;
        Sample sample{};
        bool failed = false;
    };
    struct Checkpoint {
        size_t chunk = 0, stsc = 0, entries = 0;
        uint64_t raw = 0, left = 0, offset = 0;
        explicit Checkpoint(const Cursor& c)
            : chunk(c.chunk), stsc(c.stsc), entries(c.entries), raw(c.raw), left(c.left), offset(c.offset) {}
        Checkpoint() = default;
        Cursor cursor() const {
            Cursor c;
            c.chunk = chunk; c.stsc = stsc; c.entries = entries;
            c.raw = raw; c.left = left; c.offset = offset;
            return c;
        }
    };
    std::vector<Checkpoint> checkpoints_;
    uint64_t checkpointInterval_ = 4096;
    mutable Cursor cursor_;
    friend struct ::tcx::hap::HapBudgetTestAccess;
    mutable uint64_t tableSteps_ = 0; // Per-instance test seam for derived table work.
    // Bound (cache): derived sample offsets retain one 256-entry page within the metadata budget. Overflow replaces.
    struct Page {
        size_t index = SIZE_MAX;
        std::array<Sample, 256> samples{};
    };
    mutable std::shared_ptr<Page> cache_;
    mutable Sample invalid_{};
    friend class ::tcx::hap::MovParser;
    uint64_t reservedEntries() const {
        return checkpoints_.size() + 256u * (bool(count_) + !sizes_.empty() + !offsets_.empty() + !chunks_.empty());
    }
    uint64_t reservedBytes() const {
        // Source pages remain live even when stsc derives no samples.
        const uint64_t tables = !sizes_.empty() + !offsets_.empty() + !chunks_.empty();
        return checkpoints_.capacity() * sizeof(Checkpoint) + tables * (256u * 12 + 64) + (count_ ? 256u * sizeof(Sample) + 64 : 0);
    }
    bool good() const { return sizes_.good() && offsets_.good() && chunks_.good(); }
    bool ready(Cursor& c) const {
        if (!good()) { c.failed = true; return false; }
        if (c.raw >= limit_) return false;
        while (!c.left) {
            if (c.chunk >= offsets_.size()) return false;
            std::pair<uint32_t, uint32_t> run;
            while (c.stsc + 1 < chunks_.size()) {
                if (!chunks_.read(c.stsc + 1, run)) { c.failed = true; return false; }
                if (c.chunk + 1 < run.first) break;
                ++c.stsc;
            }
            if (!chunks_.read(c.stsc, run) || !offsets_.read(c.chunk, c.offset)) {
                c.failed = true; return false;
            }
            c.left = std::min<uint64_t>(run.second, limit_ - c.raw);
            ++c.chunk;
        }
        return true;
    }
    bool skipTo(Cursor& c, size_t target) const {
        while (c.entries < target) {
            if (!constant_ || pcmBytes_) { if (!next(c)) return false; continue; }
            ++tableSteps_; // Count each constant-size chunk skip, including seek work.
            if (!ready(c)) return false;
            const uint64_t count = std::min<uint64_t>(c.left, target - c.entries);
            if (count > (UINT64_MAX - c.offset) / constant_) { c.failed = true; return false; }
            c.offset += count * constant_; c.raw += count; c.left -= count;
            c.entries += size_t(count);
        }
        return true;
    }
    bool next(Cursor& c) const {
        ++tableSteps_;
        if (!ready(c)) return false;
        const uint64_t frames = pcmBytes_ ? std::min<uint64_t>(c.left, movFrameBytes / pcmBytes_) : 1;
        if (!frames) return false;
        Sample sample;
        sample.offset = c.offset;
        if (pcmBytes_) sample.size = uint32_t(frames * pcmBytes_);
        else if (constant_) sample.size = constant_;
        else if (!sizes_.read(size_t(c.raw), sample.size)) { c.failed = true; return false; }
        if (sample.size > UINT64_MAX - c.offset) { c.failed = true; return false; }
        const double seconds = pcmBytes_ && rate_ ? double(c.raw) / rate_ :
                               (count_ ? double(c.entries) * duration_ / count_ : 0);
        sample.timestamp = seconds;
        const double ticks = pcmBytes_ && rate_ ? double(frames) * timescale_ / rate_ :
                             (count_ ? duration_ * timescale_ / count_ : 0);
        sample.duration = ticks >= UINT32_MAX ? UINT32_MAX : uint32_t(ticks);
        c.offset += sample.size; c.raw += frames; c.left -= frames; ++c.entries;
        c.sample = sample;
        return true;
    }
public:
    bool open(const MovTable<uint32_t>& sizes, uint32_t constant, uint64_t limit,
              const MovTable<uint64_t>& offsets,
              const MovTable<std::pair<uint32_t, uint32_t>>& chunks,
              uint32_t pcmBytes, uint64_t fileSize, uint64_t byteBudget, uint64_t entryBudget) {
        sizes_ = sizes; constant_ = constant; limit_ = limit;
        offsets_ = offsets; chunks_ = chunks; pcmBytes_ = pcmBytes;
        count_ = 0; cursor_ = {}; pcmFrames_ = 0; cache_.reset();
        std::vector<Checkpoint>().swap(checkpoints_);
        if (chunks.empty() || offsets.empty() || !limit) return true;
        // A constant-size track can describe billions of samples in one
        // chunk. Validate/count these arithmetically, without walking a count
        // unrelated to the amount of table data in the file.
        if (constant_ && !pcmBytes_) {
            size_t stsc = 0;
            uint64_t placed = 0, bytes = 0;
            for (size_t chunk = 0; chunk < offsets_.size() && placed < limit_; ++chunk) {
                std::pair<uint32_t, uint32_t> run;
                while (stsc + 1 < chunks_.size()) {
                    if (!chunks_.read(stsc + 1, run)) return false;
                    if (chunk + 1 < run.first) break;
                    ++stsc;
                }
                uint64_t offset;
                if (!chunks_.read(stsc, run) || !offsets_.read(chunk, offset)) return false;
                const uint64_t count = std::min<uint64_t>(run.second, limit_ - placed);
                if (count > (UINT64_MAX - offset) / constant_) return false;
                if (offset <= fileSize) {
                    bytes += std::min<uint64_t>(count, (fileSize - offset) / constant_) * constant_;
                    if (bytes > fileSize) return false;
                }
                placed += count;
            }
            count_ = size_t(placed);
            if (!good()) return false;
            // One chunk can always be skipped arithmetically in one step.
            if (offsets_.size() == 1) return true;
        } else {
            Cursor walk;
            uint64_t bytes = 0;
            while (next(walk)) {
                const auto& sample = walk.sample;
                if (sample.size <= fileSize && sample.offset <= fileSize - sample.size) {
                    bytes += sample.size;
                    if (bytes > fileSize) return false;
                }
                ++count_;
            }
            pcmFrames_ = walk.raw;
            if (walk.failed || !good()) return false;
        }
        if (!count_) return true;
        const auto baseBytes = reservedBytes(), baseEntries = reservedEntries();
        if (byteBudget <= baseBytes || entryBudget <= baseEntries) return false;
        const uint64_t maxPoints = std::min((byteBudget - baseBytes) / sizeof(Checkpoint), entryBudget - baseEntries);
        if (!maxPoints) return false;
        // Bound (cache): sparse checkpoints share 64 MiB / 1,000,000 resident entries with pages and tracks. Overflow replaces checkpoints with a wider interval.
        const uint64_t neededInterval = (uint64_t(count_) + maxPoints - 1) / maxPoints;
        checkpointInterval_ = std::max<uint64_t>(4096, ((neededInterval + 255) / 256) * 256);
        const size_t points = size_t(1 + (count_ - 1) / checkpointInterval_);
        // Count/stride and remaining metadata are checked above before the exact allocation.
        std::vector<Checkpoint> checkpoints(points);
        Cursor checkpointWalk;
        for (size_t i = 0; i < points; ++i) {
            if (!skipTo(checkpointWalk, size_t(i * checkpointInterval_))) return false;
            checkpoints[i] = Checkpoint(checkpointWalk);
        }
        checkpoints_.swap(checkpoints);
        return true;
    }
    void setTiming(double duration, uint32_t timescale, uint32_t rate) {
        duration_ = duration; timescale_ = timescale; rate_ = rate; cursor_ = {}; cache_.reset();
    }
    uint64_t pcmFrames() const { return pcmFrames_; }
    size_t size() const { return count_; }
    bool empty() const { return !count_; }
    const Sample& operator[](size_t index) const {
        if (index >= count_ || !good()) return invalid_;
        if (!cache_) {
            try { cache_ = std::make_shared<Page>(); }
            catch (const std::exception&) {
                trussc::logError("MovParser") << "MOV sample page allocation failed";
                return invalid_;
            }
        }
        const size_t pageStart = (index / cache_->samples.size()) * cache_->samples.size();
        if (pageStart != cache_->index) {
            if (!checkpoints_.empty()) {
                const auto& checkpoint = checkpoints_[pageStart / checkpointInterval_];
                if (cursor_.entries > pageStart || cursor_.entries < checkpoint.entries)
                    cursor_ = checkpoint.cursor();
            } else if (cursor_.entries > pageStart) cursor_ = {};
            if (!skipTo(cursor_, pageStart)) return invalid_;
            const size_t length = std::min(cache_->samples.size(), count_ - pageStart);
            for (size_t i = 0; i < length; ++i) {
                if (!next(cursor_)) return invalid_;
                cache_->samples[i] = cursor_.sample;
            }
            cache_->index = pageStart;
        }
        return cache_->samples[index - pageStart];
    }
    struct Iterator {
        const MovSamples* owner; size_t index;
        const Sample& operator*() const { return (*owner)[index]; }
        Iterator& operator++() { ++index; return *this; }
        bool operator!=(const Iterator& other) const { return index != other.index; }
    };
    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, count_}; }
    const Sample& front() const { return (*this)[0]; }
    const Sample& back() const { return (*this)[count_ - 1]; }
};
} // namespace tcx::hap::detail
