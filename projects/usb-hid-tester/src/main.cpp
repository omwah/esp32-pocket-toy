#include <Arduino.h>
#include <TFT_eSPI.h>
#include "driver/gpio.h"
#include "usb/usb_host.h"
#include "hid_host.h"
#include "hid_usage_keyboard.h"
#include "hippo_image.h"

namespace {
TFT_eSPI tft;
TFT_eSprite hippoCanvas = TFT_eSprite(&tft);
constexpr uint16_t BG = 0x10C5, PANEL = 0x2128, CREAM = 0xFF5A;
constexpr uint16_t HIPPO = 0x9CF3, HIPPO_DARK = 0x52AA, PINK = 0xF3B2;

struct UiEvent { enum Type : uint8_t { CONNECT, DISCONNECT, PRESS } type; uint8_t code, modifier; };
QueueHandle_t uiQueue = nullptr;
QueueHandle_t hostQueue = nullptr;
bool keyboardConnected = false;
char keyLabel[24] = "waiting...";
uint32_t lastKeyMs = 0, lastFrameMs = 0;
uint8_t frame = 0;

struct HostEvent { hid_host_device_handle_t handle; hid_host_driver_event_t event; void *arg; };

bool contains(const uint8_t *keys, uint8_t key) {
  for (int i = 0; i < HID_KEYBOARD_KEY_MAX; ++i) if (keys[i] == key) return true;
  return false;
}

const char *specialName(uint8_t c) {
  switch (c) {
    case 0x28: return "ENTER"; case 0x29: return "ESC"; case 0x2a: return "BACKSPACE";
    case 0x2b: return "TAB"; case 0x2c: return "SPACE"; case 0x39: return "CAPS LOCK";
    case 0x3a: return "F1"; case 0x3b: return "F2"; case 0x3c: return "F3";
    case 0x3d: return "F4"; case 0x3e: return "F5"; case 0x3f: return "F6";
    case 0x40: return "F7"; case 0x41: return "F8"; case 0x42: return "F9";
    case 0x43: return "F10"; case 0x44: return "F11"; case 0x45: return "F12";
    case 0x49: return "INSERT"; case 0x4a: return "HOME"; case 0x4b: return "PAGE UP";
    case 0x4c: return "DELETE"; case 0x4d: return "END"; case 0x4e: return "PAGE DOWN";
    case 0x4f: return "RIGHT"; case 0x50: return "LEFT"; case 0x51: return "DOWN";
    case 0x52: return "UP"; case 0x53: return "NUM LOCK";
    default: return nullptr;
  }
}

void formatKey(uint8_t code, uint8_t mod, char *out, size_t n) {
  const char *special = specialName(code);
  if (special) { snprintf(out, n, "%s", special); return; }
  bool shift = mod & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT);
  if (code >= 0x04 && code <= 0x1d) {
    char c = 'a' + code - 0x04; if (shift) c -= 32; snprintf(out, n, "%c", c); return;
  }
  const char plain[] = "1234567890-=[]\\#;'`,./";
  const char shifted[] = "!@#$%^&*()_+{}|~:\"~<>?";
  if (code >= 0x1e && code <= 0x38) {
    uint8_t i = code - 0x1e; snprintf(out, n, "%c", shift ? shifted[i] : plain[i]); return;
  }
  if (code >= 0xe0 && code <= 0xe7) {
    static const char *names[] = {"L CTRL","L SHIFT","L ALT","L GUI","R CTRL","R SHIFT","R ALT","R GUI"};
    snprintf(out, n, "%s", names[code - 0xe0]); return;
  }
  snprintf(out, n, "HID 0x%02X", code);
}

void post(UiEvent::Type type, uint8_t code = 0, uint8_t mod = 0) {
  UiEvent e{type, code, mod}; if (uiQueue) xQueueSend(uiQueue, &e, 0);
}

void keyboardReport(const uint8_t *data, int length) {
  if (length < (int)sizeof(hid_keyboard_input_report_boot_t)) return;
  auto *report = (const hid_keyboard_input_report_boot_t *)data;
  static uint8_t previous[HID_KEYBOARD_KEY_MAX] = {};
  for (int i = 0; i < HID_KEYBOARD_KEY_MAX; ++i)
    if (report->key[i] > HID_KEY_ERROR_UNDEFINED && !contains(previous, report->key[i]))
      post(UiEvent::PRESS, report->key[i], report->modifier.val);
  memcpy(previous, report->key, sizeof(previous));
}

void interfaceCallback(hid_host_device_handle_t handle, hid_host_interface_event_t event, void *) {
  hid_host_dev_params_t params{};
  if (hid_host_device_get_params(handle, &params) != ESP_OK) return;
  if (event == HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
    uint8_t data[64]; size_t length = 0;
    if (params.proto == HID_PROTOCOL_KEYBOARD &&
        hid_host_device_get_raw_input_report_data(handle, data, sizeof(data), &length) == ESP_OK)
      keyboardReport(data, length);
  } else if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
    if (params.proto == HID_PROTOCOL_KEYBOARD) post(UiEvent::DISCONNECT);
    hid_host_device_close(handle);
  }
}

