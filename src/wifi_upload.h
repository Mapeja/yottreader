#pragma once
#include <Arduino.h>

void wifi_upload_begin();   // start AP + web server, draw info screen
void wifi_upload_handle();  // call every loop iteration while active
void wifi_upload_end();     // stop server + shut down radio
bool wifi_upload_active();
