#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_err.h"

#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include "presence_detector.h"

#define CSI_MAX_LEN       612
#define CSI_QUEUE_LENGTH  16

static const char *TAG = "DENSEPOSITION";

typedef struct {
    uint16_t len;
    int8_t rssi;
    uint8_t channel;
    uint8_t first_word_invalid;
    uint8_t data[CSI_MAX_LEN];
} csi_sample_t;

static QueueHandle_t s_csi_queue;
static wifi_ap_record_t s_ap_info;

static volatile uint32_t s_queue_drops = 0;
static bool s_csi_started = false;
static bool s_ping_started = false;

/*
 * CSI callback:
 * Runs in the Wi-Fi task.
 * Only copies packet information into a queue.
 */
static void wifi_csi_callback(void *ctx, wifi_csi_info_t *info)
{
    if (info == NULL || info->buf == NULL || s_csi_queue == NULL) {
        return;
    }

    /* Keep CSI received from our connected access point only. */
    if (ctx != NULL && memcmp(info->mac, ctx, 6) != 0) {
        return;
    }

    csi_sample_t sample = {0};

    uint16_t copy_len = info->len;

    if (copy_len > CSI_MAX_LEN) {
        copy_len = CSI_MAX_LEN;
    }

    sample.len = copy_len;
    sample.rssi = info->rx_ctrl.rssi;
    sample.channel = info->rx_ctrl.channel;
    sample.first_word_invalid = info->first_word_invalid;

    memcpy(sample.data, info->buf, copy_len);

    if (xQueueSend(s_csi_queue, &sample, 0) != pdTRUE) {
        s_queue_drops++;
    }
}

/*
 * Consumer task:
 * Processes queued CSI samples outside the Wi-Fi callback.
 *
 * At this stage, mean subcarrier power is only a diagnostic
 * feature. It is not yet a presence or breathing measurement.
 */


#define CSI_EXPORT_BINS 32

/*
 * Export 32 pooled CSI magnitude bands per received frame.
 * Format:
 * CSI,timestamp_us,rssi,channel,pair_count,m0,...,m31
 */
static void emit_csi_dataset_record(const csi_sample_t *sample)
{
    uint16_t start = sample->first_word_invalid ? 4 : 0;

    if (sample->len <= start) {
        return;
    }

    uint16_t pairs = (sample->len - start) / 2;

    char line[384];

    int used = snprintf(
        line,
        sizeof(line),
        "CSI,%lld,%d,%u,%u",
        (long long)esp_timer_get_time(),
        sample->rssi,
        sample->channel,
        pairs
    );

    if (used < 0 || used >= sizeof(line)) {
        return;
    }

    for (uint16_t g = 0; g < CSI_EXPORT_BINS; g++) {
        uint16_t first = (g * pairs) / CSI_EXPORT_BINS;
        uint16_t last = ((g + 1) * pairs) / CSI_EXPORT_BINS;

        float total = 0.0f;
        uint16_t count = 0;

        for (uint16_t b = first; b < last; b++) {
            uint16_t i = start + b * 2;

            int real = (int8_t)sample->data[i + 1];
            int imag = (int8_t)sample->data[i];

            total += sqrtf(
                (float)(real * real + imag * imag)
            );

            count++;
        }

        unsigned int magnitude = count > 0
            ? (unsigned int)(total / count + 0.5f)
            : 0;

        int written = snprintf(
            line + used,
            sizeof(line) - used,
            ",%u",
            magnitude
        );

        if (written < 0 || written >= sizeof(line) - used) {
            return;
        }

        used += written;
    }

    printf("%s\n", line);
    fflush(stdout);
}

static void csi_processing_task(void *arg)
{
    csi_sample_t sample;
    static presence_detector_t detector;

    presence_detector_init(&detector);

    uint32_t frame_count = 0;
    float power_sum = 0.0f;

    int last_rssi = 0;
    uint8_t last_channel = 0;
    uint16_t last_len = 0;

    int64_t report_start = esp_timer_get_time();

    while (true) {
        if (xQueueReceive(
                s_csi_queue,
                &sample,
                pdMS_TO_TICKS(100)
            ) == pdTRUE) {

            uint16_t start =
                sample.first_word_invalid ? 4 : 0;

            uint32_t bins = 0;
            float sample_power = 0.0f;

            for (uint16_t i = start; i + 1 < sample.len; i += 2) {
                int16_t imag = (int8_t)sample.data[i];
                int16_t real = (int8_t)sample.data[i + 1];

                sample_power +=
                    (float)(real * real + imag * imag);

                bins++;
            }

            if (bins > 0) {
                sample_power /= (float)bins;
                power_sum += sample_power;
            }

            presence_detector_update(
                &detector,
                sample.data,
                sample.len,
                sample.first_word_invalid
            );

            emit_csi_dataset_record(&sample);

            frame_count++;
            last_rssi = sample.rssi;
            last_channel = sample.channel;
            last_len = sample.len;
        }

        int64_t now = esp_timer_get_time();
        int64_t elapsed = now - report_start;

        if (elapsed >= 1000000) {
            float rate =
                (float)frame_count * 1000000.0f / (float)elapsed;

            float mean_power = frame_count > 0
                ? power_sum / (float)frame_count
                : 0.0f;

            if (!detector.calibrated) {
                ESP_LOGI(
                    TAG,
                    "CSI %.1f fps | baseline=%u/%u frames | "
                    "RSSI=%d dBm | channel=%u | drops=%" PRIu32,
                    rate,
                    detector.frames_collected,
                    PD_CALIBRATION_FRAMES,
                    last_rssi,
                    last_channel,
                    s_queue_drops
                );
            }
            else {
                ESP_LOGI(
                    TAG,
                    "CSI %.1f fps | PRESENCE=%s | score=%.2f | "
                    "valid_bins=%u | power=%.2f | RSSI=%d dBm | "
                    "channel=%u | length=%u | drops=%" PRIu32,
                    rate,
                    detector.present ? "DETECTED" : "NOT_DETECTED",
                    detector.score,
                    detector.valid_bins,
                    mean_power,
                    last_rssi,
                    last_channel,
                    last_len,
                    s_queue_drops
                );
            }

            frame_count = 0;
            power_sum = 0.0f;
            report_start = now;
        }
    }
}

