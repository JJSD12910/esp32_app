/*
 * ESP32-S3 Touch LCD 3.49" LVGL V8 测试程序
 * 
 * 功能描述：
 * 本程序演示如何在ESP32-S3平台上使用LVGL V8图形库驱动3.49英寸触摸LCD显示屏
 * 包含LCD初始化、触摸处理、LVGL集成以及演示界面显示等功能
 * 
 * 平台：ESP32-S3
 * 显示屏：3.49英寸触摸LCD
 * 图形库：LVGL V8
 */

#include <stdio.h>

extern "C" {
#include "app_flow.h"
#include "app_ui_dispatch.h"
}

// FreeRTOS相关头文件
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

// ESP32硬件驱动相关头文件
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"

// LVGL图形库相关头文件
#include "lvgl.h"
#include "lv_demos.h"

// 自定义配置和驱动相关头文件
#include "user_config.h"
#include "esp_lcd_axs15231b.h"
#include "i2c_bsp.h"
#include "button_bsp.h"
#include "esp_io_expander_tca9554.h"
#include "lcd_bl_pwm_bsp.h"

// 日志标签
static const char *TAG = "example";
static QueueHandle_t s_ui_job_queue = NULL;

// LVGL互斥锁，用于保护LVGL API的并发访问
static SemaphoreHandle_t lvgl_mux = NULL;

// LVGL DMA缓冲区，用于高效的数据传输
static uint16_t *lvgl_dma_buf = NULL; 

// LVGL刷新信号量，用于同步显示刷新操作
static SemaphoreHandle_t lvgl_flush_semap;

typedef struct
{
    app_ui_dispatch_fn_t fn;
    void *ctx;
} app_ui_job_t;

// 旋转缓冲区，用于处理90度旋转显示
#if (Rotated == USER_DISP_ROT_90)
uint16_t* rotat_ptr = NULL;
#endif
static esp_io_expander_handle_t io_expander = NULL;
static bool is_vbatpowerflag = false;

// 定义LCD像素格式为16位色
#define LCD_BIT_PER_PIXEL (16)

/**
 * @brief LVGL触摸回调函数
 * @param drv LVGL输入设备驱动结构
 * @param data 触摸数据结构，包含触摸点坐标和状态
 */
static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);

/**
 * @brief 获取LVGL互斥锁
 * @param timeout_ms 超时时间（毫秒），-1表示无限等待
 * @return 获取成功返回true，失败返回false
 */
static bool example_lvgl_lock(int timeout_ms);

/**
 * @brief 释放LVGL互斥锁
 */
static void example_lvgl_unlock(void);

/**
 * @brief LVGL显示刷新回调函数
 * @param drv LVGL显示驱动结构
 * @param area 要刷新的区域
 * @param color_map 颜色数据缓冲区
 */
static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);

/**
 * @brief LVGL主任务
 * @param arg 任务参数（未使用）
 */
void example_lvgl_port_task(void *arg);

/**
 * @brief 背光控制循环任务
 * @param arg 任务参数（未使用）
 */
static void example_backlight_loop_task(void *arg);
static void power_Test(void *arg);
static esp_err_t tca9554_init(void);
static void example_button_pwr_task(void *arg);
static void example_process_ui_jobs(void);

// LCD初始化命令序列
static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = 
{
    {0x11, (uint8_t []){0x00}, 0, 100},  // 退出睡眠模式命令
    {0x29, (uint8_t []){0x00}, 0, 100},  // 显示开启命令
};

/**
 * @brief LCD颜色传输完成回调函数
 * @param panel_io LCD面板IO句柄
 * @param edata 事件数据
 * @param user_ctx 用户上下文
 * @return 总是返回false
 */
static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t TaskWoken = pdFALSE;
    // 从ISR中释放刷新信号量
    xSemaphoreGiveFromISR(lvgl_flush_semap, &TaskWoken);
    return TaskWoken == pdTRUE;
}

extern "C" bool app_ui_dispatch(app_ui_dispatch_fn_t fn, void *ctx, TickType_t timeout_ticks)
{
    if (!s_ui_job_queue || !fn)
    {
        return false;
    }

    app_ui_job_t job = {
        .fn = fn,
        .ctx = ctx,
    };

    return xQueueSend(s_ui_job_queue, &job, timeout_ticks) == pdTRUE;
}

static void example_process_ui_jobs(void)
{
    if (!s_ui_job_queue)
    {
        return;
    }

    app_ui_job_t job;
    while (xQueueReceive(s_ui_job_queue, &job, 0) == pdTRUE)
    {
        if (job.fn)
        {
            job.fn(job.ctx);
        }
    }
}

/**
 * @brief LVGL系统tick递增回调函数
 * @param arg 回调参数（未使用）
 */
