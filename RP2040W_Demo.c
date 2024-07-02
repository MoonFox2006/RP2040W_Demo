#include <stdio.h>
#include <string.h>
#include "hardware/timer.h"
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "serial.h"

//#define DEBUG_LOG(...)
#define DEBUG_LOG(...)  printf(__VA_ARGS__)

#define WIFI_SSID   "YOUR-SSID"
#define WIFI_PSWD   "YOUR-PSWD"

#define NTP_SERVER  "pool.ntp.org"
#define NTP_TZ      3 // GMT+3

#define TELE_KEY    "telegram-bot-key"
#define TELE_CHAT   -1

const char RESPONSE_OK[] = "\r\nOK\r\n";

static int halt(const char *msg) {
    printf("%s\r\n", msg);
    return -1;
}

static inline uint32_t millis() {
    return time_us_64() / 1000;
}

static uint16_t serial_read_timed(char *buf, uint16_t size, uint32_t first_timeout, uint32_t last_timeout) {
    uint32_t time;
    uint16_t result = 0;

    time = millis();
    while (! serial_available()) {
        if (millis() - time >= first_timeout)
            return 0;
    }
    time = millis();
    while (millis() - time < last_timeout) {
        if (serial_available()) {
            buf[result++] = serial_read();
            if (result >= size - 1)
                break;
            time = millis();
        }
    }
    buf[result] = '\0';
    return result;
}

static bool serial_at_cmd(const char *cmd, const char *expected, uint32_t timeout) {
    char out[256];
    uint16_t out_len;

    serial_discard();
    serial_print(cmd);
    serial_println();
    serial_flush();
    DEBUG_LOG(">%s\r\n", cmd);
    out_len = serial_read_timed(out, sizeof(out), timeout, 50);
    DEBUG_LOG("<%s", out);
    if (out_len) {
        return (! expected) || strstr(out, expected);
    }
    return false;
}

static bool serial_at_wifi_connect(const char *ssid, const char *pswd, uint32_t timeout) {
    char out[128];
    uint16_t out_len;

    sprintf(out, "AT+CWJAP=\"%s\",\"%s\",,,,,,%u", ssid, pswd, timeout / 1000);
    serial_discard();
    serial_print(out);
    serial_println();
    serial_flush();
    DEBUG_LOG(">%s\r\n", out);
    out_len = serial_read_timed(out, sizeof(out), timeout, 50);
    DEBUG_LOG("<%s", out);
    if ((out_len >= 16) && (! strncmp(out, "WIFI CONNECTED\r\n", 16))) {
        out_len = serial_read_timed(&out[out_len - 16], sizeof(out) - (out_len - 16), 5000, 50);
        DEBUG_LOG("<%s", out);
        if ((out_len >= 19) && (! strncmp(out, "WIFI GOT IP\r\n\r\nOK\r\n", 19)))
            return true;
    }
    return false;
}

static int8_t multisearch(const char *test, const char *samples[], uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        if (! strncmp(test, samples[i], strlen(samples[i])))
            return i;
    }
    return -1;
}

static int16_t parseint(char **str) {
    int16_t result = -1;

    while ((**str >= '0') && (**str <= '9')) {
        if (result != -1)
            result *= 10;
        else
            result = 0;
        result += *(*str)++ - '0';
    }
    return result;
}

static bool serial_at_ntp_time() {
    const char *WEEKDAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char *MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    char out[128];
    uint16_t out_len;

    serial_discard();
    serial_print("AT+CIPSNTPTIME?");
    serial_println();
    serial_flush();
    DEBUG_LOG(">%s\r\n", "AT+CIPSNTPTIME?");
    out_len = serial_read_timed(out, sizeof(out), 5000, 50);
    DEBUG_LOG("<%s", out);
    if ((out_len > 19) && (! strncmp(out, "+CIPSNTPTIME:", 13)) && (strstr(out, RESPONSE_OK) != NULL)) {
        datetime_t dt;
        char *str = &out[13];

        dt.dotw = multisearch(str, WEEKDAYS, 7);
        if ((dt.dotw >= 0) && ((dt.dotw <= 6)) && (str[3] == ' ')) {
            str += 4;
            dt.month = multisearch(str, MONTHS, 12) + 1;
            if ((dt.month >= 1) && (dt.month <= 12) && (str[3] == ' ')) {
                str += 4;
                while (*str == ' ')
                    ++str;
                dt.day = parseint(&str);
                if ((dt.day >= 1) && (dt.day <= 31) && (*str == ' ')) {
                    ++str;
                    while (*str == ' ')
                        ++str;
                    dt.hour = parseint(&str);
                    if ((dt.hour >= 0) && (dt.hour <= 23) && (*str == ':')) {
                        ++str;
                        dt.min = parseint(&str);
                        if ((dt.min >= 0) && (dt.min <= 59) && (*str == ':')) {
                            ++str;
                            dt.sec = parseint(&str);
                            if ((dt.sec >= 0) && (dt.sec <= 59) && (*str == ' ')) {
                                ++str;
                                while (*str == ' ')
                                    ++str;
                                dt.year = parseint(&str);
                                if (dt.year > 1970) {
                                    rtc_set_datetime(&dt);
                                    printf("%02d.%02d.%d %02d:%02d:%02d\r\n", dt.day, dt.month, dt.year, dt.hour, dt.min, dt.sec);
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}

static bool serial_at_telebot(const char *key, int32_t chat, const char *text) {
    char out[512];
    uint16_t out_len;

    out_len = snprintf(&out[256], sizeof(out) - 256, "{\"chat_id\":%ld,\"text\":\"%s\"}", chat, text);
    snprintf(out, 256, "AT+HTTPCPOST=\"https://api.telegram.org/bot%s/sendMessage\",%u,2,\"Content-Type: application/json\",\"Connection: close\"",
        TELE_KEY, out_len);
    if (serial_at_cmd(out, "\r\nOK\r\n\r\n>", 500)) {
        serial_discard();
        serial_print(&out[256]);
        serial_flush();
        DEBUG_LOG(">%s\r\n", &out[256]);
        out_len = serial_read_timed(out, sizeof(out), 15000, 50);
        DEBUG_LOG("<%s", out);
        return (out_len > 22) && strstr(out, "\r\n\r\nSEND OK\r\n") && strstr(out, "\"ok\":true");
    }
    return false;
}

int main() {
    stdio_init_all();

    sleep_ms(5000);

    rtc_init();
    serial_begin(115200);

    if (! serial_at_cmd("ATE0", RESPONSE_OK, 500))
        return halt("AT init fail!");

    if ((! serial_at_cmd("AT+SYSSTORE=0", RESPONSE_OK, 500)) || (! serial_at_cmd("AT+CWMODE=1", RESPONSE_OK, 500)))
        return halt("AT WiFi init fail!");
    if (! serial_at_wifi_connect(WIFI_SSID, WIFI_PSWD, 30000))
        return halt("AT WiFi connection fail!");
    if (! serial_at_cmd("AT+CIPSNTPCFG=1," __XSTRING(NTP_TZ) ",\"" NTP_SERVER "\"", RESPONSE_OK, 500))
        return halt("AT NTP init fail!");

    bool ntp_valid = false;

    while (true) {
        if (! ntp_valid) {
            if (! (ntp_valid = serial_at_ntp_time())) {
                printf("AT NTP get fail!\r\n");
                sleep_ms(5000);
            }
        } else {
            if (! serial_at_telebot(TELE_KEY, TELE_CHAT, "Hello, World!")) {
                printf("AT BOT send fail!\r\n");
            }
            sleep_ms(30000);
        }
    }
}
