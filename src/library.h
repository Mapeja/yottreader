#pragma once
#include <Arduino.h>

#define LIBRARY_MAX_BOOKS 32
#define LIBRARY_PATH_LEN  64
#define LIBRARY_STR_LEN   48

struct BookEntry {
  char     path[LIBRARY_PATH_LEN];
  char     title[LIBRARY_STR_LEN];
  char     author[LIBRARY_STR_LEN];
  uint32_t lastSeq; // recency counter from storage; higher = more recently read
};

void library_scan();
void library_resort();   // re-reads seq values and re-sorts without filesystem scan
int  library_count();
const BookEntry* library_get(int index);
