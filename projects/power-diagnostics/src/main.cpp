// Guided probe for an external-power indication pin.
//
// The first version of this sketch streamed pin states over USB serial and
// left the correlation to whoever was reading the log. That cannot work: the
// cable being pulled IS the serial link, so the one condition worth measuring
// is the one condition with no way to report itself.
//
// So the panel runs the test. It says when to pull the cable, counts the
// battery-only window down, says when to plug back in, and records throughout.
// Only afterwards, with USB back, does any of it reach the host -- as a CSV
// dump plus the verdict already shown on screen.
//
// Two further changes from v1:
//
//   * Candidate pins are read three ways -- pulled up, pulled down, then
//     floating. A pin an external source actually drives holds its level
//     against both pulls; an unconnected one just follows whichever pull is
//     on. v1 read floating inputs only, so it could not tell a real signal
//     from noise, which is the likeliest reason it found nothing.
//
//   * GPIO 38-42 are sampled too. v1 tested 2/3/6/14/21/47/48 only, and those
//     five are not the whole of what this board leaves unassigned.
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <Wire.h>

constexpr int SDA_PIN = 16;
constexpr int SCL_PIN = 15;
constexpr int BATTERY_PIN = 9;

// Pins with no assigned function on this board. Flash, PSRAM, USB, the
// display, touch, audio, the battery divider, BOOT and UART0 are left out.
constexpr uint8_t CANDIDATES[] = {2, 3, 6, 14, 21, 38, 39, 40, 41, 42, 47, 48};
constexpr size_t PIN_COUNT = sizeof(CANDIDATES);

// How a pin answered the pull test.
enum PinState : uint8_t {
    DRIVEN_LOW = 0,   // held low against a pull-up: something drives it
    DRIVEN_HIGH = 1,  // held high against a pull-down
    FLOATING = 2,     // followed both pulls: nothing is connected
    CONFUSED = 3,     // followed neither; should not happen
};

struct Sample {
    uint32_t ms;
    uint16_t batteryMv;
    uint8_t usb;
    uint8_t pins[PIN_COUNT];
};

constexpr size_t MAX_SAMPLES = 220;
Sample samples[MAX_SAMPLES];
uint16_t sampleCount = 0;
// Samples live in ordinary RAM, so a reset loses them. Whether a reset
// happened at all is the part worth keeping, and that goes in NVS: if pulling
// the cable reboots the board, the run restarts from the first screen and the
// flag below says why.
uint16_t bootCount = 0;
bool resetDuringRun = false;

constexpr uint32_t SAMPLE_MS = 500;
constexpr uint32_t BASELINE_MS = 8000;
constexpr uint32_t BATTERY_MS = 15000;
// Two passes: one cycle can be a coincidence, a pin that tracks the cable
// twice is a signal.
constexpr uint8_t CYCLES = 2;
// Below this the board is running on USB alone and pulling the cable would
// simply switch it off, taking the recording with it.
constexpr uint16_t BATTERY_PRESENT_MV = 3300;

enum Phase : uint8_t {
    PHASE_CHECK,
    PHASE_BASELINE,
    PHASE_UNPLUG,
    PHASE_BATTERY,
    PHASE_REPLUG,
    PHASE_DONE,
};

TFT_eSPI tft;
Phase phase = PHASE_CHECK;
uint8_t cycle = 0;
uint32_t phaseStarted = 0;
uint32_t nextSample = 0;
uint32_t steadySince = 0;
bool dumped = false;

// Redraw only when the words or the countdown change; a full repaint every
// sample would flicker.
char lastScreen[64] = "";
int lastCountdown = -1;
uint16_t lastColor = 0;

uint16_t readBatteryMv() {
    uint32_t total = 0;
    for (int i = 0; i < 16; ++i) total += analogReadMilliVolts(BATTERY_PIN);
    return uint16_t((total / 16) * 2);
}

// Pulled up, then pulled down, then back to high impedance. A driven pin
// ignores the pull; a floating one follows it.
PinState probePin(uint8_t pin) {
    pinMode(pin, INPUT_PULLUP);
    delayMicroseconds(200);
    const int up = digitalRead(pin);
    pinMode(pin, INPUT_PULLDOWN);
    delayMicroseconds(200);
    const int down = digitalRead(pin);
    pinMode(pin, INPUT);
    if (up && down) return DRIVEN_HIGH;
    if (!up && !down) return DRIVEN_LOW;
    if (up && !down) return FLOATING;
    return CONFUSED;
}

