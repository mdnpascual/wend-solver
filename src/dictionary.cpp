#include "dictionary.h"
#include <fstream>
#include <algorithm>
#include <cctype>

Dictionary::Dictionary() {
    root_ = new TrieNode();
}

Dictionary::~Dictionary() {
    destroy(root_);
}

void Dictionary::destroy(TrieNode* node) {
    if (!node) return;
    for (auto* child : node->children)
        destroy(child);
    delete node;
}

void Dictionary::insert(std::string_view word) {
    TrieNode* cur = root_;
    for (char ch : word) {
        int idx = ch - 'a';
        if (idx < 0 || idx > 25) return; // safety guard
        if (!cur->children[idx])
            cur->children[idx] = new TrieNode();
        cur = cur->children[idx];
    }
    cur->is_terminal = true;
}

int Dictionary::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return -1;

    int count = 0;
    std::string word;
    word.reserve(32);

    while (std::getline(file, word)) {
        // Trim trailing whitespace / CR
        while (!word.empty() && (word.back() == '\r' || word.back() == '\n' || word.back() == ' '))
            word.pop_back();
        if (word.empty()) continue;

        // Normalise to lowercase, skip if non-alpha
        bool valid = true;
        for (char& ch : word) {
            if (!std::isalpha((unsigned char)ch)) { valid = false; break; }
            ch = (char)std::tolower((unsigned char)ch);
        }
        if (!valid) continue;

        insert(word);
        ++count;
    }

    word_count_ = count;
    loaded_     = (count > 0);
    return count;
}

bool Dictionary::is_prefix(std::string_view s) const {
    const TrieNode* cur = root_;
    for (char ch : s) {
        int idx = (char)std::tolower((unsigned char)ch) - 'a';
        if (idx < 0 || idx > 25 || !cur->children[idx]) return false;
        cur = cur->children[idx];
    }
    return true; // reached this node, so it is a valid prefix
}

bool Dictionary::is_word(std::string_view s) const {
    const TrieNode* cur = root_;
    for (char ch : s) {
        int idx = (char)std::tolower((unsigned char)ch) - 'a';
        if (idx < 0 || idx > 25 || !cur->children[idx]) return false;
        cur = cur->children[idx];
    }
    return cur->is_terminal;
}