static void example_increase_lvgl_tick(void *arg)
{
    // 按配置的周期增加LVGL tick计数
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void power_Test(void *arg)
{
    if (gpio_get_level(GPIO_NUM_16))
    {
        is_vbatpowerflag = true;
    }
    vTaskDelete(NULL);
}

static esp_err_t tca9554_init(void)
{
    i2c_master_bus_handle_t tca9554_i2c_bus_ = NULL;
    esp_err_t err = i2c_master_get_bus_handle(0, &tca9554_i2c_bus_);
    if (err != ESP_OK || !tca9554_i2c_bus_)
    {
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    err = esp_io_expander_new_i2c_tca9554(tca9554_i2c_bus_, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander);
    if (err != ESP_OK || !io_expander)
    {
        io_expander = NULL;
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    err = esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_6, IO_EXPANDER_OUTPUT);
    if (err != ESP_OK)
    {
        return err;
    }

    return esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_6, 1);
}

static void example_button_pwr_task(void* arg)
{
    for (;;)
    {
        EventBits_t even = xEventGroupWaitBits(pwr_groups, set_bit_all, pdTRUE, pdFALSE, pdMS_TO_TICKS(2 * 1000));
        if (get_bit_button(even, 0))
        {

        }
        else if (get_bit_button(even, 1)) // long press
        {
            if (is_vbatpowerflag && io_expander)
            {
                is_vbatpowerflag = false;
                esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_6, 0);
            }
        }
        else if (get_bit_button(even, 2))
        {
            if (!is_vbatpowerflag)
            {
                is_vbatpowerflag = true;
            }
        }
    }
}

/**
 * @brief 应用程序入口点
 */
