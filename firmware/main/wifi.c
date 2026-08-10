/* Prusa-Touch — WiFi STA with NVS-stored credentials + on-device onboarding. */
#include "wifi.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "wifi";
#define NS "pp"
#define KEY_SSID "wifi_ssid"
#define KEY_PASS "wifi_pass"

static volatile bool s_connected;
static volatile bool s_have_creds;
static volatile bool s_ap_active;       /* provisioning hotspot is up */
static char          s_ap_ssid[33];
static char          s_ip[16];          /* dotted IPv4 once STA has an address */
static int s_retries;

#define WIFI_AP_FALLBACK_RETRIES 12     /* STA attempts before raising the hotspot */

/* Load creds: NVS first, else Kconfig (if it was customised — not the placeholder). */
static bool load_creds(char *ssid, size_t ssz, char *pass, size_t psz)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t a = ssz, b = psz;
        esp_err_t e1 = nvs_get_str(h, KEY_SSID, ssid, &a);
        esp_err_t e2 = nvs_get_str(h, KEY_PASS, pass, &b);
        nvs_close(h);
        if (e1 == ESP_OK && e2 == ESP_OK && ssid[0]) {
            return true;
        }
    }
    if (CONFIG_PP_WIFI_SSID[0] && strcmp(CONFIG_PP_WIFI_SSID, "myssid") != 0) {
        strlcpy(ssid, CONFIG_PP_WIFI_SSID, ssz);
        strlcpy(pass, CONFIG_PP_WIFI_PASSWORD, psz);
        return true;
    }
    return false;
}

static void apply_and_connect(const char *ssid, const char *pass)
{
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    /* Accept WPA2 and WPA3 (the test AP "Tore" is WPA3-SAE). */
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    s_retries = 0;
    esp_wifi_disconnect();
    esp_wifi_connect();
}

/* Bring up an open SoftAP "KlipperTouch-XXXX" (192.168.4.1) so a phone/laptop can
 * reach the web UI and configure WiFi. Runs in APSTA so STA keeps retrying. */
static void start_ap(void)
{
    if (s_ap_active) return;
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "KlipperTouch-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;     /* open: easiest provisioning path */

    esp_wifi_set_mode(WIFI_MODE_APSTA);  /* keep STA alive to scan + reconnect */
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    s_ap_active = true;
    ESP_LOGW(TAG, "no network — provisioning hotspot \"%s\" up at http://192.168.4.1", s_ap_ssid);
}

/* Drop the hotspot once we are on a real network. */
static void stop_ap(void)
{
    if (!s_ap_active) return;
    s_ap_active = false;
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "joined network — provisioning hotspot stopped");
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        char ssid[33] = {0}, pass[65] = {0};
        if (load_creds(ssid, sizeof(ssid), pass, sizeof(pass))) {
            s_have_creds = true;
            apply_and_connect(ssid, pass);
        } else {
            /* Nothing to connect to — open the provisioning hotspot immediately. */
            ESP_LOGW(TAG, "no stored credentials — opening provisioning hotspot");
            start_ap();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = '\0';
        if (s_have_creds) {
            s_retries++;
            if (s_retries == WIFI_AP_FALLBACK_RETRIES) start_ap();  /* raise hotspot once */
            esp_wifi_connect();                                     /* keep trying STA */
        } else {
            start_ap();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        s_retries = 0;
        stop_ap();
    }
}

bool wifi_is_connected(void) { return s_connected; }
bool wifi_has_creds(void)    { return s_have_creds; }
bool wifi_is_ap_active(void) { return s_ap_active; }
const char *wifi_ap_ssid(void) { return s_ap_ssid; }
const char *wifi_ip_str(void)  { return s_ip; }

void wifi_init_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();   /* hotspot fallback (192.168.4.1 + DHCP) */

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());   /* STA_START handler attempts connect */
}

int wifi_scan(char ssids[][33], int max)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        return 0;
    }
    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) return 0;
    if (num > WIFI_MAX_SCAN) num = WIFI_MAX_SCAN;
    wifi_ap_record_t *recs = calloc(num, sizeof(wifi_ap_record_t));
    if (!recs) return 0;
    esp_wifi_scan_get_ap_records(&num, recs);

    int out = 0;
    for (int i = 0; i < num && out < max; i++) {
        const char *ss = (const char *)recs[i].ssid;
        if (!ss[0]) continue;
        bool dup = false;                      /* de-dupe repeated SSIDs */
        for (int j = 0; j < out; j++) {
            if (strcmp(ssids[j], ss) == 0) { dup = true; break; }
        }
        if (!dup) strlcpy(ssids[out++], ss, 33);
    }
    free(recs);
    return out;
}

void wifi_save_and_connect(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, KEY_SSID, ssid);
        nvs_set_str(h, KEY_PASS, pass);
        nvs_commit(h);
        nvs_close(h);
    }
    s_have_creds = true;
    ESP_LOGI(TAG, "saved creds for \"%s\", connecting", ssid);
    apply_and_connect(ssid, pass);
}
