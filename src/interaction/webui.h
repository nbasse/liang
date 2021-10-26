#ifndef _WEBUI_H_
#define _WEBUI_H_

#include <ESPAsyncWebServer.h>
#include "Logger.h"
#include "interaction/presenter.h"
#include "operational_modes/operationalmode.h"
#include "logevent.h"

#include "utils.h"
#include "definitions.h"
#include <map>
#include <vector>
#include <ArduinoJson.h>
#include "SPIFFS.h"



class PendingMessage {
    public:
        uint32_t clientId = 0;
        String *msg = nullptr;
};

class WEBUI : public PRESENTER { 
    public:

        WEBUI(AsyncWebServer* server_, AsyncWebSocket* webSocketServer_, ModeSelectEvent modeSelectEvent_);
        void setup();
        void doLoop();
        void SetLogger(LOGGER* logger_);
        void PresentMowerModel(MowerModel* model, bool forceFullPresentation);
        void SendLog(LogEvent *e);


    private:
        bool sendToClients(String prefix, String data, unsigned int minSilentTime = 1000);
        bool sendToClients(String prefix, int data, unsigned int minSilentTime = 1000);
        void wsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);
        void handleNotFound(AsyncWebServerRequest *request);
        //bool enqueueMessage(uint32_t clientId, const String& msg);
        bool hasPrefixSilentTimePassed(String prefix, String data, unsigned int minSilentTime);
        ModeSelectEvent modeSelectEvent;
        AsyncWebSocket* webSocketServer;
        AsyncWebServer* server;
        MowerModel *printedModel;
        std::map<String, unsigned long> prefixSendTimes = std::map<String, unsigned long>();
        bool forceFullPrint = true;
        unsigned long dataThrottleTime = 1000; //Time to wait before sending everchanging data such as voltage or motorloads.
        unsigned long sendTimeForThrottledData = 0;

        LOGGER* logger;
};


#endif