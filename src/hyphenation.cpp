#include "hyphenation.h"
#include "hyphenation_data.h"
#include <string.h>

static inline bool is_ascii_alpha(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline uint8_t lower_ascii(char c) {
  if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A' + 'a');
  return (uint8_t)c;
}

static inline uint8_t nibble_at(uint16_t nibbleIdx) {
  uint8_t b = HY_VALS_PACKED[nibbleIdx >> 1];
  return (nibbleIdx & 1) ? (b & 0x0F) : (b >> 4);
}

static int16_t trie_step(uint16_t nodeIdx, uint8_t ch) {
  const HyNode& n = HY_NODES[nodeIdx];
  uint16_t a = n.edge_start;
  uint16_t b = (uint16_t)(a + n.edge_count);
  for (uint16_t i = a; i < b; i++) {
    const HyEdge& e = HY_EDGES[i];
    if (e.ch == ch) return (int16_t)e.next;
    if (e.ch > ch) break;
  }
  return -1;
}

int hyphenation_collect_splits(
  const char* word,
  int         len,
  uint8_t*    outSplits,
  int         outCap,
  uint8_t     leftMin,
  uint8_t     rightMin
) {
  if (!word || len < 4 || outCap <= 0) return 0;
  if ((int)leftMin + (int)rightMin >= len) return 0;

  for (int i = 0; i < len; i++) {
    if (!is_ascii_alpha(word[i])) return 0;
  }

  // ".word."
  char pointed[66];
  if (len + 2 >= (int)sizeof(pointed)) return 0;
  pointed[0] = '.';
  for (int i = 0; i < len; i++) pointed[i + 1] = (char)lower_ascii(word[i]);
  pointed[len + 1] = '.';
  int plen = len + 2;

  // Values are 0..9; uint8_t is enough.
  uint8_t refs[67] = {0};

  for (int i = 0; i < plen - 1; i++) {
    int16_t node = 0;
    int stop = i + HY_MAX_PATTERN_LEN;
    if (stop > plen) stop = plen;

    for (int j = i; j < stop; j++) {
      node = trie_step((uint16_t)node, (uint8_t)pointed[j]);
      if (node < 0) break;

      const HyNode& tn = HY_NODES[(uint16_t)node];
      if (tn.pat_index == 0xFFFF) continue;

      const HyPat& p = HY_PATS[tn.pat_index];
      int base = i + p.offset;
      for (int k = 0; k < p.vals_len; k++) {
        int idx = base + k;
        if (idx < 0 || idx >= (int)sizeof(refs)) continue;
        uint8_t v = nibble_at((uint16_t)(p.vals_start_nibble + k));
        if (v > refs[idx]) refs[idx] = v;
      }
    }
  }

  int n = 0;
  for (int i = 0; i <= plen; i++) {
    if ((refs[i] & 1) == 0) continue;
    int split = i - 1; // pyphen-compatible mapping
    if (split < leftMin) continue;
    if (split > len - rightMin) continue;
    if (n < outCap) outSplits[n] = (uint8_t)split;
    n++;
  }

  return (n < outCap) ? n : outCap;
}

