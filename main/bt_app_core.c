/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <xtensa_api.h>
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "bt_app_core.h"

#ifdef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
#include "driver/dac_continuous.h"
#else
#include "driver/i2s_std.h"
#endif

#define BT_AV_TAG "BT_AV"
#define MAX_AUDIO_BUFFER_SIZE 2048

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

static QueueHandle_t s_bt_app_task_queue = NULL;  /* handle of work queue */
static TaskHandle_t s_bt_app_task_handle = NULL;  /* handle of application task  */
static RingbufHandle_t s_ringbuf = NULL;          /* ring buffer for audio data */

#ifndef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
extern i2s_chan_handle_t tx_chan;
#else
extern dac_continuous_handle_t tx_chan;
#endif

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

static void bt_app_task_handler(void *arg);
static bool bt_app_send_msg(bt_app_msg_t *msg);
static void bt_app_work_dispatched(bt_app_msg_t *msg);
static void i2s_writer_task(void *arg);

/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/

static bool bt_app_send_msg(bt_app_msg_t *msg) {
    if (!msg) return false;
    if (xQueueSend(s_bt_app_task_queue, msg, 10 / portTICK_PERIOD_MS) != pdTRUE) {
        ESP_LOGE(BT_AV_TAG, "%s xQueue send failed", __func__);
        return false;
    }
    return true;
}

static void bt_app_work_dispatched(bt_app_msg_t *msg) {
    if (msg->cb) msg->cb(msg->event, msg->param);
}

static void bt_app_task_handler(void *arg) {
    bt_app_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_bt_app_task_queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.sig) {
                case BT_APP_SIG_WORK_DISPATCH:
                    bt_app_work_dispatched(&msg);
                    break;
                default:
                    ESP_LOGW(BT_AV_TAG, "%s unhandled signal: %d", __func__, msg.sig);
                    break;
            }
            if (msg.param) free(msg.param);
        }
    }
}

static void i2s_writer_task(void *arg) {
    size_t bytes_written;
    while (1) {
        size_t item_size;
        uint8_t *data = (uint8_t *)xRingbufferReceive(s_ringbuf, &item_size, portMAX_DELAY);
        if (data) {
<<<<<<< Updated upstream
            if (tx_chan) {
                i2s_channel_write(tx_chan, data, item_size, &bytes_written, portMAX_DELAY);
            }
            vRingbufferReturnItem(s_ringbuf, data);
        }
    }
}

static void bt_i2s_task_handler(void *arg)
{
    uint8_t *data = NULL;
    size_t item_size = 0;
    /**
     * The total length of DMA buffer of I2S is:
     * `dma_frame_num * dma_desc_num * i2s_channel_num * i2s_data_bit_width / 8`.
     * Transmit `dma_frame_num * dma_desc_num` bytes to DMA is trade-off.
     */
    const size_t item_size_upto = 240 * 6;
    size_t bytes_written = 0;

    for (;;) {
        if (pdTRUE == xSemaphoreTake(s_i2s_write_semaphore, portMAX_DELAY)) {
            for (;;) {
                item_size = 0;
                /* receive data from ringbuffer and write it to I2S DMA transmit buffer */
                data = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, (TickType_t)pdMS_TO_TICKS(20), item_size_upto);
                if (item_size == 0) {
                    ESP_LOGI(BT_APP_CORE_TAG, "ringbuffer underflowed! mode changed: RINGBUFFER_MODE_PREFETCHING");
                    ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
                    break;
                }

            #ifdef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
                dac_continuous_write(tx_chan, data, item_size, &bytes_written, -1);
            #else
                i2s_channel_write(tx_chan, data, item_size, &bytes_written, portMAX_DELAY);
            #endif
                vRingbufferReturnItem(s_ringbuf_i2s, (void *)data);
            }
=======
#ifdef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
            dac_continuous_write(tx_chan, data, item_size, &bytes_written, -1);
#else
            i2s_channel_write(tx_chan, data, item_size, &bytes_written, portMAX_DELAY);
#endif
            vRingbufferReturnItem(s_ringbuf, data);
>>>>>>> Stashed changes
        }
    }
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback) {
    bt_app_msg_t msg;
    memset(&msg, 0, sizeof(bt_app_msg_t));
    msg.sig = BT_APP_SIG_WORK_DISPATCH;
    msg.event = event;
    msg.cb = p_cback;

    if (param_len == 0) return bt_app_send_msg(&msg);

    if (p_params && param_len > 0) {
        msg.param = malloc(param_len);
        if (msg.param) {
            memcpy(msg.param, p_params, param_len);
            if (p_copy_cback) {
                p_copy_cback(msg.param, p_params, param_len);
            }
            return bt_app_send_msg(&msg);
        }
    }
    return false;
}