extern "C" void app_main(void)
{
    // 初始化LCD背光PWM控制，设置为最大亮度(255)
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);
    
    // 如果配置为90度旋转显示，分配额外的旋转缓冲区
    #if (Rotated == USER_DISP_ROT_90)
    rotat_ptr = (uint16_t*)heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(rotat_ptr);
    #endif
    
    // 创建LVGL刷新信号量
    lvgl_flush_semap = xSemaphoreCreateBinary();
    if (!lvgl_flush_semap)
    {
        ESP_LOGE(TAG, "Failed to create LVGL flush semaphore");
        return;
    }

    s_ui_job_queue = xQueueCreate(8, sizeof(app_ui_job_t));
    if (!s_ui_job_queue)
    {
        ESP_LOGE(TAG, "Failed to create UI job queue");
        return;
    }
    
    // 初始化I2C总线（用于触摸控制器）
    i2c_master_Init();
    esp_err_t expander_err = tca9554_init();
    if (expander_err != ESP_OK)
    {
        ESP_LOGE(TAG, "TCA9554 init failed: %s", esp_err_to_name(expander_err));
        io_expander = NULL;
    }
    button_Init();
    xTaskCreatePinnedToCore(example_button_pwr_task, "example_button_pwr_task", 4 * 1024, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(power_Test, "power_Test", 4 * 1024, NULL, 3, NULL, 1);

    
    // 创建LVGL显示缓冲区和显示驱动结构
    static lv_disp_draw_buf_t disp_buf; // LVGL内部图形缓冲区
    static lv_disp_drv_t disp_drv;      // LVGL显示驱动，包含回调函数
    
    ESP_LOGI(TAG, "Initialize LCD RESET GPIO");
    
    // 配置LCD复位引脚
    gpio_config_t gpio_conf = {};
        gpio_conf.intr_type = GPIO_INTR_DISABLE;      // 禁用中断
        gpio_conf.mode = GPIO_MODE_OUTPUT;            // 输出模式
        gpio_conf.pin_bit_mask = ((uint64_t)0X01<<EXAMPLE_PIN_NUM_LCD_RST); // 复位引脚
        gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // 禁用下拉
        gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;    // 启用上拉
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    ESP_LOGI(TAG, "Initialize QSPI bus");
    // 配置QSPI总线参数
    spi_bus_config_t buscfg = {};
        buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;  // 数据引脚0
        buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;  // 数据引脚1
        buscfg.sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK;    // 时钟引脚
        buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;  // 数据引脚2
        buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;  // 数据引脚3
        buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;       // 最大传输大小
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    // 面板IO和面板句柄
    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    
    // 配置SPI面板IO参数
    esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;                  // 片选引脚
        io_config.dc_gpio_num = -1;           // DC引脚（此处未使用）
        io_config.spi_mode = 3;               // SPI模式3
        io_config.pclk_hz = 40 * 1000 * 1000; // 40MHz时钟频率
        io_config.trans_queue_depth = 10;     // 传输队列深度
        io_config.on_color_trans_done = example_notify_lvgl_flush_ready; // 颜色传输完成回调
        io_config.lcd_cmd_bits = 32;          // 命令位数
        io_config.lcd_param_bits = 8;         // 参数位数
        io_config.flags.quad_mode = true;     // 启用四线模式
    
    // 创建SPI面板IO
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &panel_io));
    
    // 配置AXS15231B厂商特定参数
    axs15231b_vendor_config_t vendor_config = {};
        vendor_config.flags.use_qspi_interface = 1;  // 使用QSPI接口
        vendor_config.init_cmds = lcd_init_cmds;    // 初始化命令
        vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]); // 命令数量
    
    // 配置LCD面板参数
    esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;               // 软件控制复位
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; // RGB元素顺序
        panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL; // 每像素位数
        panel_config.vendor_config = &vendor_config;     // 厂商配置
    
    ESP_LOGI(TAG, "Install panel driver");
    // 创建AXS15231B面板驱动
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));
    
    // LCD硬件复位序列
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1)); // 置高
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 0)); // 置低（复位）
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1)); // 释放复位
    vTaskDelay(pdMS_TO_TICKS(30));
    
    // 初始化LCD面板
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    // 初始化LVGL图形库
    lv_init();
    
    // 分配LVGL DMA缓冲区（从DMA能力内存）
    lvgl_dma_buf = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(lvgl_dma_buf);
    
    // 分配LVGL显示缓冲区（从SPIRAM）
    lv_color_t *buffer_1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    lv_color_t *buffer_2 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    assert(buffer_1);
    assert(buffer_2);
    
    // 初始化LVGL显示缓冲区
    lv_disp_draw_buf_init(&disp_buf, buffer_1, buffer_2, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    // 初始化并配置LVGL显示驱动
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;     // 水平分辨率
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;     // 垂直分辨率
    disp_drv.flush_cb = example_lvgl_flush_cb; // 刷新回调函数
    disp_drv.draw_buf = &disp_buf;            // 显示缓冲区
    disp_drv.full_refresh = 1;                // 必须使用全屏刷新
    disp_drv.user_data = panel;               // 用户数据（LCD面板句柄）
    
    // 注册LVGL显示驱动
    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // 创建LVGL系统tick定时器
    esp_timer_create_args_t lvgl_tick_timer_args = {};
        lvgl_tick_timer_args.callback = &example_increase_lvgl_tick; // 回调函数
        lvgl_tick_timer_args.name = "lvgl_tick";                    // 定时器名称
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    // 启动周期性定时器（微秒）
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    // 初始化并配置LVGL输入设备（触摸）
    static lv_indev_drv_t indev_drv;    // LVGL输入设备驱动
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER; // 指针类型输入（触摸）
    indev_drv.read_cb = example_lvgl_touch_cb; // 读取回调函数
    lv_indev_drv_register(&indev_drv);

    // 创建LVGL互斥锁，保护并发访问
    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    
    // 创建LVGL主任务（固定在CPU 0上运行）
    xTaskCreatePinnedToCore(example_lvgl_port_task, "LVGL", 4000, NULL, 4, NULL, 0);
    
    // 创建背光控制任务（固定在CPU 0上运行）
    xTaskCreatePinnedToCore(example_backlight_loop_task, "example_backlight_loop_task", 4 * 1024, NULL, 2, NULL, 0); 
    
    // Launch application flow (login -> quiz)
    if (example_lvgl_lock(-1))
    {
        app_flow_start();
        example_lvgl_unlock();
    }

}

/**
 * @brief LVGL显示刷新回调函数实现
 * @param drv LVGL显示驱动
 * @param area 刷新区域
 * @param color_map 颜色数据
 */
static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    #if (Rotated == USER_DISP_ROT_90)
    // 处理90度旋转显示
    uint32_t index = 0;
    uint16_t *data_ptr = (uint16_t *)color_map;
    for (uint16_t j = 0; j < EXAMPLE_LCD_H_RES; j++)
    {
        for (uint16_t i = 0; i < EXAMPLE_LCD_V_RES; i++)
        {
            // 旋转像素数据
            rotat_ptr[index++] = data_ptr[EXAMPLE_LCD_H_RES * (EXAMPLE_LCD_V_RES - i - 1) + j];              
        }
    }
    #endif
    
    // 获取LCD面板句柄
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    
    // 计算刷新参数
    const int flush_coun = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN); // 刷新次数
    const int offgap = (LCD_NOROT_VRES / flush_coun); // 每次刷新高度
    const int dmalen = (LVGL_DMA_BUFF_LEN / 2); // DMA传输长度
    
    // 初始化刷新坐标
    int offsetx1 = 0;
    int offsety1 = 0;
    int offsetx2 = LCD_NOROT_HRES;
    int offsety2 = offgap;

    // 根据旋转配置选择数据源
    #if (Rotated == USER_DISP_ROT_90)
    uint16_t *map = (uint16_t *)rotat_ptr;
    #else
    uint16_t *map = (uint16_t *)color_map;
    #endif

    // 触发第一次刷新信号量
    xSemaphoreGive(lvgl_flush_semap);
    
    // 分块刷新LCD显示
    for(int i = 0; i < flush_coun; i++)
    {
        // 等待前一次刷新完成
        xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
        
        // 将数据复制到DMA缓冲区
        memcpy(lvgl_dma_buf, map, LVGL_DMA_BUFF_LEN);
        
        // 绘制位图到LCD指定区域
        esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2, offsety2, lvgl_dma_buf);
        
        // 更新下一块区域的坐标
        offsety1 += offgap;
        offsety2 += offgap;
        map += dmalen;
    }
    
    // 等待最后一次刷新完成
    xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
    
    // 通知LVGL刷新已完成
    lv_disp_flush_ready(drv);
}