uint32_t sampleInterval = SAMPLE_MS;

void recordSample() {
    if (sampleCount >= MAX_SAMPLES) {
        // Whoever is following the prompts took their time. Halve the
        // resolution rather than stop recording: keep every other sample and
        // sample half as often from here on.
        for (uint16_t i = 0; i < MAX_SAMPLES / 2; ++i) samples[i] = samples[i * 2];
        sampleCount = MAX_SAMPLES / 2;
        sampleInterval *= 2;
    }
    Sample &s = samples[sampleCount++];
    s.ms = millis();
    s.batteryMv = readBatteryMv();
    s.usb = HWCDC::isPlugged() ? 1 : 0;
    for (size_t i = 0; i < PIN_COUNT; ++i) s.pins[i] = probePin(CANDIDATES[i]);
}

const char *stateName(uint8_t state) {
    switch (state) {
        case DRIVEN_LOW: return "low";
        case DRIVEN_HIGH: return "high";
        case FLOATING: return "float";
        default: return "?";
    }
}

// A sample counts as settled when the cable state either side of it agrees,
// which drops the samples straddling a plug or unplug.
bool settled(uint16_t i) {
    if (i == 0 || i + 1 >= sampleCount) return false;
    return samples[i - 1].usb == samples[i].usb &&
           samples[i + 1].usb == samples[i].usb;
}

// Collect the states a pin took while USB was present and while it was not.
// A mask rather than a single value, so an unstable pin is visible as such.
void pinStates(size_t pin, uint8_t &plugged, uint8_t &unplugged) {
    plugged = unplugged = 0;
    for (uint16_t i = 0; i < sampleCount; ++i) {
        if (!settled(i)) continue;
        const uint8_t bit = 1 << samples[i].pins[pin];
        if (samples[i].usb)
            plugged |= bit;
        else
            unplugged |= bit;
    }
}

bool onlyBit(uint8_t mask, uint8_t &value) {
    if (!mask || (mask & (mask - 1))) return false;
    value = __builtin_ctz(mask);
    return true;
}

// A pin qualifies if it was stable in both conditions and the two differ.
size_t findCandidates(size_t *found, uint8_t *plugState, uint8_t *unplugState) {
    size_t count = 0;
    for (size_t p = 0; p < PIN_COUNT; ++p) {
        uint8_t pluggedMask, unpluggedMask, a, b;
        pinStates(p, pluggedMask, unpluggedMask);
        if (!onlyBit(pluggedMask, a) || !onlyBit(unpluggedMask, b)) continue;
        if (a == b) continue;
        found[count] = p;
        plugState[count] = a;
        unplugState[count] = b;
        ++count;
    }
    return count;
}

void averageBattery(uint16_t &plugged, uint16_t &unplugged) {
    uint32_t pSum = 0, uSum = 0;
    uint16_t pN = 0, uN = 0;
    for (uint16_t i = 0; i < sampleCount; ++i) {
        if (!settled(i)) continue;
        if (samples[i].usb) {
            pSum += samples[i].batteryMv;
            ++pN;
        } else {
            uSum += samples[i].batteryMv;
            ++uN;
        }
    }
    plugged = pN ? uint16_t(pSum / pN) : 0;
    unplugged = uN ? uint16_t(uSum / uN) : 0;
}

