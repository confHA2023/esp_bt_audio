/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"

#include "bt_app_core.h"
#include "bt_app_av.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include <xtensa_api.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
#include "driver/dac_continuous.h"
#else
#include "driver/i2s_std.h"
#endif

#include "sys/lock.h"

#define APP_RC_CT_TL_GET_CAPS            (0)
#define APP_RC_CT_TL_GET_META_DATA       (1)
#define APP_RC_CT_TL_RN_TRACK_CHANGE     (2)
#define APP_RC_CT_TL_RN_PLAYBACK_CHANGE  (3)
#define APP_RC_CT_TL_RN_PLAY_POS_CHANGE  (4)

#define APP_DELAY_VALUE  50
#define MAX_AUDIO_BUFFER_SIZE 2048

extern void bt_av_hdl_a2d_evt(uint16_t event, void *p_param);
extern void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param);
extern void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param);

/*static const char *BT_AV_TAG = "BT_AV";
static const char *BT_RC_CT_TAG = "RC_CT";
static const char *BT_RC_TG_TAG = "RC_TG";*/

static uint32_t s_pkt_cnt = 0;
static esp_avrc_rn_evt_cap_mask_t s_avrc_peer_rn_cap;

#ifndef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
i2s_chan_handle_t tx_chan = NULL;
#else
dac_continuous_handle_t tx_chan;
#endif

static void bt_app_alloc_meta_buffer(esp_avrc_ct_cb_param_t *param) {
    uint8_t *attr_text = malloc(param->meta_rsp.attr_length + 1);
    if (attr_text) {
        memcpy(attr_text, param->meta_rsp.attr_text, param->meta_rsp.attr_length);
        attr_text[param->meta_rsp.attr_length] = '\0';
        param->meta_rsp.attr_text = attr_text;
    }
}

/*static void bt_av_new_track(void) {
    uint8_t attr_mask = ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST |
                        ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_GENRE;
    esp_avrc_ct_send_metadata_cmd(APP_RC_CT_TL_GET_META_DATA, attr_mask);

    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap, ESP_AVRC_RN_TRACK_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_TRACK_CHANGE, ESP_AVRC_RN_TRACK_CHANGE, 0);
    }
}

static void bt_av_playback_changed(void) {
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap, ESP_AVRC_RN_PLAY_STATUS_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAYBACK_CHANGE, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
    }
}

static void bt_av_play_pos_changed(void) {
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap, ESP_AVRC_RN_PLAY_POS_CHANGED)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAY_POS_CHANGE, ESP_AVRC_RN_PLAY_POS_CHANGED, 10);
    }
}*/

void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len) {
    if (!data || len == 0 || len > MAX_AUDIO_BUFFER_SIZE) return;
    write_ringbuf(data, len);
    if (++s_pkt_cnt % 100 == 0) {
        ESP_LOGI(BT_AV_TAG, "Audio packet count: %"PRIu32, s_pkt_cnt);
    }
}

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
        case ESP_A2D_AUDIO_STATE_EVT:
        case ESP_A2D_AUDIO_CFG_EVT:
        case ESP_A2D_PROF_STATE_EVT:
        case ESP_A2D_SNK_PSC_CFG_EVT:
        case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
        case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
            bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param, sizeof(esp_a2d_cb_param_t), NULL);
            break;
        default:
            ESP_LOGE(BT_AV_TAG, "Invalid A2DP event: %d", event);
            break;
    }
}

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param) {
    if (event == ESP_AVRC_CT_METADATA_RSP_EVT) bt_app_alloc_meta_buffer(param);

    switch (event) {
        case ESP_AVRC_CT_METADATA_RSP_EVT:
        case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
        case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
            bt_app_work_dispatch(bt_av_hdl_avrc_ct_evt, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);
            break;
        default:
            ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
            break;
    }
}

void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param) {
    switch (event) {
        case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
        case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
        case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
        case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        case ESP_AVRC_TG_SET_PLAYER_APP_VALUE_EVT:
            bt_app_work_dispatch(bt_av_hdl_avrc_tg_evt, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL);
            break;
        default:
            ESP_LOGE(BT_RC_TG_TAG, "Invalid AVRC event: %d", event);
            break;
    }
}

void bt_av_hdl_a2d_evt(uint16_t event, void *p_param) {
    ESP_LOGI(BT_AV_TAG, "bt_av_hdl_a2d_evt called with event: %d", event);
    // Ajoute ici ton traitement spécifique de l’événement A2DP si nécessaire
}

void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param) {
    ESP_LOGI(BT_RC_CT_TAG, "bt_av_hdl_avrc_ct_evt called with event: %d", event);
    // Ajoute ici ton traitement spécifique AVRCP Controller si nécessaire
}

void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param) {
    ESP_LOGI(BT_RC_TG_TAG, "bt_av_hdl_avrc_tg_evt called with event: %d", event);
    // Ajoute ici ton traitement spécifique AVRCP Target si nécessaire
}
