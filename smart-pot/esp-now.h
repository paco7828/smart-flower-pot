#include <esp_now.h>
#include <WiFi.h>

class ESPNow
{
private:
    esp_now_peer_info_t peerInfo{};
    bool isInitialized = false;
    uint8_t receiverMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool isCommandSent = false;

public:
    // Pass receiver mac in constructor
    ESPNow(const uint8_t mac[6])
    {
        memcpy(this->receiverMac, mac, 6);
    }

    // Initialization
    bool init()
    {
        // Set WiFi STA mode
        WiFi.mode(WIFI_STA);

        // Initialization failed
        if (esp_now_init() != ESP_OK)
        {
            this->isInitialized = false;
            return false;
        }

        // Clear peer info
        memset(&peerInfo, 0, sizeof(peerInfo));

        // Register water station peer
        memcpy(peerInfo.peer_addr, this->receiverMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;

        // Failed peer
        if (esp_now_add_peer(&peerInfo) != ESP_OK)
        {
            this->isInitialized = false;
            return false;
        }

        this->isInitialized = true;
        return true;
    }

    // Deinitialization
    void deInit()
    {
        esp_now_deinit();
        this->isInitialized = false;
    }

    // Check initialization state
    bool isReady()
    {
        return this->isInitialized;
    }

    void sendCommand(const char command[])
    {
        // Not initialized
        if (!isReady())
            return;

        // Attempt command sending
        esp_err_t result = esp_now_send(this->receiverMac, (const uint8_t *)command, strlen(command));

        // Send failed
        if (result != ESP_OK)
        {
            this->isCommandSent = false;
        }
        // Send successful
        else
        {
            this->isCommandSent = true;
        }
    }

    // Check command send state
    bool commandSendOk()
    {
        return this->isCommandSent
    }

    // Set new mac address after constructor
    void setReceiverMac(const uint8_t newMacAddr[6])
    {
        memcpy(this->receiverMac, newMacAddr, 6);
    }
};