/*
 * app_main.c - ESP32 port of the cooler panel (ESP-IDF 5.x).
 *
 * NOT COMPILED IN THIS REPOSITORY'S CI: no ESP-IDF toolchain was available.
 * The portable core (../../core) is host-tested (make -C firmware/panel test);
 * this file is the integration skeleton: tasks, Wi-Fi/BLE provisioning,
 * local WebSocket API, MQTT cloud link, OTA and warranty upload.
 *
 *   bus task   1 ms: panel_tick(), frames from the 2-wire bus
 *   ui task    display + keys (7-segment/LED driver or LVGL touch screen)
 *   net        BLE provisioning -> Wi-Fi STA; mDNS "_cooler._tcp";
 *              HTTP server with WebSocket /ws (JSON, docs/08);
 *              MQTT/TLS: cooler/<serial>/{state,event,cmd,reply}
 *   OTA        panel: esp_https_ota (A/B partitions, rollback)
 *              drive: download image + signature -> drive_img partition ->
 *              Ed25519 check -> drive_update_start() over the bus
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_mac.h"
#include "esp_random.h"
#include <time.h>
#include "esp_partition.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "sodium.h"
#include "panel.h"
#include "json_api.h"
#include "drive_update.h"
#include "bus_uart.h"

static const char *TAG = "panel";
#define MANUFACTURER_API "https://service.example-cooler.ir/api/v1"   /* placeholder: manufacturer server */
#define MQTT_BROKER      "mqtts://mqtt.example-cooler.ir:8883"       /* placeholder */

static panel_t P;
static SemaphoreHandle_t P_lock;              /* the core is not thread safe: one lock around it */
static esp_mqtt_client_handle_t mqtt;
static httpd_handle_t http;
static char serial[24];
static char token[33];                        /* local API token, created at first provisioning */
static drive_update_t upd;
static const panel_ident_t ID = { serial, "1.0.0" };
/* manufacturer Ed25519 public key for drive images (placeholder, 32 bytes) */
static const uint8_t DRIVE_PUBKEY[32] = {0};

/* ------------------------------------------------------------ events out */
static void publish(const char *suffix, const char *json)
{
    if (!mqtt) return;
    char topic[64];
    snprintf(topic, sizeof topic, "cooler/%s/%s", serial, suffix);
    esp_mqtt_client_publish(mqtt, topic, json, 0, 1, 0);
}
static void ws_broadcast(const char *json);
static void on_event(void *ctx, const char *json)
{
    (void)ctx;
    publish("event", json);
    ws_broadcast(json);
}

/* ------------------------------------------------------------ bus task */
static void bus_task(void *arg)
{
    uint8_t f[256];
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        int n = bus_uart_read_frame(f, sizeof f);
        xSemaphoreTake(P_lock, portMAX_DELAY);
        if (n > 0) panel_rx_frame(&P, f, (uint16_t)n);
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        uint16_t mow = (tm.tm_year > 120) ? (uint16_t)(((tm.tm_wday + 1) % 7) * 1440 + tm.tm_hour * 60 + tm.tm_min) : 0xFFFF;
        panel_tick(&P, (uint32_t)(esp_timer_get_time() / 1000), mow);
        xSemaphoreGive(P_lock);
        vTaskDelayUntil(&last, 1);                /* CONFIG_FREERTOS_HZ=1000 */
    }
}

/* ------------------------------------------------------------ commands from any transport */
static bool verify_drive_image(const uint8_t *img, uint32_t n, const uint8_t *sig)
{
    return crypto_sign_verify_detached(sig, img, n, DRIVE_PUBKEY) == 0;
}

static void start_drive_update(const char *url)
{
    /* download <url> and <url>.sig into the drive_img partition (omitted: esp_http_client
     * read loop + esp_partition_write), then map it and stream it to the drive */
    const esp_partition_t *part = esp_partition_find_first(0x40, ESP_PARTITION_SUBTYPE_ANY, "drive_img");
    if (!part) return;
    const void *map; esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &map, &h) != ESP_OK) return;
    uint32_t size; memcpy(&size, (const uint8_t *)map + part->size - 68, 4);      /* trailer: size + 64-byte sig */
    const uint8_t *sig = (const uint8_t *)map + part->size - 64;
    xSemaphoreTake(P_lock, portMAX_DELAY);
    drive_update_start(&P, &upd, map, size, verify_drive_image, sig);
    xSemaphoreGive(P_lock);
    (void)url;
}

static void handle_command(const char *json, char *reply, size_t cap)
{
    char cmd[16], url[160];
    json_get_str(json, "cmd", cmd, sizeof cmd);
    if (!strcmp(cmd, "ota_panel") && json_get_str(json, "url", url, sizeof url)) {
        esp_http_client_config_t hc = { .url = url, .crt_bundle_attach = esp_crt_bundle_attach };
        esp_https_ota_config_t oc = { .http_config = &hc };
        snprintf(reply, cap, "{\"ok\":1,\"info\":\"panel update started\"}");
        if (esp_https_ota(&oc) == ESP_OK) esp_restart();        /* new image confirms itself after boot */
        snprintf(reply, cap, "{\"ok\":0,\"err\":\"ota failed\"}");
        return;
    }
    if (!strcmp(cmd, "ota_drive") && json_get_str(json, "url", url, sizeof url)) {
        start_drive_update(url);
        snprintf(reply, cap, "{\"ok\":1}");
        return;
    }
    xSemaphoreTake(P_lock, portMAX_DELAY);
    json_handle(&P, json, reply, cap, &ID);
    xSemaphoreGive(P_lock);
    if (!strcmp(cmd, "warranty")) {                              /* also upload it to the manufacturer */
        esp_http_client_config_t hc = { .url = MANUFACTURER_API "/warranty", .method = HTTP_METHOD_POST,
                                        .crt_bundle_attach = esp_crt_bundle_attach };
        esp_http_client_handle_t c = esp_http_client_init(&hc);
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, reply, (int)strlen(reply));
        esp_err_t e = esp_http_client_perform(c);
        int st = esp_http_client_get_status_code(c);
        esp_http_client_cleanup(c);
        ESP_LOGI(TAG, "warranty upload: %s %d", esp_err_to_name(e), st);
    }
}

