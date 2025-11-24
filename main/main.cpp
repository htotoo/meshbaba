
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "esp_random.h"
#include "MtCompact.hpp"
#include "mykey.hpp"

static const char* TAG = "MeshBaba";
static const char* ssid = "MeshBaba";
static const char* password = "1234Mesh";

extern "C" {
void app_main();
}

Radio_PINS radio_pins = {9, 11, 10, 8, 14, 12, 13};  // Default radio pins for Heltec WSL V3.
LoraConfig lora_config = {
    /*.frequency = */ 869.525,   // config
    /*.bandwidth = */ 250,       // config
    /*.spreading_factor = */ 9,  // config
    /*.coding_rate = */ 5,       // config
    /*.sync_word = */ 0x2b,
    /*.preamble_length = */ 16,
    /*.output_power = */ 22,  // config
    /*.tcxo_voltage = */ 1.8,
    /*.use_regulator_ldo = */ false,
};  // default LoRa configuration for EU MFFAST 868
MtCompact mtCompact;

bool needsPongReply(std::string& msg) {
    std::string lower_msg = msg;
    for (char& c : lower_msg) {
        c = tolower(c);
    }
    return (lower_msg.rfind("ping", 0) == 0 ||
            lower_msg.rfind("test", 0) == 0 ||
            lower_msg.rfind("teszt", 0) == 0);
}

bool needsSeqReply(std::string& msg) {
    std::string lower_msg = msg;
    for (char& c : lower_msg) {
        c = tolower(c);
    }
    return (lower_msg.rfind("seq ", 0) == 0);
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, ssid);
    wifi_config.ap.ssid_len = strlen(ssid);
    strcpy((char*)wifi_config.ap.password, password);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen(password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi AP initialized. SSID: %s, Password: %s", ssid, password);

    ESP_LOGI(TAG, "Loading radio config.");

    mtCompact.loadNodeDb();
    mtCompact.setOkToMqtt(true);
    ESP_LOGI(TAG, "Radio initializing...");
    mtCompact.RadioInit(RadioType::SX1262, radio_pins, lora_config);
    ESP_LOGI(TAG, "Radio initialized.");
    mtCompact.setAutoFullNode(true);  // we don't want to be a full node
    mtCompact.setSendHopLimit(7);     // max hop limit
    mtCompact.setStealthMode(false);  // stealth mode, we don't
    mtCompact.setSendEnabled(true);   // we want to send packets
    mtCompact.setOnNodeInfoMessage([](MCT_Header& header, MCT_NodeInfo& nodeinfo, bool needReply, bool newNode) {
        if (nodeinfo.role == 0 && newNode) {
            std::string sender = nodeinfo.short_name;
            std::string reply = sender + "! Kerlek gondold at, hogy biztosan CLIENT role kell-e neked. https://meshtastic.creativo.hu";
            mtCompact.sendTextMessage(reply, header.srcnode, 0, MCT_MESSAGE_TYPE_TEXT, 0, 0, false, 0);
            ESP_LOGI(TAG, "Sent role change suggestion to %s", sender.c_str());
        }
    });

    mtCompact.setOnMessage([](MCT_Header& header, MCT_TextMessage& message) {
        MCT_NodeInfo* nodeinfo = mtCompact.nodeinfo_db.get(header.srcnode);
        std::string sender;
        if (nodeinfo) {
            sender = nodeinfo->short_name;
        } else {
            char hexbuf[11];
            snprintf(hexbuf, sizeof(hexbuf), "0x%08" PRIx32, header.srcnode);
            sender = hexbuf;
        }
        uint32_t replyto = 0xffffffff;
        uint8_t chan = header.chan_hash;
        if (header.dstnode == 0xffffffff) {
            ESP_LOGI(TAG, "Broadcast message from %s: %s", sender.c_str(), message.text.c_str());
            replyto = 0xffffffff;
        } else {
            ESP_LOGI(TAG, "Direct message from %s: %s", sender.c_str(), message.text.c_str());
            replyto = header.srcnode;
            chan = 0;
        }
        if (needsPongReply(message.text)) {
            std::string reply = "Pong! ";
            int8_t hops = header.hop_start - header.hop_limit;
            if (hops > 0) {
                reply += " (hops: " + std::to_string(hops) + ")";
            } else {
                char rssi_buf[16], snr_buf[16];
                snprintf(rssi_buf, sizeof(rssi_buf), "%.1f", header.rssi);
                snprintf(snr_buf, sizeof(snr_buf), "%.1f", header.snr);
                reply += " (rssi: " + std::string(rssi_buf) + " dBm, snr: " + std::string(snr_buf) + " dB)";
            }
            mtCompact.sendTextMessage(reply, replyto, chan, MCT_MESSAGE_TYPE_TEXT, 0, header.packet_id, replyto == 0xffffffff, 0);
            ESP_LOGI(TAG, "Sent pong reply to %s", sender.c_str());
        }
        if (needsSeqReply(message.text)) {
            std::string reply = sender + "! Kerlek ne csinalj csomag tesztet publik csatornan. Mindneki latja!";
            mtCompact.sendTextMessage(reply, replyto, chan, MCT_MESSAGE_TYPE_TEXT, 0, header.packet_id, false, 0);
            ESP_LOGI(TAG, "Sent seq reply to %s", sender.c_str());
        }
    });

    std::string short_name = "Info";                                                                     // short name
    std::string long_name = "Hungarian Info Node";                                                       // long name
    MtCompactHelpers::NodeInfoBuilder(mtCompact.getMyNodeInfo(), 0xabbababa, short_name, long_name, 1);  // random nodeinfo
    mtCompact.getMyNodeInfo()->role = 1;
    uint8_t my_p_key[32] = MYKEY;
    memcpy(mtCompact.getMyNodeInfo()->private_key, my_p_key, 32);
    MtCompactHelpers::RegenerateOrGeneratePrivateKey(*mtCompact.getMyNodeInfo());
    mtCompact.setPrimaryChanByHash(31);
    MtCompactHelpers::PositionBuilder(mtCompact.my_position, 47.486, 19.078, 100);
    uint32_t timer = 0;  // 0.1 second timer
    mtCompact.sendMyNodeInfo();
    while (1) {
        timer++;
        if (timer % (30 * 60 * 10) == 0) {
            mtCompact.sendMyNodeInfo();
        }
        if (mtCompact.nodeinfo_db.needsSave()) {
            mtCompact.saveNodeDb();
            mtCompact.nodeinfo_db.clearChangedFlag();
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // wait 100 milliseconds
    }
}