void draw(const char *line1, const char *line2, uint16_t color, int countdown) {
    char key[64];
    snprintf(key, sizeof(key), "%s|%s", line1, line2);
    if (!strcmp(key, lastScreen) && countdown == lastCountdown &&
        color == lastColor)
        return;
    strncpy(lastScreen, key, sizeof(lastScreen) - 1);
    lastScreen[sizeof(lastScreen) - 1] = 0;
    lastCountdown = countdown;
    lastColor = color;

    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(line1, 160, countdown >= 0 ? 70 : 100, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(line2, 160, countdown >= 0 ? 110 : 140, 2);
    if (countdown >= 0) {
        char seconds[8];
        snprintf(seconds, sizeof(seconds), "%d", countdown);
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString(seconds, 160, 170, 4);
    }
    char footer[48];
    snprintf(footer, sizeof(footer), "pass %u/%u   %u samples", cycle + 1,
             CYCLES, sampleCount);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(footer, 160, 220, 2);
}

void dumpSerial() {
    // Writes are non-blocking for the rest of the run, which for a dump this
    // long means the CDC buffer fills and the surplus is silently discarded --
    // it ate the header the first time this ran. Allow a short block here, and
    // flush a line at a time so the buffer never has a backlog to lose.
    Serial.setTxTimeoutMs(HWCDC::isConnected() ? 100 : 0);
    Serial.printf("# POWER_PROBE v2 boot=%u reset_during_run=%s samples=%u\n",
                  bootCount, resetDuringRun ? "yes" : "no", sampleCount);
    Serial.print("# columns: ms,usb,battery_mv");
    for (uint8_t pin : CANDIDATES) Serial.printf(",gpio%u", pin);
    Serial.println();
    for (uint16_t i = 0; i < sampleCount; ++i) {
        const Sample &s = samples[i];
        Serial.printf("%lu,%u,%u", (unsigned long)s.ms, s.usb, s.batteryMv);
        for (size_t p = 0; p < PIN_COUNT; ++p)
            Serial.printf(",%s", stateName(s.pins[p]));
        Serial.println();
        Serial.flush();
    }

    size_t found[PIN_COUNT];
    uint8_t plugState[PIN_COUNT], unplugState[PIN_COUNT];
    const size_t count = findCandidates(found, plugState, unplugState);
    uint16_t plugMv, unplugMv;
    averageBattery(plugMv, unplugMv);
    Serial.printf("# battery divider: %u mV on USB, %u mV on battery\n", plugMv,
                  unplugMv);
    if (!count) {
        Serial.println("# VERDICT: no candidate pin tracks USB power");
    } else {
        for (size_t i = 0; i < count; ++i)
            Serial.printf("# VERDICT: GPIO %u is %s on USB and %s on battery\n",
                          CANDIDATES[found[i]], stateName(plugState[i]),
                          stateName(unplugState[i]));
    }
    Serial.println("# end");
    Serial.flush();
    Serial.setTxTimeoutMs(0);
}

void showVerdict() {
    size_t found[PIN_COUNT];
    uint8_t plugState[PIN_COUNT], unplugState[PIN_COUNT];
    const size_t count = findCandidates(found, plugState, unplugState);

    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(count ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    tft.drawString(count ? "Signal found" : "No signal found", 160, 10, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    int y = 60;
    if (!count) {
        tft.drawString("No candidate pin tracks USB power.", 160, y, 2);
        y += 22;
        uint16_t plugMv, unplugMv;
        averageBattery(plugMv, unplugMv);
        char line[64];
        snprintf(line, sizeof(line), "Battery divider %u mV USB, %u mV off",
                 plugMv, unplugMv);
        tft.drawString(line, 160, y, 2);
        y += 22;
    }
    for (size_t i = 0; i < count && i < 5; ++i) {
        char line[64];
        snprintf(line, sizeof(line), "GPIO %u: %s on USB, %s on battery",
                 CANDIDATES[found[i]], stateName(plugState[i]),
                 stateName(unplugState[i]));
        tft.drawString(line, 160, y, 2);
        y += 22;
    }
    if (resetDuringRun) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Note: the board reset during an earlier run.", 160, 186,
                       2);
    }
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Full log sent over USB serial.", 160, 210, 2);
}

void setRunning(bool running) {
    Preferences prefs;
    prefs.begin("power-probe", false);
    prefs.putBool("running", running);
    prefs.end();
}

void enterPhase(Phase next) {
    phase = next;
    phaseStarted = millis();
    steadySince = 0;
}

void scanI2c() {
    Serial.println("# I2C scan:");
    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) Serial.printf("#   0x%02X\n", address);
    }
}

