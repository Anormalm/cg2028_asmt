/**
  ******************************************************************************
  * @file    vl53l0x_tof.c
  * @author  MCD Application Team
  * @brief   STM32L475E-IOT01 board support package
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "stm32l4xx_hal.h"
#include "vl53l0x_def.h"
#include "vl53l0x_api.h"

#include "vl53l0x_tof.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define I2C_TIME_OUT_BASE   2
#define I2C_TIME_OUT_BYTE   0

#define VL53L0X_OsDelay(...) HAL_Delay(2)
    
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Exported functions --------------------------------------------------------*/
    
int _I2CWrite(VL53L0X_DEV Dev, uint8_t *pdata, uint32_t count) {
    int status;
    int i2c_time_out = I2C_TIME_OUT_BASE+ count* I2C_TIME_OUT_BYTE;

    status = HAL_I2C_Master_Transmit(Dev->I2cHandle, Dev->I2cDevAddr, pdata, count, i2c_time_out);
    
    return status;
}

int _I2CRead(VL53L0X_DEV Dev, uint8_t *pdata, uint32_t count) {
    int status;
    int i2c_time_out = I2C_TIME_OUT_BASE+ count* I2C_TIME_OUT_BYTE;

    status = HAL_I2C_Master_Receive(Dev->I2cHandle, Dev->I2cDevAddr|1, pdata, count, i2c_time_out);
    
    return status;
}

VL53L0X_Error VL53L0X_RdByte(VL53L0X_DEV Dev, uint8_t index, uint8_t *data) {
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    int32_t status_int;

    status_int = _I2CWrite(Dev, &index, 1);
    
    if( status_int ){
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
        goto done;
    }
    
    status_int = _I2CRead(Dev, data, 1);
    
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
    }
done:
    return Status;
}

uint8_t _I2CBuffer[64];


// the ranging_sensor_comms.dll will take care of the page selection
VL53L0X_Error VL53L0X_WriteMulti(VL53L0X_DEV Dev, uint8_t index, uint8_t *pdata, uint32_t count) {
    int status_int;
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    
    if (count > sizeof(_I2CBuffer) - 1) {
        return VL53L0X_ERROR_INVALID_PARAMS;
    }
    
    _I2CBuffer[0] = index;
    memcpy(&_I2CBuffer[1], pdata, count);
    
    status_int = _I2CWrite(Dev, _I2CBuffer, count + 1);
    
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
    }
    
    return Status;
}

// the ranging_sensor_comms.dll will take care of the page selection
VL53L0X_Error VL53L0X_ReadMulti(VL53L0X_DEV Dev, uint8_t index, uint8_t *pdata, uint32_t count) {
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    int32_t status_int;
    
    status_int = _I2CWrite(Dev, &index, 1);
    
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
        goto done;
    }
    
    status_int = _I2CRead(Dev, pdata, count);
    
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
    }
done:
    return Status;
}


VL53L0X_Error VL53L0X_RdWord(VL53L0X_DEV Dev, uint8_t index, uint16_t *data) {
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    int32_t status_int;

    status_int = _I2CWrite(Dev, &index, 1);

    if( status_int ){
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
        goto done;
    }
    
    status_int = _I2CRead(Dev, _I2CBuffer, 2);
    
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
        goto done;
    }

    *data = ((uint16_t)_I2CBuffer[0]<<8) + (uint16_t)_I2CBuffer[1];
done:
    return Status;
}

VL53L0X_Error VL53L0X_RdDWord(VL53L0X_DEV Dev, uint8_t index, uint32_t *data) {
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    int32_t status_int;

    status_int = _I2CWrite(Dev, &index, 1);
    
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
        goto done;
    }
    
    status_int = _I2CRead(Dev, _I2CBuffer, 4);
    
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
        goto done;
    }

    *data = ((uint32_t)_I2CBuffer[0]<<24) + ((uint32_t)_I2CBuffer[1]<<16) + ((uint32_t)_I2CBuffer[2]<<8) + (uint32_t)_I2CBuffer[3];

done:
    return Status;
}

VL53L0X_Error VL53L0X_WrByte(VL53L0X_DEV Dev, uint8_t index, uint8_t data) {
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    int32_t status_int;

    _I2CBuffer[0] = index;
    _I2CBuffer[1] = data;

    status_int = _I2CWrite(Dev, _I2CBuffer, 2);
    
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
    }
    
    return Status;
}

VL53L0X_Error VL53L0X_WrWord(VL53L0X_DEV Dev, uint8_t index, uint16_t data) {
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    int32_t status_int;

    _I2CBuffer[0] = index;
    _I2CBuffer[1] = data >> 8;
    _I2CBuffer[2] = data & 0x00FF;

    status_int = _I2CWrite(Dev, _I2CBuffer, 3);
    
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
    }
    
    return Status;
}

VL53L0X_Error VL53L0X_UpdateByte(VL53L0X_DEV Dev, uint8_t index, uint8_t AndData, uint8_t OrData) {
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    uint8_t data;

    Status = VL53L0X_RdByte(Dev, index, &data);
    
    if (Status) {
        goto done;
    }
    
    data = (data & AndData) | OrData;
    Status = VL53L0X_WrByte(Dev, index, data);
done:
    return Status;
}

VL53L0X_Error VL53L0X_WrDWord(VL53L0X_DEV Dev, uint8_t index, uint32_t data) {
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    int32_t status_int;
    _I2CBuffer[0] = index;
    _I2CBuffer[1] = (data >> 24) & 0xFF;
    _I2CBuffer[2] = (data >> 16) & 0xFF;
    _I2CBuffer[3] = (data >> 8)  & 0xFF;
    _I2CBuffer[4] = (data >> 0 ) & 0xFF;

    status_int = _I2CWrite(Dev, _I2CBuffer, 5);
    if (status_int != 0) {
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;
    }

    return Status;
}

VL53L0X_Error VL53L0X_PollingDelay(VL53L0X_DEV Dev) {
    VL53L0X_Error status = VL53L0X_ERROR_NONE;

    // do nothing
    VL53L0X_OsDelay();
    return status;
}