/**
 * @brief 获取LVGL互斥锁实现
 * @param timeout_ms 超时时间（毫秒）
 * @return 获取成功返回true
 */
static bool example_lvgl_lock(int timeout_ms)
{
    // 转换超时时间为FreeRTOS tick
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;       
}

/**
 * @brief 释放LVGL互斥锁实现
 */
static void example_lvgl_unlock(void)
{
    assert(lvgl_mux && "bsp_display_start must be called first");
    xSemaphoreGive(lvgl_mux);
}

/**
 * @brief LVGL主任务实现
 * @param arg 任务参数
 */
void example_lvgl_port_task(void *arg)
{
    // 初始化任务延迟时间
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    
    for(;;)
    {
        // 获取LVGL互斥锁
        if (example_lvgl_lock(-1)) 
        {
            example_process_ui_jobs();
            // 处理LVGL定时器和事件
            task_delay_ms = lv_timer_handler();
            
            // 释放互斥锁
            example_lvgl_unlock();
        }
        
        // 确保任务延迟在合理范围内
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        } 
        else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        
        // 任务延迟
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/**
 * @brief LVGL触摸回调函数实现
 * @param drv LVGL输入设备驱动
 * @param data 触摸数据
 */
static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    // 触摸控制器读取命令
    uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buff[32] = {0};  // 接收触摸数据的缓冲区
    
    memset(buff, 0, 32);  // 清空缓冲区
    
    // 通过I2C读取触摸数据
    if (!disp_touch_dev_handle)
    {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    esp_err_t touch_err = i2c_master_touch_write_read(disp_touch_dev_handle, read_touchpad_cmd, 11, buff, 32);
    if (touch_err != ESP_OK)
    {
        // I2C read failed; report release state to avoid bogus coordinates
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    
    // 解析触摸坐标
    uint16_t pointX;
    uint16_t pointY;
    pointX = (((uint16_t)buff[2] & 0x0f) << 8) | (uint16_t)buff[3];
    pointY = (((uint16_t)buff[4] & 0x0f) << 8) | (uint16_t)buff[5];
    
    // 判断触摸状态
    if (buff[1] > 0 && buff[1] < 5)  // 检测到触摸
    {
        data->state = LV_INDEV_STATE_PR;  // 按下状态
        
        #if (Rotated == USER_DISP_ROT_NONO)
        // 无旋转处理
        if (pointX >= EXAMPLE_LCD_V_RES) pointX = EXAMPLE_LCD_V_RES - 1;
        if (pointY >= EXAMPLE_LCD_H_RES) pointY = EXAMPLE_LCD_H_RES - 1;
        data->point.x = pointY;
        data->point.y = (EXAMPLE_LCD_V_RES - 1 - pointX);
        #else
        // 有旋转处理
        if (pointX >= EXAMPLE_LCD_H_RES) pointX = EXAMPLE_LCD_H_RES - 1;
        if (pointY >= EXAMPLE_LCD_V_RES) pointY = EXAMPLE_LCD_V_RES - 1;
        data->point.x = (EXAMPLE_LCD_H_RES - 1 - pointX);
        data->point.y = (EXAMPLE_LCD_V_RES - 1 - pointY);
        #endif
    }
    else  // 未检测到触摸
    {
        data->state = LV_INDEV_STATE_REL;  // 释放状态
    }
}

/**
 * @brief 背光控制循环任务实现
 * @param arg 任务参数
 */
static void example_backlight_loop_task(void *arg)
{
    for(;;)
    {
        #if (Backlight_Testing == true)
        // 背光测试模式：循环切换不同亮度
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_255);  // 最大亮度
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_175);  // 中等亮度
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_125);  // 低亮度
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_0);    // 关闭
        #else
        // 正常模式：保持当前亮度
        vTaskDelay(pdMS_TO_TICKS(2000));
        #endif
    }
}