void deviceEvent(hid_host_device_handle_t handle, hid_host_driver_event_t event, void *) {
  if (event != HID_HOST_DRIVER_EVENT_CONNECTED) return;
  hid_host_dev_params_t params{};
  hid_host_device_get_params(handle, &params);
  const hid_host_device_config_t cfg = {.callback = interfaceCallback, .callback_arg = nullptr};
  if (hid_host_device_open(handle, &cfg) != ESP_OK) return;
  if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
    hid_class_request_set_protocol(handle, HID_REPORT_PROTOCOL_BOOT);
    if (params.proto == HID_PROTOCOL_KEYBOARD) hid_class_request_set_idle(handle, 0, 0);
  }
  hid_host_device_start(handle);
  if (params.proto == HID_PROTOCOL_KEYBOARD) post(UiEvent::CONNECT);
}

void driverCallback(hid_host_device_handle_t h, hid_host_driver_event_t e, void *arg) {
  HostEvent msg{h, e, arg}; if (hostQueue) xQueueSend(hostQueue, &msg, 0);
}

void usbTask(void *) {
  const usb_host_config_t cfg = {.skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1};
  if (usb_host_install(&cfg) != ESP_OK) vTaskDelete(nullptr);
  const hid_host_driver_config_t hidCfg = {.create_background_task=true, .task_priority=5,
    .stack_size=4096, .core_id=0, .callback=driverCallback, .callback_arg=nullptr};
  hid_host_install(&hidCfg);
  for (;;) {
    uint32_t flags = 0; usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    HostEvent msg;
    while (xQueueReceive(hostQueue, &msg, 0)) deviceEvent(msg.handle, msg.event, msg.arg);
  }
}

void drawHippo(bool) {
  // The source art is square and includes a black background; display it at 130x130.
  hippoCanvas.pushImage(0, 0, HIPPO_IMAGE_WIDTH, HIPPO_IMAGE_HEIGHT, hippoImage);
  hippoCanvas.pushSprite(184, 45);
}

void drawStatic() {
  tft.fillScreen(BG); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(CREAM, BG); tft.drawString("KEYBOARD TEST", 12, 10, 4);
  tft.drawFastHLine(12, 40, 296, HIPPO_DARK);
  tft.fillRoundRect(10, 58, 168, 105, 10, PANEL);
  tft.setTextColor(0xAEBF, PANEL); tft.drawString("LAST KEY", 23, 70, 2);
  tft.fillRoundRect(19, 94, 150, 55, 8, TFT_BLACK);
  tft.setTextColor(CREAM, BG); tft.drawString("Connect a USB keyboard", 12, 215, 2);
}

void drawStatus() {
  tft.fillRect(12, 174, 168, 27, BG);
  tft.fillCircle(20, 187, 5, keyboardConnected ? TFT_GREEN : TFT_ORANGE);
  tft.setTextColor(CREAM, BG); tft.setTextDatum(TL_DATUM);
  tft.drawString(keyboardConnected ? "Keyboard connected" : "Waiting for keyboard", 31, 179, 2);
}

void drawKey() {
  tft.fillRoundRect(19, 94, 150, 55, 8, TFT_BLACK);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_CYAN, TFT_BLACK);
  int font = strlen(keyLabel) > 10 ? 2 : 4;
  tft.drawString(keyLabel, 94, 121, font);
}
} // namespace

void setup() {
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
  tft.init(); tft.setRotation(3); tft.setTextWrap(false);
  // RGB565 sprite buffer keeps LCD updates tear-free.
  hippoCanvas.setColorDepth(16);
  if (!hippoCanvas.createSprite(130, 130)) {
    tft.fillScreen(TFT_RED);
    return;
  }
  drawStatic(); drawStatus(); drawKey(); drawHippo(false);
  uiQueue = xQueueCreate(16, sizeof(UiEvent));
  hostQueue = xQueueCreate(12, sizeof(HostEvent));
  xTaskCreatePinnedToCore(usbTask, "usb-host", 6144, nullptr, 3, nullptr, 0);
}

void loop() {
  UiEvent e;
  while (xQueueReceive(uiQueue, &e, 0)) {
    if (e.type == UiEvent::CONNECT) keyboardConnected = true;
    else if (e.type == UiEvent::DISCONNECT) keyboardConnected = false;
    else { formatKey(e.code, e.modifier, keyLabel, sizeof(keyLabel)); lastKeyMs = millis(); drawKey(); }
    drawStatus();
  }
  if (millis() - lastFrameMs >= 170) {
    lastFrameMs = millis(); ++frame; drawHippo(millis() - lastKeyMs < 650);
  }
  delay(5);
}