void setup() {
    Serial.begin(115200);
    // Without this, every print made while the cable is out blocks the loop
    // waiting for a host that is not there.
    Serial.setTxTimeoutMs(0);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    Wire.begin(SDA_PIN, SCL_PIN);
    analogReadResolution(12);
    analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
    for (uint8_t pin : CANDIDATES) pinMode(pin, INPUT);

    Preferences prefs;
    prefs.begin("power-probe", false);
    bootCount = prefs.getUShort("boots", 0) + 1;
    prefs.putUShort("boots", bootCount);
    // Set while a run is in progress and cleared when it finishes, so finding
    // it still set at boot means the board reset mid-test -- which is itself a
    // finding: it cannot ride out the cable being pulled.
    resetDuringRun = prefs.getBool("running", false);
    prefs.putBool("running", false);
    prefs.end();

    delay(300);
    Serial.println("# POWER_PROBE v2 -- follow the prompts on the screen.");
    Serial.println("# Commands: dump, restart");
    scanI2c();
    enterPhase(PHASE_CHECK);
}

void handleSerial() {
    static String line;
    while (Serial.available()) {
        const char c = Serial.read();
        if (c == '\r') continue;
        if (c != '\n' && line.length() < 32) {
            line += c;
            continue;
        }
        line.trim();
        if (line == "dump")
            dumpSerial();
        else if (line == "restart") {
            sampleCount = 0;
            sampleInterval = SAMPLE_MS;
            resetDuringRun = false;
            cycle = 0;
            dumped = false;
            lastScreen[0] = 0;
            enterPhase(PHASE_CHECK);
        }
        line = "";
    }
}

void loop() {
    handleSerial();

    const uint32_t now = millis();
    if (phase != PHASE_CHECK && phase != PHASE_DONE &&
        int32_t(now - nextSample) >= 0) {
        nextSample = now + sampleInterval;
        recordSample();
    }

    const bool usb = HWCDC::isPlugged();
    const uint32_t elapsed = now - phaseStarted;

    switch (phase) {
        case PHASE_CHECK: {
            const uint16_t mv = readBatteryMv();
            if (mv < BATTERY_PRESENT_MV) {
                draw("Connect the battery", "Pulling USB would cut power", TFT_RED,
                     -1);
                steadySince = 0;
                break;
            }
            if (!usb) {
                draw("Connect USB", "The test starts with the cable in", TFT_RED,
                     -1);
                steadySince = 0;
                break;
            }
            draw("Ready", "Starting...", TFT_GREEN, -1);
            if (!steadySince) steadySince = now;
            if (now - steadySince > 1500) {
                sampleCount = 0;
                sampleInterval = SAMPLE_MS;
                nextSample = now;
                setRunning(true);
                enterPhase(PHASE_BASELINE);
            }
            break;
        }

        case PHASE_BASELINE:
            draw("Baseline", "Leave USB connected", TFT_CYAN,
                 int((BASELINE_MS - min(elapsed, BASELINE_MS)) / 1000));
            if (elapsed >= BASELINE_MS) enterPhase(PHASE_UNPLUG);
            break;

        case PHASE_UNPLUG:
            draw("UNPLUG USB NOW", "Pull the cable right out", TFT_YELLOW, -1);
            if (!usb) {
                if (!steadySince) steadySince = now;
                // Ride out the moment of contact bounce before believing it.
                if (now - steadySince > 1000) enterPhase(PHASE_BATTERY);
            } else {
                steadySince = 0;
            }
            break;

        case PHASE_BATTERY:
            draw("On battery", "Do not plug in yet", TFT_CYAN,
                 int((BATTERY_MS - min(elapsed, BATTERY_MS)) / 1000));
            if (usb) {
                // Plugged back in early: the window is short, so take what was
                // recorded and move on rather than discarding the pass.
                enterPhase(PHASE_REPLUG);
            } else if (elapsed >= BATTERY_MS) {
                enterPhase(PHASE_REPLUG);
            }
            break;

        case PHASE_REPLUG:
            draw("PLUG USB BACK IN", "Then wait", TFT_YELLOW, -1);
            if (usb) {
                if (!steadySince) steadySince = now;
                if (now - steadySince > 1000) {
                    if (++cycle >= CYCLES) {
                        enterPhase(PHASE_DONE);
                    } else {
                        enterPhase(PHASE_BASELINE);
                    }
                }
            } else {
                steadySince = 0;
            }
            break;

        case PHASE_DONE:
            if (!dumped) {
                setRunning(false);
                showVerdict();
                // Only worth sending if someone is listening; `dump` replays
                // it later for a host that attaches after the fact.
                if (HWCDC::isConnected()) dumpSerial();
                dumped = true;
            }
            // The host may connect after the fact; `dump` replays the log.
            break;
    }

    delay(20);
}