/* ------------------------------------------------------------ local WebSocket API */
static int ws_fds[4] = {-1, -1, -1, -1};
static void ws_broadcast(const char *json)
{
    if (!http) return;
    httpd_ws_frame_t fr = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)json, .len = strlen(json) };
    for (int i = 0; i < 4; i++) if (ws_fds[i] >= 0) httpd_ws_send_frame_async(http, ws_fds[i], &fr);
}
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ESP_OK;                  /* handshake */
    uint8_t buf[512];
    httpd_ws_frame_t fr = { .payload = buf };
    if (httpd_ws_recv_frame(req, &fr, sizeof buf - 1) != ESP_OK) return ESP_FAIL;
    buf[fr.len] = 0;
    char tok[40], reply[2048];
    /* every message carries the token handed to the phone during provisioning */
    if (!json_get_str((char *)buf, "token", tok, sizeof tok) || strcmp(tok, token)) {
        snprintf(reply, sizeof reply, "{\"ok\":0,\"err\":\"auth\"}");
    } else {
        int fd = httpd_req_to_sockfd(req);
        for (int i = 0; i < 4; i++) if (ws_fds[i] == fd || ws_fds[i] < 0) { ws_fds[i] = fd; break; }
        handle_command((char *)buf, reply, sizeof reply);
    }
    httpd_ws_frame_t out = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)reply, .len = strlen(reply) };
    return httpd_ws_send_frame(req, &out);
}
static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&http, &cfg) != ESP_OK) return;
    httpd_uri_t ws = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true };
    httpd_register_uri_handler(http, &ws);
    mdns_init();
    mdns_hostname_set(serial);
    mdns_service_add(NULL, "_cooler", "_tcp", 80, NULL, 0);
}

/* ------------------------------------------------------------ MQTT */
static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    if (id == MQTT_EVENT_CONNECTED) {
        char t[64]; snprintf(t, sizeof t, "cooler/%s/cmd", serial);
        esp_mqtt_client_subscribe(mqtt, t, 1);
    } else if (id == MQTT_EVENT_DATA && e->data_len < 512) {
        char js[512], reply[2048];
        memcpy(js, e->data, e->data_len); js[e->data_len] = 0;
        handle_command(js, reply, sizeof reply);
        publish("reply", reply);
    }
}
static void state_push_task(void *arg)
{
    char js[1024];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        xSemaphoreTake(P_lock, portMAX_DELAY);
        json_state(&P, js, sizeof js);
        xSemaphoreGive(P_lock);
        publish("state", js);
        ws_broadcast(js);
    }
}

/* ------------------------------------------------------------ Wi-Fi + provisioning */
static void net_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        P.wifi_connected = true;
        http_start();
        esp_mqtt_client_config_t mc = { .broker.address.uri = MQTT_BROKER,
                                        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
                                        .credentials.username = serial };
        mqtt = esp_mqtt_client_init(&mc);
        esp_mqtt_client_register_event(mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL);
        esp_mqtt_client_start(mqtt);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        P.wifi_connected = false;
        esp_wifi_connect();
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_ota_mark_app_valid_cancel_rollback();                    /* reached main: keep this panel image */
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(serial, sizeof serial, "cooler-%02x%02x%02x", mac[3], mac[4], mac[5]);
    nvs_handle_t nv;
    nvs_open("panel", NVS_READWRITE, &nv);
    size_t tl = sizeof token;
    if (nvs_get_str(nv, "token", token, &tl) != ESP_OK) {
        uint8_t r[16]; esp_fill_random(r, sizeof r);
        for (int i = 0; i < 16; i++) sprintf(token + 2 * i, "%02x", r[i]);
        nvs_set_str(nv, "token", token); nvs_commit(nv);
    }
    sodium_init();

    P_lock = xSemaphoreCreateMutex();
    bus_uart_init();
    panel_init(&P, bus_uart_send, on_event, NULL);
    xTaskCreate(bus_task, "bus", 4096, NULL, 10, NULL);
    xTaskCreate(state_push_task, "push", 4096, NULL, 3, NULL);
    /* ui_task: 7-segment/LED + keys (C3 version) or LVGL touch screen (S3 version) - next phase */

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wc);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, net_event, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, net_event, NULL);
    wifi_prov_mgr_config_t pc = { .scheme = wifi_prov_scheme_ble,
                                  .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM };
    wifi_prov_mgr_init(pc);
    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);
    if (!provisioned) {
        /* proof of possession = 6-digit code printed on the panel label; the app reads the
         * local API token over the same secured BLE session (custom endpoint "token") */
        wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, "123456" /* from label */, serial, NULL);
    } else {
        wifi_prov_mgr_deinit();
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        esp_wifi_connect();
    }
}
