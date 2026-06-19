/*!
 * @file Display.ino
 * @brief display.
 * @copyright	Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @licence     The MIT License (MIT)
 * @maintainer [yangfeng](feng.yang@dfrobot.com)
 * @version  V1.0
 * @date  2021-09-24
 * @url https://github.com/DFRobot/DFRobot_RGBLCD1602
 */
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <esp_system.h>
#include <esp_log.h>
#include <string.h>
#include <sys/time.h>
#include "esp_event.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"

static const char *TAG = "example";
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

/*
Change the RGBaddr value based on the hardware version
-----------------------------------------
       Moudule        | Version| RGBAddr|
-----------------------------------------
  LCD1602 Module      |  V1.0  | 0x60   |
-----------------------------------------
  LCD1602 Module      |  V1.1  | 0x6B   |
-----------------------------------------
  LCD1602 RGB Module  |  V1.0  | 0x60   |
-----------------------------------------
  LCD1602 RG Module  |  V2.0  | 0x2D   |
-----------------------------------------
*/

// screen helper funcs
DFRobot_RGBLCD1602 lcd(/*RGBAddr*/ 0x2D, /*lcdCols*/ 16, /*lcdRows*/ 2); // 16 characters and 2 lines of show

static i2c_master_dev_handle_t lcd_dev_handle;
static i2c_master_dev_handle_t rgb_dev_handle;

#define I2C_MASTER_TIMEOUT_MS 1000

const uint8_t color_define[4][3] = {
    {255, 255, 255}, // WHITE
    {255, 0, 0},     // RED
    {0, 255, 0},     // GREEN
    {0, 0, 255},     // BLUE
};

void DFRobot_RGBLCD1602::init(i2c_master_bus_handle_t bus)
{
    i2c_master_init(bus);
    if (_RGBAddr == (0x60))
    {
        REG_RED = 0x04;
        REG_GREEN = 0x03;
        REG_BLUE = 0x02;
        REG_ONLY = 0x02;
    }
    else if (_RGBAddr == (0x60 >> 1))
    {
        REG_RED = 0x06;   // pwm2
        REG_GREEN = 0x07; // pwm1
        REG_BLUE = 0x08;  // pwm0
        REG_ONLY = 0x08;
    }
    else if (_RGBAddr == (0x6B))
    {
        REG_RED = 0x06;   // pwm2
        REG_GREEN = 0x05; // pwm1
        REG_BLUE = 0x04;  // pwm0
        REG_ONLY = 0x04;
    }
    else if (_RGBAddr == (0x2D))
    {
        REG_RED = 0x01;   // pwm2
        REG_GREEN = 0x02; // pwm1
        REG_BLUE = 0x03;  // pwm0
        REG_ONLY = 0x01;
    }
    _showFunction = LCD_4BITMODE | LCD_1LINE | LCD_5x8DOTS;
    begin(_rows);
}

void DFRobot_RGBLCD1602::clear()
{
    command(LCD_CLEARDISPLAY); // clear display, set cursor position to zero
    vTaskDelay(pdMS_TO_TICKS(2));
}

void DFRobot_RGBLCD1602::home()
{
    command(LCD_RETURNHOME); // set cursor position to z
    vTaskDelay(pdMS_TO_TICKS(2));
}

