#pragma once
#include <Arduino.h>

void     storage_save_position(const char* path, int page, int totalPages);
bool     storage_load_position(char* pathOut, size_t pathLen, int* pageOut);
int      storage_get_book_progress(const char* path); // 0..100, or -1 if unknown
int      storage_get_book_page(const char* path);     // last page for this book, 0 if unknown
uint32_t storage_get_book_seq(const char* path);      // recency counter, higher = more recent
void     storage_clear_position();