void bt_i2s_task_start_up(void) {
    if (!s_ringbuf) {
        s_ringbuf = xRingbufferCreate(8 * 1024, RINGBUF_TYPE_BYTEBUF);
        if (!s_ringbuf) {
            ESP_LOGE(BT_AV_TAG, "Failed to create ring buffer");
<<<<<<< Updated upstream
        }
    }
    // Démarrer la tâche d’écriture I2S ici si nécessaire
    if (!s_i2s_task_handle) {
        xTaskCreatePinnedToCore(i2s_writer_task, "i2s_writer", 4096, NULL, 5, &s_i2s_task_handle, 0);
    }
}

void bt_app_task_start_up(void)
{
    s_bt_app_task_queue = xQueueCreate(10, sizeof(bt_app_msg_t));
    xTaskCreate(bt_app_task_handler, "BtAppTask", 3072, NULL, 10, &s_bt_app_task_handle);
    
    s_ringbuf = xRingbufferCreate(8 * 1024, RINGBUF_TYPE_BYTEBUF); // 8 KB
    if (s_ringbuf == NULL) {
        ESP_LOGE(BT_AV_TAG, "Failed to create ring buffer");
        return;
=======
            return;
        }
        ESP_LOGI(BT_AV_TAG, "Ring buffer created at %p", s_ringbuf);
    }
    xTaskCreate(i2s_writer_task, "i2s_writer", 4096, NULL, 5, NULL);
}

void bt_i2s_task_shut_down(void) {
    if (s_ringbuf) {
        vRingbufferDelete(s_ringbuf);
        s_ringbuf = NULL;
>>>>>>> Stashed changes
    }
}

void bt_app_task_start_up(void) {
    s_bt_app_task_queue = xQueueCreate(10, sizeof(bt_app_msg_t));
    xTaskCreate(bt_app_task_handler, "BtAppTask", 3072, NULL, 10, &s_bt_app_task_handle);
}

void bt_app_task_shut_down(void) {
    if (s_bt_app_task_handle) {
        vTaskDelete(s_bt_app_task_handle);
        s_bt_app_task_handle = NULL;
    }
    if (s_bt_app_task_queue) {
        vQueueDelete(s_bt_app_task_queue);
        s_bt_app_task_queue = NULL;
    }
}

<<<<<<< Updated upstream
/*static void i2s_writer_task(void *arg) {
    while (1) {
        size_t item_size;
        uint8_t *data = (uint8_t *)xRingbufferReceive(s_ringbuf, &item_size, portMAX_DELAY);
        if (data) {
        #ifdef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
            // DAC: push to continuous DAC buffer
            dac_continuous_write(tx_chan, data, item_size, NULL, 0);
        #else
            // I2S: blocking write
            size_t bytes_written = 0;
            esp_err_t err = i2s_channel_write(tx_chan, data, item_size, &bytes_written, portMAX_DELAY);
            if (err != ESP_OK || bytes_written < item_size) {
                ESP_LOGW(BT_AV_TAG, "I2S write warning: %d / %u", (int)bytes_written, (unsigned)item_size);
            }
        #endif
            vRingbufferReturnItem(s_ringbuf, data);
        }
    }
}*/

void bt_i2s_task_shut_down(void) {
    if (s_i2s_task_handle) {
        vTaskDelete(s_i2s_task_handle);
        s_i2s_task_handle = NULL;
    }

    if (s_ringbuf) {
    vRingbufferDelete(s_ringbuf);
    s_ringbuf = NULL;
    }
}


size_t write_ringbuf(const uint8_t *data, size_t len) {
    if (!s_ringbuf) {
        ESP_LOGE(BT_AV_TAG, "Ring buffer not initialized");
        return 0;
    }
    if (!xRingbufferSend(s_ringbuf, data, len, pdMS_TO_TICKS(10))) {
        ESP_LOGW(BT_AV_TAG, "Ring buffer full, dropping packet");
        return 0;
=======
size_t write_ringbuf(const uint8_t *data, size_t size) {
    if (!data || size == 0 || !s_ringbuf) {
        ESP_LOGE(BT_AV_TAG, "Invalid write_ringbuf call: data=%p, size=%u, ringbuf=%p", data, (unsigned)size, s_ringbuf);
        return 0;
    }
    if (!xRingbufferSend(s_ringbuf, data, size, portMAX_DELAY)) {
        ESP_LOGW(BT_AV_TAG, "Failed to send data to ring buffer");
>>>>>>> Stashed changes
    }
    return len;
}
