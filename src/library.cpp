#include "library.h"
#include "storage.h"
#include <LittleFS.h>

static BookEntry books[LIBRARY_MAX_BOOKS];
static int       nBooks = 0;

static void parseMeta(const char* path, BookEntry& e) {
  strncpy(e.path, path, LIBRARY_PATH_LEN - 1);
  e.path[LIBRARY_PATH_LEN - 1] = 0;
  e.title[0]  = 0;
  e.author[0] = 0;

  File f = LittleFS.open(path, "r");
  if (!f) return;

  bool inTitle = false, inAuthor = false;
  bool titleDone = false, authorDone = false;

  while (f.available() && !(titleDone && authorDone)) {
    String line = f.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length() - 1);

    if (line == "::TITLE::") {
      inTitle = true; inAuthor = false; continue;
    }
    if (line == "::AUTHOR::") {
      inAuthor = true; inTitle = false; continue;
    }
    if (line.startsWith("::")) {
      inTitle = false; inAuthor = false; continue;
    }

    if (inTitle && !titleDone && line.length() > 0) {
      strncpy(e.title, line.c_str(), LIBRARY_STR_LEN - 1);
      e.title[LIBRARY_STR_LEN - 1] = 0;
      titleDone = true;
    }
    if (inAuthor && !authorDone && line.length() > 0) {
      strncpy(e.author, line.c_str(), LIBRARY_STR_LEN - 1);
      e.author[LIBRARY_STR_LEN - 1] = 0;
      authorDone = true;
    }
  }
  f.close();

  if (e.title[0] == 0) {
    // Fall back to filename without path and extension
    const char* slash = strrchr(path, '/');
    const char* name  = slash ? slash + 1 : path;
    strncpy(e.title, name, LIBRARY_STR_LEN - 1);
    e.title[LIBRARY_STR_LEN - 1] = 0;
    char* dot = strrchr(e.title, '.');
    if (dot) *dot = 0;
  }
}

void library_scan() {
  nBooks = 0;
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) return;

  File entry = root.openNextFile();
  while (entry && nBooks < LIBRARY_MAX_BOOKS) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      // f.name() returns path with or without leading '/' depending on core version
      while (name.startsWith("/")) name = name.substring(1);
      if (name.endsWith(".book")) {
        String fullPath = "/" + name;
        parseMeta(fullPath.c_str(), books[nBooks]);
        books[nBooks].lastSeq = storage_get_book_seq(books[nBooks].path);
        nBooks++;
      }
    }
    entry = root.openNextFile();
  }

  // Insertion sort: most recently read (highest lastSeq) first.
  for (int i = 1; i < nBooks; i++) {
    BookEntry tmp = books[i];
    int j = i - 1;
    while (j >= 0 && books[j].lastSeq < tmp.lastSeq) {
      books[j + 1] = books[j];
      j--;
    }
    books[j + 1] = tmp;
  }
}

void library_resort() {
  for (int i = 0; i < nBooks; i++)
    books[i].lastSeq = storage_get_book_seq(books[i].path);
  for (int i = 1; i < nBooks; i++) {
    BookEntry tmp = books[i];
    int j = i - 1;
    while (j >= 0 && books[j].lastSeq < tmp.lastSeq) {
      books[j + 1] = books[j];
      j--;
    }
    books[j + 1] = tmp;
  }
}

int library_count() { return nBooks; }

const BookEntry* library_get(int index) {
  if (index < 0 || index >= nBooks) return nullptr;
  return &books[index];
}