void DFRobot_RGBLCD1602::noDisplay()
{
    _showControl &= ~LCD_DISPLAYON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::display()
{
    _showControl |= LCD_DISPLAYON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::stopBlink()
{
    _showControl &= ~LCD_BLINKON;
    command(LCD_DISPLAYCONTROL | _showControl);
}
void DFRobot_RGBLCD1602::blink()
{
    _showControl |= LCD_BLINKON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::noCursor()
{
    _showControl &= ~LCD_CURSORON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::cursor()
{
    _showControl |= LCD_CURSORON;
    command(LCD_DISPLAYCONTROL | _showControl);
}

void DFRobot_RGBLCD1602::scrollDisplayLeft(void)
{
    command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
}

void DFRobot_RGBLCD1602::scrollDisplayRight(void)
{
    command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
}

void DFRobot_RGBLCD1602::leftToRight(void)
{
    _showMode |= LCD_ENTRYLEFT;
    command(LCD_ENTRYMODESET | _showMode);
}

void DFRobot_RGBLCD1602::rightToLeft(void)
{
    _showMode &= ~LCD_ENTRYLEFT;
    command(LCD_ENTRYMODESET | _showMode);
}

void DFRobot_RGBLCD1602::noAutoscroll(void)
{
    _showMode &= ~LCD_ENTRYSHIFTINCREMENT;
    command(LCD_ENTRYMODESET | _showMode);
}

void DFRobot_RGBLCD1602::autoscroll(void)
{
    _showMode |= LCD_ENTRYSHIFTINCREMENT;
    command(LCD_ENTRYMODESET | _showMode);
}

void DFRobot_RGBLCD1602::customSymbol(uint8_t location, uint8_t charmap[])
{

    location &= 0x7; // we only have 8 locations 0-7
    command(LCD_SETCGRAMADDR | (location << 3));

    uint8_t data[9];
    data[0] = 0x40;
    for (int i = 0; i < 8; i++)
    {
        data[i + 1] = charmap[i];
    }
    send(data, 9);
}

void DFRobot_RGBLCD1602::setCursor(uint8_t col, uint8_t row)
{

    col = (row == 0 ? col | 0x80 : col | 0xc0);
    uint8_t data[3] = {0x80, col};

    send(data, 2);
}

void DFRobot_RGBLCD1602::setRGB(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t temp_r, temp_g, temp_b;
    if (_RGBAddr == 0x60 >> 1)
    {
        temp_r = (uint16_t)r * 192 / 255;
        temp_g = (uint16_t)g * 192 / 255;
        temp_b = (uint16_t)b * 192 / 255;
        setReg(REG_RED, temp_r);
        setReg(REG_GREEN, temp_g);
        setReg(REG_BLUE, temp_b);
    }
    else
    {
        setReg(REG_RED, r);
        setReg(REG_GREEN, g);
        setReg(REG_BLUE, b);
        if (_RGBAddr == 0x6B)
        {
            setReg(0x07, 0xFF);
        }
    }
}

void DFRobot_RGBLCD1602::setColor(uint8_t color)
{
    if (color > 3)
        return;
    setRGB(color_define[color][0], color_define[color][1], color_define[color][2]);
}

void DFRobot_RGBLCD1602::write(uint8_t value)
{

    uint8_t data[3] = {0x40, value};
    send(data, 2);
}

inline void DFRobot_RGBLCD1602::command(uint8_t value)
{
    uint8_t data[3] = {0x80, value};
    send(data, 2);
}

void DFRobot_RGBLCD1602::setBacklight(bool mode)
{
    if (mode)
    {
        setColorWhite(); // turn backlight on
    }
    else
    {
        closeBacklight(); // turn backlight off
    }
}

/*******************************private*******************************/
void DFRobot_RGBLCD1602::begin(uint8_t rows, uint8_t charSize)
{
    if (rows > 1)
    {
        _showFunction |= LCD_2LINE;
    }
    _numLines = rows;
    _currLine = 0;
    ///< for some 1 line displays you can select a 10 pixel high font
    if ((charSize != 0) && (rows == 1))
    {
        _showFunction |= LCD_5x10DOTS;
    }

    ///< SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
    ///< according to datasheet, we need at least 40ms after power rises above 2.7V
    ///< before sending commands. Arduino can turn on way befer 4.5V so we'll wait 50
    vTaskDelay(pdMS_TO_TICKS(50));

    ///< this is according to the hitachi HD44780 datasheet
    ///< page 45 figure 23

    ///< Send function set command sequence
    command(LCD_FUNCTIONSET | _showFunction);
    vTaskDelay(pdMS_TO_TICKS(5));

    ///< second try
    command(LCD_FUNCTIONSET | _showFunction);
    vTaskDelay(pdMS_TO_TICKS(5));

    ///< third go
    command(LCD_FUNCTIONSET | _showFunction);

    ///< turn the display on with no cursor or blinking default
    _showControl = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
    display();

    ///< clear it off
    clear();

    ///< Initialize to default text direction (for romance languages)
    _showMode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
    ///< set the entry mode
    command(LCD_ENTRYMODESET | _showMode);

    if (_RGBAddr == (0xc0 >> 1))
    {
        ///< backlight init
        setReg(REG_MODE1, 0);
        ///< set LEDs controllable by both PWM and GRPPWM registers
        setReg(REG_OUTPUT, 0xFF);
        ///< set MODE2 values
        ///< 0010 0000 -> 0x20  (DMBLNK to 1, ie blinky mode)
        setReg(REG_MODE2, 0x20);
    }
    else if (_RGBAddr == (0x60 >> 1))
    {
        setReg(0x01, 0x00);
        setReg(0x02, 0xfF);
        setReg(0x04, 0x15);
    }
    else if (_RGBAddr == 0x6B)
    {
        setReg(0x2F, 0x00);
        setReg(0x00, 0x20);
        setReg(0x01, 0x00);
        setReg(0x02, 0x01);
        setReg(0x03, 4);
    }
    setColorWhite();
}

// Constructor
DFRobot_RGBLCD1602::DFRobot_RGBLCD1602(uint8_t RGBAddr, uint8_t lcdCols,
                                       uint8_t lcdRows, uint8_t lcdAddr)
{
    _RGBAddr = RGBAddr;
    _lcdAddr = lcdAddr;
    _cols = lcdCols;
    _rows = lcdRows;
}

// printstr
void DFRobot_RGBLCD1602::printstr(const char *str)
{
    while (*str)
    {
        write((uint8_t)*str++);
    }
}

//---------------------------------------------------------------------------------------------------
void DFRobot_RGBLCD1602::i2c_master_init(i2c_master_bus_handle_t bus_handle)
{

    i2c_device_config_t lcd_dev_config = {};
    lcd_dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    lcd_dev_config.device_address = _lcdAddr;
    lcd_dev_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &lcd_dev_config, &lcd_dev_handle));

    i2c_device_config_t rgb_dev_config = {};
    rgb_dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    rgb_dev_config.device_address = _RGBAddr;
    rgb_dev_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &rgb_dev_config, &rgb_dev_handle));
}

