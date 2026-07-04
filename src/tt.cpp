#include "tt.h"
#include <vector>
#include <cstring>

namespace TT {

    static std::vector<Entry> table;
    static size_t num_entries = 0;

    void init(size_t mb) {
        if (mb < 1) mb = 1;
        num_entries = (mb * 1024 * 1024) / sizeof(Entry);
        table.assign(num_entries, Entry{});
    }

    void clear() {
        std::fill(table.begin(), table.end(), Entry{});
    }

    static inline size_t index_of(uint64_t key) {

        return static_cast<size_t>((static_cast<unsigned __int128>(key) * num_entries) >> 64);
    }

    void store(uint64_t key, int depth, int score, Flag flag, Move best_move) {
        if (num_entries == 0) return;
        Entry& e = table[index_of(key)];

        if (e.key == key && e.depth > depth + 2 && flag != EXACT)
            return;

        e.key = key;
        e.depth = static_cast<uint8_t>(depth < 0 ? 0 : depth);
        e.score = static_cast<int16_t>(score);
        e.flag = flag;
        if (!best_move.is_none() || e.key != key)
            e.move = best_move.raw();
    }

    const Entry* probe(uint64_t key) {
        if (num_entries == 0) return nullptr;
        const Entry& e = table[index_of(key)];
        return (e.key == key && e.flag != NONE) ? &e : nullptr;
    }

    int hashfull() {
        int cnt = 0;
        size_t sample = num_entries < 1000 ? num_entries : 1000;
        for (size_t i = 0; i < sample; ++i)
            if (table[i].flag != NONE) cnt++;
        return static_cast<int>(cnt * 1000 / (sample ? sample : 1));
    }
}
