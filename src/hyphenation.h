#pragma once
#include <Arduino.h>

// Collect valid Knuth-Liang hyphen split positions for an ASCII word.
// Returns split count; each split is an index in [1, len-1], meaning:
//   left part = word[0:split], right part = word[split:len]
int hyphenation_collect_splits(
  const char* word,
  int         len,
  uint8_t*    outSplits,
  int         outCap,
  uint8_t     leftMin = 2,
  uint8_t     rightMin = 2
);

