#pragma once
#include <string>
#include <string_view>
#include <array>

struct TrieNode {
    std::array<TrieNode*, 26> children{};
    bool is_terminal = false;

    TrieNode() { children.fill(nullptr); }
};

class Dictionary {
public:
    Dictionary();
    ~Dictionary();

    // Load words from file (one word per line, letters only).
    // Returns number of words loaded, or -1 on failure.
    int load(const std::string& path);

    // Returns true if `s` is a prefix of any word in the dictionary.
    bool is_prefix(std::string_view s) const;

    // Returns true if `s` is an exact word in the dictionary.
    bool is_word(std::string_view s) const;

    bool is_loaded() const { return loaded_; }
    int  word_count() const { return word_count_; }

private:
    TrieNode* root_  = nullptr;
    bool      loaded_ = false;
    int       word_count_ = 0;

    void insert(std::string_view word);
    void destroy(TrieNode* node);
};
