#include "Arduino.h"
extern "C" {
    #include "sntp.h"
    #include <time.h>
}
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include "Private.h"

#define __THROTTLER(n) throttler_##n
#define _THROTTLER(n) __THROTTLER(n)
#define _callEvery(INTERVAL,__COUNT,BLOCK) static unsigned long _THROTTLER(__COUNT) = 0; \
					  if (millis() - _THROTTLER(__COUNT) >= INTERVAL){ \
						_THROTTLER(__COUNT) = millis(); \
						BLOCK \
					  }

#define callEvery(INTERVAL,BLOCK) _callEvery(INTERVAL,__COUNTER__,BLOCK)

#define strequ(constant, str) (strncmp(constant, str, sizeof constant - 1) == 0)
#define arraySize(a) (sizeof(a) / sizeof(a[0]))


#define REFRESH_INTERVAL_MINUTES 15
#define TIME_ADJUST_SECONDS 0

#define OTA 0
#define TEENSY_BAUD 0

const char *chip_hostname = "nixieclockntp";

void user_check_sntp_stamp(void *arg);
extern "C" struct tm *sntp_localtime(time_t *t);

os_timer_t sntp_timer;
bool sntp_running = false;

void refreshTime()
{
    sntp_running = true;
    configTime(1 * 3600, 60, "time.euro.apple.com", "0.de.pool.ntp.org", "pool.ntp.org");
}

void setup()
{
#if TEENSY_BAUD
    Serial.begin(921600);
#else
    Serial.begin(38400);
#endif

    WiFi.mode(WIFI_STA);
    WiFi.hostname(chip_hostname);
    WiFi.setOutputPower(1);
    WiFi.begin(WIFI_NET_NAME,WIFI_NET_PASSWD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(200);
    }

    delay(200);

#if OTA
    ArduinoOTA.setHostname(chip_hostname);
    ArduinoOTA.begin();
#endif

    refreshTime();
}

bool isEuropeanDST(struct tm *time)
{
    if (time->tm_mon < 3 || time->tm_mon > 10)  return false;
    if (time->tm_mon > 3 && time->tm_mon < 10)  return true;

    int previousSunday = time->tm_mday - time->tm_wday;

    if (time->tm_mon == 3) return previousSunday >= 25;
    if (time->tm_mon == 10) return previousSunday < 25;

    return false;
}

void loop()
{
#if OTA
    ArduinoOTA.handle();
#endif

    callEvery(REFRESH_INTERVAL_MINUTES * 60 * 1000, {
        refreshTime();
    });

    if (sntp_running == true && time(nullptr) != 0)
    {
        sntp_running = false;
    }

    callEvery(1000, {
        if (sntp_running == false)
        {
            time_t t = time(nullptr) + TIME_ADJUST_SECONDS;

            struct tm *time = sntp_localtime(&t);

            if (isEuropeanDST(time))
            {
                t += 60*60;
                time = sntp_localtime(&t);
            }

            //SNTP timestamp refers to 1900, therefore we need to substract 100 from the year to get e.x. "16" for the year 2016
            char buffer[196];
            snprintf((char *)&buffer,sizeof buffer, "$GPRMC,%02d%02d%02d,A,3751.65,S,14507.36,E,000.0,360.0,%02d%02d%02d,011.3,E*",
                time->tm_hour,time->tm_min,time->tm_sec,
                time->tm_mday,time->tm_mon + 1,time->tm_year - 100
            );

            //calculate checksum
            uint8_t checksum = 0;
            for (uint8_t i = 0; i < 62; i++)
            {
                checksum ^= buffer[i+1];
            }

            sprintf((char *)&buffer + 64,"%02X",checksum);

            Serial.println(buffer);
        }
    });
}
