#include "state_machine.h"
#include "reader.h"
#include "menu.h"
#include "library.h"
#include "storage.h"
#include "settings.h"
#include "wifi_upload.h"

static AppState state = APP_READING;

void     sm_init(AppState s) { state = s; }
AppState sm_state()          { return state; }

static void enterMenu() {
  state = APP_MENU;
  menu_open();
}

static void enterWifi() {
  reader_close(); // free 48KB page index so WiFi stack has enough heap
  state = APP_WIFI;
  wifi_upload_begin();
}

static void enterSettings() {
  state = APP_SETTINGS;
  settings_open();
}

static void enterReading(const char* path, int page) {
  if (reader_open(path)) {
    reader_goto(page);
    reader_draw(true);
    state = APP_READING;
  } else {
    enterMenu();
  }
}

void sm_handle(ButtonEvent evt) {
  // WiFi must pump the server every loop iteration, not just on button events.
  if (state == APP_WIFI) {
    wifi_upload_handle();
    if (evt == BTN_SINGLE || evt == BTN_LONG || evt == BTN_CLICK_HOLD) {
      wifi_upload_end();
      library_scan();
      enterMenu();
    }
    return;
  }

  if (evt == BTN_NONE) return;

  switch (state) {
    case APP_READING:
      if (evt == BTN_SINGLE) {
        reader_go_next();
        reader_draw();
        storage_save_position(reader_path(), reader_current_page(), reader_page_count());
      } else if (evt == BTN_DOUBLE) {
        reader_go_prev();
        reader_draw();
        storage_save_position(reader_path(), reader_current_page(), reader_page_count());
      } else if (evt == BTN_LONG) {
        storage_save_position(reader_path(), reader_current_page(), reader_page_count());
        enterMenu();
      } else if (evt == BTN_CLICK_HOLD) {
        storage_save_position(reader_path(), reader_current_page(), reader_page_count());
        enterSettings();
      }
      break;

    case APP_MENU: {
      const char* result = menu_handle(evt);
      if (result == nullptr) break;
      if (result[0] == '\0') {
        enterSettings();
      } else {
        enterReading(result, storage_get_book_page(result));
      }
      break;
    }

    case APP_SETTINGS: {
      SettingsResult r = settings_handle(evt);
      if (r == SETTINGS_GO_LIBRARY) enterMenu();
      else if (r == SETTINGS_GO_WIFI) enterWifi();
      break;
    }

  }
}
