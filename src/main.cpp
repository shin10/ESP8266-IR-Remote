#include <LittleFS.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <DoubleResetDetector.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#define MQTT_MAX_PACKET_SIZE 1024
#include <PubSubClient.h>
#include <WiFiManager.h>

// OTA
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// webserver
#include <ESP8266WebServer.h>
#include <FS.h> // has to be imported before <detail\RequestHandlersImpl.h>
#include <detail\RequestHandlersImpl.h>

#include "services/led/led-service.h"

char clientName[32];

#define DRD_TIMEOUT 4
#define DRD_ADDRESS 0

DoubleResetDetector drd = DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);

DynamicJsonDocument config(1024);

unsigned long requestCurrentTime = millis();
unsigned long requestPreviousTime = 0;
const long requestTimeoutTime = 2000;

LEDService ledService;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer server(80);
String header;

const uint16_t PIN_BUZZER = D6;
const uint16_t NOTE_ON = LOW;
const uint16_t NOTE_OFF = HIGH;
const uint16_t PIN_IR_LED = D2;

IRsend irsend(PIN_IR_LED);

bool shouldSaveConfig = false;
void saveConfigCallback()
{
    shouldSaveConfig = true;
}

void notFound();
void handleUnknown();

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    Serial.println("\nincoming message");
    Serial.println(topic);
    Serial.println(length);

    const char *MQTT_TOPIC_COMMAND = config["mqttTopicCommand"];
    const char *MQTT_TOPIC_BUZZER = config["mqttTopicBuzzer"];

    String _msg;
    for (unsigned int i = 0; i < length; ++i)
    {
        char tmp = char(payload[i]);
        _msg += tmp;
    }
    Serial.println(_msg);

    char json[length];
    _msg.toCharArray(json, length + 1);
    StaticJsonDocument<MQTT_MAX_PACKET_SIZE> doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error)
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_BUZZER) == 0)
    {
        digitalWrite(PIN_BUZZER, NOTE_ON);

        JsonArray buzzSequence = doc["buzz"].as<JsonArray>();
        for (JsonVariant v : buzzSequence)
        {
            int frequency = v["f"].as<int>();
            if (frequency == 0)
            {
                digitalWrite(PIN_BUZZER, NOTE_OFF);
            }
            else
            {
                tone(PIN_BUZZER, frequency);
            }
            int duration = v["d"].as<int>();
            delay(duration);
        }

        digitalWrite(PIN_BUZZER, NOTE_OFF);
    }

    if (strcmp(topic, MQTT_TOPIC_COMMAND) == 0)
    {
        uint16_t address = doc["addr"];
        uint16_t command = doc["cmd"];

        irsend.sendPioneer(irsend.encodePioneer(address, command), kPioneerBits >> (address ? 0 : 1), 1);
        delay(1000);
    }
}

DynamicJsonDocument readConfig()
{
    DynamicJsonDocument docLoad(512);
    Serial.println("mounting FS...");

    if (!LittleFS.begin())
    {
        Serial.println("An Error has occurred while mounting LittleFS");
        return docLoad;
    }

    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json"))
    {
        Serial.println("reading config file");
        File configFile = LittleFS.open("/config.json", "r");
        if (configFile)
        {
            Serial.println("opened config file");
            DeserializationError error = deserializeJson(docLoad, configFile);

            if (!error)
            {
                Serial.println("config:");
                serializeJson(docLoad, Serial);
                Serial.println("");
            }
            else
            {
                Serial.println("failed to load json config");
            }
            configFile.close();
        }
    }
    return docLoad;
}

