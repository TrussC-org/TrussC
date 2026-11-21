#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <array>
#include <iterator>
#include <limits>
#include <memory>
#include <tuple>

namespace trussc::internal {
// Immutable AVL paths share stable listener records. Writers copy O(log n)
// nodes; a notify owns one root and traverses without allocating or leasing
// each callable. Ordering is priority, then registration identity (FIFO).
template<class Entry> struct EventTree {
    using Tree = std::shared_ptr<const EventTree>;
    std::shared_ptr<Entry> entry;
    Tree left, right;
    int height;
    static int depth(const Tree& tree) { return tree ? tree->height : 0; }
    static auto key(const Entry& value) { return std::tuple(value.priority, value.id); }
    static Tree node(std::shared_ptr<Entry> value, Tree lhs = {}, Tree rhs = {}) {
        const int height = 1 + (std::max)(depth(lhs), depth(rhs));
        return std::make_shared<EventTree>(EventTree{std::move(value), std::move(lhs), std::move(rhs), height});
    }
    static Tree balance(std::shared_ptr<Entry> value, Tree lhs, Tree rhs) {
        if (depth(lhs) > depth(rhs) + 1) {
            if (depth(lhs->right) > depth(lhs->left)) {
                const auto& pivot = lhs->right;
                lhs = node(pivot->entry, node(lhs->entry, lhs->left, pivot->left), pivot->right);
            }
            return node(lhs->entry, lhs->left, node(std::move(value), lhs->right, std::move(rhs)));
        }
        if (depth(rhs) > depth(lhs) + 1) {
            if (depth(rhs->left) > depth(rhs->right)) {
                const auto& pivot = rhs->left;
                rhs = node(pivot->entry, pivot->left, node(rhs->entry, pivot->right, rhs->right));
            }
            return node(rhs->entry, node(std::move(value), std::move(lhs), rhs->left), rhs->right);
        }
        return node(std::move(value), std::move(lhs), std::move(rhs));
    }
    static Tree insert(const Tree& root, const std::shared_ptr<Entry>& value) {
        if (!root) return node(value);
        if (key(*value) < key(*root->entry))
            return balance(root->entry, insert(root->left, value), root->right);
        return balance(root->entry, root->left, insert(root->right, value));
    }
    static Tree erase(const Tree& root, const Entry& value) {
        if (!root) return {};
        if (key(value) < key(*root->entry))
            return balance(root->entry, erase(root->left, value), root->right);
        if (key(value) > key(*root->entry))
            return balance(root->entry, root->left, erase(root->right, value));
        if (!root->left) return root->right;
        if (!root->right) return root->left;
        auto* successor = root->right.get();
        while (successor->left) successor = successor->left.get();
        return balance(successor->entry, root->left, erase(root->right, *successor->entry));
    }
    // Only needed after an allocation-failed removal left a dead record.
    static Tree sweep(const Tree& root) {
        Tree result;
        if (root) for (auto& value : *root)
            if (value.alive->load(std::memory_order_acquire)) result = insert(result, value.shared_from_this());
        return result;
    }
    class Iterator {
        // Bound (representation): AVL height for 64-bit listener identities is below the 128-entry traversal capacity (Fibonacci node bound).
        std::array<const EventTree*, 2 * std::numeric_limits<uint64_t>::digits> path_{};
        size_t count_ = 0;
        void descend(const EventTree* tree) {
            while (tree) { path_[count_++] = tree; tree = tree->left.get(); }
        }
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Entry;
        using difference_type = std::ptrdiff_t;
        using pointer = Entry*;
        using reference = Entry&;
        Iterator() = default;
        explicit Iterator(const EventTree* tree) { descend(tree); }
        Entry& operator*() const { return *path_[count_ - 1]->entry; }
        Entry* operator->() const { return &**this; }
        Iterator& operator++() { const auto* tree = path_[--count_]; descend(tree->right.get()); return *this; }
        Iterator operator++(int) { auto previous = *this; ++*this; return previous; }
        bool operator==(const Iterator& other) const {
            return (count_ ? path_[count_ - 1] : nullptr) == (other.count_ ? other.path_[other.count_ - 1] : nullptr);
        }
    };
    Iterator begin() const { return Iterator(this); }
    Iterator end() const { return {}; }
};
} // namespace trussc::internal
