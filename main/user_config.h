#ifndef USER_CONFIG_H
#define USER_CONFIG_H

//spi & i2c handle
#define LCD_HOST SPI3_HOST

// touch I2C port
#define Touch_SCL_NUM (GPIO_NUM_18)
#define Touch_SDA_NUM (GPIO_NUM_17)

// touch esp
#define ESP_SCL_NUM (GPIO_NUM_48)
#define ESP_SDA_NUM (GPIO_NUM_47)

//  DISP
#define EXAMPLE_PIN_NUM_LCD_CS     (GPIO_NUM_9) 
#define EXAMPLE_PIN_NUM_LCD_PCLK   (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0  (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1  (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2  (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3  (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_RST    (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_BK_LIGHT   (GPIO_NUM_8) 


#define EXAMPLE_PIN_NUM_TOUCH_ADDR        0x3b
#define EXAMPLE_PIN_NUM_TOUCH_RST         (-1)
#define EXAMPLE_PIN_NUM_TOUCH_INT         (-1)


#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 5



/*ADDR*/
#define EXAMPLE_RTC_ADDR 0x51

#define EXAMPLE_IMU_ADDR 0x6b


#define USER_DISP_ROT_90    1
#define USER_DISP_ROT_NONO  0
#define Rotated USER_DISP_ROT_90   //软件实现旋转


/*bl test*/
#define Backlight_Testing 0





#if (Rotated == USER_DISP_ROT_NONO)
#define EXAMPLE_LCD_H_RES 172   
#define EXAMPLE_LCD_V_RES 640
#else
#define EXAMPLE_LCD_H_RES 640   
#define EXAMPLE_LCD_V_RES 172
#endif

#define LCD_NOROT_HRES     172
#define LCD_NOROT_VRES     640
#define LVGL_DMA_BUFF_LEN (LCD_NOROT_HRES * 64 * 2)
#define LVGL_SPIRAM_BUFF_LEN (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2)

/* Network and server config */
#ifndef APP_WIFI_SSID
/*#define APP_WIFI_SSID "16517yyds"*/
#define APP_WIFI_SSID "JD"
#endif

#ifndef APP_WIFI_PASS
/*#define APP_WIFI_PASS "15172052448"*/
#define APP_WIFI_PASS "123456789"

#endif

#ifndef APP_SERVER_HOST
#define APP_SERVER_HOST "10.205.90.86"
#endif

#ifndef APP_SERVER_PORT
#define APP_SERVER_PORT 8000
#endif

#ifndef APP_API_LOGIN_PATH
#define APP_API_LOGIN_PATH "/client/login"
#endif

#ifndef APP_API_STATUS_PATH
#define APP_API_STATUS_PATH "/auth/status"
#endif

#ifndef APP_API_QUESTIONS_PATH
#define APP_API_QUESTIONS_PATH "/questions"
#endif

#ifndef APP_API_SUBMIT_PATH
#define APP_API_SUBMIT_PATH "/submit"
#endif

#ifndef APP_API_ATTEMPT_ANSWER_PATH_FMT
#define APP_API_ATTEMPT_ANSWER_PATH_FMT "/api/client/attempts/%s/answers"
#endif

#ifndef APP_SINGLE_ANSWER_RETRY_COUNT
#define APP_SINGLE_ANSWER_RETRY_COUNT 2
#endif

#endif