void setupOTA()
{
    ArduinoOTA.onStart([]()
                       {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type); });
    ArduinoOTA.onEnd([]()
                     { Serial.println("\nEnd"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
    ArduinoOTA.onError([](ota_error_t error)
                       {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    } });
    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);

    sprintf(clientName, "ESP-IR-Remote_%X", ESP.getChipId());
    Serial.println("booting...");
    Serial.println(clientName);
    // delay(500); // note for other projects
    // Serial.swap();
    // delay(500);

    // alternative:
    // Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY, 2); // (TX to GPIO2; this however is the builtin led :| )

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, NOTE_OFF);

    ledService.setup();
    irsend.begin();

    config = readConfig();

    if (!config["name"] || !config["name"].as<String>().length())
        config["name"] = clientName;
    else
        strcpy(clientName, config["name"].as<String>().c_str());
    if (!config["mqttBroker"] || !config["mqttBroker"].as<String>().length())
        config["mqttBroker"] = "local.broker";
    if (!config["mqttPort"] || !config["mqttPort"].as<String>().length())
        config["mqttPort"] = 1883;
    if (!config["mqttPortWS"] || !config["mqttPortWS"].as<String>().length())
        config["mqttPortWS"] = 9001;
    if (!config["mqttTopicBuzzer"] || !config["mqttTopicBuzzer"].as<String>().length())
        config["mqttTopicBuzzer"] = "home/tv-remote/buzzer";
    if (!config["mqttTopicCommand"] || !config["mqttTopicCommand"].as<String>().length())
        config["mqttTopicCommand"] = "home/tv-remote/command";

    wifi_station_set_hostname(clientName);
    WiFiManager wifiManager;

    wifiManager.setSaveConfigCallback(saveConfigCallback);

    WiFiManagerParameter custom_name("name", "name", config["name"], 64);
    WiFiManagerParameter custom_mqtt_broker("mqtt-server", "mqtt broker", config["mqttBroker"], 64);
    WiFiManagerParameter custom_mqtt_port("mqtt-port", "mqtt server port", config["mqttPort"], 5);
    WiFiManagerParameter custom_mqtt_port_ws("mqtt-port-ws", "mqtt server port", config["mqttPortWS"], 5);
    WiFiManagerParameter custom_mqtt_topic_buzzer("buzzer-topic", "buzzer topic", config["mqttTopicBuzzer"], 128);
    WiFiManagerParameter custom_mqtt_topic_command("command-topic", "command topic", config["mqttTopicCommand"], 128);

    wifiManager.addParameter(&custom_name);
    wifiManager.addParameter(&custom_mqtt_broker);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_mqtt_port_ws);
    wifiManager.addParameter(&custom_mqtt_topic_buzzer);
    wifiManager.addParameter(&custom_mqtt_topic_command);

    wifiManager.setTimeout(120);

    drd.loop();
    drd.detectDoubleReset();

    if (0 && drd.doubleResetDetected)
    {
        Serial.println("Double Reset Detected");
        wifiManager.resetSettings();

        // config.remove("mqttBroker");
        // config.remove("mqttPort");
        // config.remove("mqttPortWS");
        // config.remove("mqttTopicBuzzer");
        // config.remove("mqttTopicCommand");

        // struct station_config conf;
        // *conf.ssid = 0;
        // *conf.password = 0;

        // // API Reference: wifi_station_disconnect() need to be called after system initializes and the ESP8266 Station mode is enabled.
        // if (WiFi.getMode() & WIFI_STA)
        //     wifi_station_disconnect();

        // ETS_UART_INTR_DISABLE();
        // if (WiFi.persistent)
        // {
        //     wifi_station_set_config(&conf);
        // }
        // else
        // {
        //     wifi_station_set_config_current(&conf);
        // }

        // ETS_UART_INTR_ENABLE();

        drd.stop();
        drd.detectDoubleReset();

        delay(1000);
        wifiManager.startConfigPortal(clientName);
    }
    else
    {
        wifiManager.autoConnect(clientName);
        setupOTA();
    }

    drd.loop();

    Serial.println("Connected.");

    if (shouldSaveConfig)
    {
        Serial.println("saving config");
        config["name"] = custom_name.getValue();
        config["mqttBroker"] = custom_mqtt_broker.getValue();
        config["mqttPort"] = custom_mqtt_port.getValue();
        config["mqttPortWS"] = custom_mqtt_port_ws.getValue();
        config["mqttTopicBuzzer"] = custom_mqtt_topic_buzzer.getValue();
        config["mqttTopicCommand"] = custom_mqtt_topic_command.getValue();
        serializeJson(config, Serial);

        Serial.println("open config file for writing");
        File configFile = LittleFS.open("/config.json", "w");
        if (!configFile)
        {
            Serial.println("failed to open config file for writing");
        }

        if (!serializeJson(config, configFile))
        {
            Serial.println("error writing config file");
            return;
        }
        Serial.println("config saved");
        configFile.close();
    }

    drd.loop();
    server.begin();

    const char *MQTT_BROKER = config["mqttBroker"];
    const uint16_t MQTT_PORT = config["mqttPort"].as<int>();
    const char *MQTT_TOPIC_COMMAND = config["mqttTopicCommand"];
    const char *MQTT_TOPIC_BUZZER = config["mqttTopicBuzzer"];
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
    mqttClient.setCallback(mqttCallback);

    if (!mqttClient.connected())
    {
        Serial.println("Connecting to MQTT Broker");
        Serial.printf("Client name: %s\n", clientName);
        while (!mqttClient.connected())
        {
            mqttClient.connect(clientName);
            Serial.print(".");
            ledService.toggle();
            ledService.loop();
            delay(100);
        }
        mqttClient.subscribe(MQTT_TOPIC_BUZZER);
        mqttClient.subscribe(MQTT_TOPIC_COMMAND);
        Serial.println(" done");
        ledService.off();
        ledService.loop();
    }

    server.onNotFound(handleUnknown);
    server.serveStatic("/", LittleFS, "/index.html");
}

void notFound()
{
    String HTML = F("<html><head><title>404 Not Found</title></head><body>"
                    "<h1>Not Found</h1>"
                    "<p>The requested URL was not found on this server.</p>"
                    "</body></html>");
    server.send(404, "text/html", HTML);
}

void handleUnknown()
{
    String filename = server.uri();
    File pageFile = LittleFS.open(filename, "r");
    if (pageFile)
    {
        String contentTyp = mime::getContentType(filename);
        server.streamFile(pageFile, contentTyp);
        pageFile.close();
    }
    else
    {
        notFound();
    }
}

void loop()
{
    drd.loop();
    mqttClient.loop();
    ledService.loop();
    server.handleClient();
    ArduinoOTA.handle();
}