void DFRobot_RGBLCD1602::send(uint8_t *data, uint8_t len)
{
    i2c_master_transmit(lcd_dev_handle, data, len, pdMS_TO_TICKS(100));
}

void DFRobot_RGBLCD1602::setReg(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    i2c_master_transmit(
        rgb_dev_handle,
        buf,
        2,
        pdMS_TO_TICKS(100));
}

/* Variable holding number of times ESP32 restarted since first boot.
 * It is placed into RTC memory using RTC_DATA_ATTR and
 * maintains its value when ESP32 wakes from deep sleep.
 */
RTC_DATA_ATTR static int boot_count = 0;

static void obtain_time(void);

static void sntp_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const esp_netif_sntp_time_sync_t *evt = (const esp_netif_sntp_time_sync_t *)data;
    if (evt)
    {
        char ts[64];
        time_t t = evt->tv.tv_sec;
        struct tm tm_utc;
        gmtime_r(&t, &tm_utc);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_utc);
        ESP_LOGI(TAG, "SNTP event: time synced (UTC): %s.%06ld", ts, (long)evt->tv.tv_usec);
    }
    else
    {
        ESP_LOGI(TAG, "SNTP event: time synced (no timeval provided)");
    }
}

#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_CUSTOM
void sntp_sync_time(struct timeval *tv)
{
    settimeofday(tv, NULL);
    ESP_LOGI(TAG, "Time is synchronized from custom code");
    sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}
#endif

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void print_servers(void)
{
    ESP_LOGI(TAG, "List of configured NTP servers:");

    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i)
    {
        if (esp_sntp_getservername(i))
        {
            ESP_LOGI(TAG, "server %d: %s", i, esp_sntp_getservername(i));
        }
        else
        {
            // we have either IPv4 or IPv6 address, let's print it
            char buff[INET6_ADDRSTRLEN];
            ip_addr_t const *ip = esp_sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
                ESP_LOGI(TAG, "server %d: %s", i, buff);
        }
    }
}

static void obtain_time(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // Register handler to log SNTP event with timeval payload
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_SNTP_EVENT, NETIF_SNTP_TIME_SYNC, &sntp_event_handler, NULL));

#if LWIP_DHCP_GET_NTP_SRV
    /**
     * NTP server address could be acquired via DHCP,
     * see following menuconfig options:
     * 'LWIP_DHCP_GET_NTP_SRV' - enable STNP over DHCP
     * 'LWIP_SNTP_DEBUG' - enable debugging messages
     *
     * NOTE: This call should be made BEFORE esp acquires IP address from DHCP,
     * otherwise NTP option would be rejected by default.
     */
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
    config.start = false;                     // start SNTP service explicitly (after connecting)
    config.server_from_dhcp = true;           // accept NTP offers from DHCP server, if any (need to enable *before* connecting)
    config.renew_servers_after_new_IP = true; // let esp-netif update configured SNTP server(s) after receiving DHCP lease
    config.index_of_first_server = 1;         // updates from server num 1, leaving server 0 (from DHCP) intact
    // configure the event on which we renew servers
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
#else
    config.ip_event_to_renew = IP_EVENT_ETH_GOT_IP;
