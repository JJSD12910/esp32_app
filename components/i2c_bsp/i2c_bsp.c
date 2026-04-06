#include <stdio.h>
#include <stdlib.h>
#include "i2c_bsp.h"
#include "user_config.h"
#include "freertos/FreeRTOS.h"

static i2c_master_bus_handle_t user_i2c_port0_handle = NULL;
static i2c_master_bus_handle_t user_i2c_port1_handle = NULL;
i2c_master_dev_handle_t disp_touch_dev_handle = NULL;
i2c_master_dev_handle_t rtc_dev_handle = NULL;
i2c_master_dev_handle_t imu_dev_handle = NULL;


static uint32_t i2c_data_pdMS_TICKS = 0;
static uint32_t i2c_done_pdMS_TICKS = 0;

static i2c_master_bus_handle_t i2c_get_bus_for_device(i2c_master_dev_handle_t dev_handle)
{
  if (dev_handle == disp_touch_dev_handle)
  {
    return user_i2c_port1_handle;
  }

  if (dev_handle == rtc_dev_handle || dev_handle == imu_dev_handle)
  {
    return user_i2c_port0_handle;
  }

  return NULL;
}


void i2c_master_Init(void)
{
  i2c_data_pdMS_TICKS = pdMS_TO_TICKS(5000);
  i2c_done_pdMS_TICKS = pdMS_TO_TICKS(1000);
  /*i2c_port 0 init*/
  i2c_master_bus_config_t i2c_bus_config = 
  {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = ESP_SCL_NUM,
    .sda_io_num = ESP_SDA_NUM,
    .glitch_ignore_cnt = 7,
    .flags = {
      .enable_internal_pullup = true,
    },
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port0_handle));
  i2c_bus_config.scl_io_num = Touch_SCL_NUM;
  i2c_bus_config.sda_io_num = Touch_SDA_NUM;
  i2c_bus_config.i2c_port = I2C_NUM_1;
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port1_handle));
  
  i2c_device_config_t dev_cfg = 
  {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .scl_speed_hz = 300000,
  };
  dev_cfg.device_address = EXAMPLE_RTC_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &rtc_dev_handle));

  dev_cfg.device_address = EXAMPLE_IMU_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &imu_dev_handle));

  dev_cfg.device_address = EXAMPLE_PIN_NUM_TOUCH_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port1_handle, &dev_cfg, &disp_touch_dev_handle));

}

esp_err_t i2c_writr_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,uint8_t len)
{
  esp_err_t ret;
  uint8_t *pbuf = NULL;
  i2c_master_bus_handle_t bus_handle = i2c_get_bus_for_device(dev_handle);
  if (!bus_handle)
  {
    return ESP_ERR_INVALID_STATE;
  }

  ret = i2c_master_bus_wait_all_done(bus_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  if(reg == -1)
  {
    ret = i2c_master_transmit(dev_handle,buf,len,i2c_data_pdMS_TICKS);
  }
  else
  {
    pbuf = (uint8_t*)malloc(len+1);
    if (!pbuf)
    {
      return ESP_ERR_NO_MEM;
    }
    pbuf[0] = reg;
    for(uint8_t i = 0; i<len; i++)
    {
      pbuf[i+1] = buf[i];
    }
    ret = i2c_master_transmit(dev_handle,pbuf,len+1,i2c_data_pdMS_TICKS);
    free(pbuf);
    pbuf = NULL;
  }
  return ret;
}
esp_err_t i2c_master_write_read_dev(i2c_master_dev_handle_t dev_handle,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen)
{
  esp_err_t ret;
  i2c_master_bus_handle_t bus_handle = i2c_get_bus_for_device(dev_handle);
  if (!bus_handle)
  {
    return ESP_ERR_INVALID_STATE;
  }

  ret = i2c_master_bus_wait_all_done(bus_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  ret = i2c_master_transmit_receive(dev_handle,writeBuf,writeLen,readBuf,readLen,i2c_data_pdMS_TICKS);
  return ret;
}
esp_err_t i2c_read_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,uint8_t len)
{
  esp_err_t ret;
  uint8_t addr = 0;
  i2c_master_bus_handle_t bus_handle = i2c_get_bus_for_device(dev_handle);
  if (!bus_handle)
  {
    return ESP_ERR_INVALID_STATE;
  }

  ret = i2c_master_bus_wait_all_done(bus_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  if( reg == -1 )
  {ret = i2c_master_receive(dev_handle, buf,len, i2c_data_pdMS_TICKS);}
  else
  {addr = (uint8_t)reg; ret = i2c_master_transmit_receive(dev_handle,&addr,1,buf,len,i2c_data_pdMS_TICKS);}
  return ret;
}


esp_err_t i2c_master_touch_write_read(i2c_master_dev_handle_t dev_handle,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen)
{
  esp_err_t ret;
  i2c_master_bus_handle_t bus_handle = i2c_get_bus_for_device(dev_handle);
  if (!bus_handle)
  {
    return ESP_ERR_INVALID_STATE;
  }

  ret = i2c_master_bus_wait_all_done(bus_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  ret = i2c_master_transmit_receive(dev_handle,writeBuf,writeLen,readBuf,readLen,i2c_data_pdMS_TICKS);
  return ret;
}