/*
 * Enable CSI after the station connects to the access point.
 */
static void wifi_csi_start(void)
{
    if (s_csi_started) {
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&s_ap_info));

    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = false,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = true,
        .shift = true,
    };

    ESP_ERROR_CHECK(
        esp_wifi_set_csi_config(&csi_config)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_csi_rx_cb(
            wifi_csi_callback,
            s_ap_info.bssid
        )
    );

    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

    s_csi_started = true;

    ESP_LOGI(
        TAG,
        "CSI enabled. AP BSSID: %02x:%02x:%02x:%02x:%02x:%02x",
        s_ap_info.bssid[0],
        s_ap_info.bssid[1],
        s_ap_info.bssid[2],
        s_ap_info.bssid[3],
        s_ap_info.bssid[4],
        s_ap_info.bssid[5]
    );
}

/*
 * Generate regular packets by pinging the gateway.
 * This encourages the access point to transmit frames
 * from which the ESP32 can collect CSI.
 */
static void wifi_ping_gateway_start(void)
{
    if (s_ping_started) {
        return;
    }

    esp_netif_t *netif =
        esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (netif == NULL) {
        ESP_LOGE(TAG, "Wi-Fi station network interface not found");
        return;
    }

    esp_netif_ip_info_t ip_info;

    ESP_ERROR_CHECK(
        esp_netif_get_ip_info(netif, &ip_info)
    );

    esp_ping_config_t ping_config =
        ESP_PING_DEFAULT_CONFIG();

    ping_config.count = 0;
    ping_config.interval_ms = 100;
    ping_config.task_stack_size = 3072;
    ping_config.data_size = 1;

    ping_config.target_addr.u_addr.ip4.addr =
        ip4_addr_get_u32(&ip_info.gw);

    ping_config.target_addr.type = ESP_IPADDR_TYPE_V4;

    esp_ping_callbacks_t callbacks = {0};
    esp_ping_handle_t ping_handle = NULL;

    ESP_ERROR_CHECK(
        esp_ping_new_session(
            &ping_config,
            &callbacks,
            &ping_handle
        )
    );

    ESP_ERROR_CHECK(esp_ping_start(ping_handle));

    s_ping_started = true;

    ESP_LOGI(
        TAG,
        "Gateway ping started: " IPSTR,
        IP2STR(&ip_info.gw)
    );
}

/*
 * Wi-Fi and IP event handler.
 */
static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {

        ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting...");
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(
            TAG,
            "Connected. IP address: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        wifi_csi_start();
        wifi_ping_gateway_start();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "WiFi DensePosition - ESP32-S3");
    ESP_LOGI(TAG, "Starting single-node CSI sensing firmware");

    if (CONFIG_DENSEPOSITION_WIFI_SSID[0] == '\0') {
        ESP_LOGE(
            TAG,
            "Wi-Fi SSID is empty. Configure it using idf.py menuconfig."
        );
        return;
    }

    s_csi_queue = xQueueCreate(
        CSI_QUEUE_LENGTH,
        sizeof(csi_sample_t)
    );

    if (s_csi_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create CSI queue");
        return;
    }

    BaseType_t task_result = xTaskCreate(
        csi_processing_task,
        "csi_processing",
        8192,
        NULL,
        5,
        NULL
    );

    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CSI processing task");
        return;
    }

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&wifi_init_config)
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config = {0};

    snprintf(
        (char *)wifi_config.sta.ssid,
        sizeof(wifi_config.sta.ssid),
        "%s",
        CONFIG_DENSEPOSITION_WIFI_SSID
    );

    snprintf(
        (char *)wifi_config.sta.password,
        sizeof(wifi_config.sta.password),
        "%s",
        CONFIG_DENSEPOSITION_WIFI_PASSWORD
    );

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "Wi-Fi station started");
}