#endif
    config.sync_cb = time_sync_notification_cb; // only if we need the notification function
    esp_netif_sntp_init(&config);

#endif /* LWIP_DHCP_GET_NTP_SRV */

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

#if LWIP_DHCP_GET_NTP_SRV
    ESP_LOGI(TAG, "Starting SNTP");
    esp_netif_sntp_start();
#if LWIP_IPV6 && SNTP_MAX_SERVERS > 2
    /* This demonstrates using IPv6 address as an additional SNTP server
     * (statically assigned IPv6 address is also possible)
     */
    ip_addr_t ip6;
    if (ipaddr_aton("2a01:3f7::1", &ip6))
    { // ipv6 ntp source "ntp.netnod.se"
        esp_sntp_setserver(2, &ip6);
    }
#endif /* LWIP_IPV6 */

#else
    ESP_LOGI(TAG, "Initializing and starting SNTP");
#if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    /* This demonstrates configuring more than one server
     */
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
                                                                      ESP_SNTP_SERVER_LIST(CONFIG_SNTP_TIME_SERVER, "pool.ntp.org"));
#else
    /*
     * This is the basic default config with one server and starting the service
     */
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
#endif
    config.sync_cb = time_sync_notification_cb; // Note: This is only needed if we want
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    config.smooth_sync = true;
#endif

    esp_netif_sntp_init(&config);
#endif

    print_servers();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {};
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_ERROR_CHECK(example_disconnect());
    esp_netif_sntp_deinit();
    ESP_ERROR_CHECK(esp_event_handler_unregister(NETIF_SNTP_EVENT, NETIF_SNTP_TIME_SYNC, &sntp_event_handler));
}

extern "C" void app_main(void)
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = (gpio_num_t)I2C_MASTER_SDA_IO;
    bus_config.scl_io_num = (gpio_num_t)I2C_MASTER_SCL_IO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus;

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

    // On a cold power-up the ESP32 boots faster than the LCD's internal
    // power-on reset. Give the display time to come up before talking to it.
    vTaskDelay(pdMS_TO_TICKS(100));

    lcd.init(bus);
    lcd.setRGB(255, 255, 255);
    vTaskDelay(pdMS_TO_TICKS(1000));
    char line[17];

    ++boot_count;
    ESP_LOGI(TAG, "Boot count: %d", boot_count);

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900))
    {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        obtain_time();
        // update 'now' variable with current time
        time(&now);
    }
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    else
    {
        // add 500 ms error to the current system time.
        // Only to demonstrate a work of adjusting method!
        {
            ESP_LOGI(TAG, "Add a error for test adjtime");
            struct timeval tv_now;
            gettimeofday(&tv_now, NULL);
            int64_t cpu_time = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
            int64_t error_time = cpu_time + 500 * 1000L;
            struct timeval tv_error = {.tv_sec = error_time / 1000000L, .tv_usec = error_time % 1000000L};
            settimeofday(&tv_error, NULL);
        }

        ESP_LOGI(TAG, "Time was set, now just adjusting it. Use SMOOTH SYNC method.");
        obtain_time();
        // update 'now' variable with current time
        time(&now);
    }
#endif

    char strftime_buf[64];

    // Set timezone to Eastern Standard Time and print local time
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Los Angeles is: %s", strftime_buf);
    bool displayOn = true;
    while (1)
    {
        int h = timeinfo.tm_hour;
        bool shouldBeOn = (h < 23) && (h > 7);

        if (shouldBeOn != displayOn)
        {
            if (shouldBeOn)
            {
                lcd.display();
                lcd.setRGB(255, 255, 255);
            }
            else
            {
                lcd.noDisplay();
                lcd.setRGB(0, 0, 0);
            }
            displayOn = shouldBeOn;
        }
        // Re-read the system clock each second. The onboard timer keeps
        // advancing it, so this is what makes the display tick.
        time(&now);
        localtime_r(&now, &timeinfo);
        int hour = timeinfo.tm_hour % 12;
        // Format the current local time as HH:MM:SS and show it.
        strftime(line, sizeof(line), "  %l:%M:%S %p", &timeinfo);
        lcd.setCursor(0, 0);
        lcd.printstr(line);
        strftime(line, sizeof(line), "   %a %b %d", &timeinfo);
        lcd.setCursor(0, 1);
        lcd.printstr(line);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
