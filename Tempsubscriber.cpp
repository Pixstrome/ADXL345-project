/*
 * Tempsubscriber.cpp
 *
 * this application subscribes to an MQTT topic ("een1071/CPUTemp") to receive CPU temperature data.
 * when the received CPU temperature (provided as a string payload) exceeds 40°C, it triggers an actuation:
 * the LED connected to GPIO pin 25 is blinked. The application uses wiringPi for GPIO control and the
 * paho MQTT C Client library for MQTT messaging.
 *
 */

 #include <cstdio>        // for standard I/O functions (printf)
 #include <cstdlib>       // for standard library functions (exit, atof)
 #include <cstring>       // for memory functions (memset, memcpy)
 #include "MQTTClient.h"  // paho MQTT C Client library header for MQTT operations
 #include <wiringPi.h>    // wiringPi library for GPIO access
 #include <thread>
 #include <chrono>
 #include <atomic>
 #include <string>
 
 // --- MQTT and Application Constants ---
 
 // MQTT Broker and authentication settings
 #define ADDRESS     "tcp://192.168.135.32:1883"  // MQTT broker address and port
 #define CLIENTID    "Tempsubscriber"            // unique MQTT client identifier
 #define AUTHMETHOD  "molloyd"                   // MQTT username
 #define AUTHTOKEN   "password"                  // MQTT password
 #define TOPIC       "een1071/CPUTemp"           // MQTT topic to subscribe to
 #define QOS         1                           // MQTT Quality of Service level
 #define TIMEOUT     10000L                      // timeout in milliseconds
 
 // GPIO Settings
 
 #define LED_PIN 25
 
 // global variable for tracking MQTT delivery tokens (not strictly used here but provided for completeness)
 volatile MQTTClient_deliveryToken deliveredtoken = 0;
 
 std::atomic<float> currentTemp(0.0);
 
 void delivered(void *context, MQTTClient_deliveryToken dt) {
     printf("Message with token value %d delivery confirmed\n", dt);
     deliveredtoken = dt;
 }
 
 
 int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
     // create a buffer to hold the payload as a null-terminated string
     char payloadBuf[message->payloadlen + 1];
     memset(payloadBuf, 0, sizeof(payloadBuf));                        // clear the buffer
     memcpy(payloadBuf, message->payload, message->payloadlen);          // copy the payload data
     payloadBuf[message->payloadlen] = '\0';                             // ensure null termination
 
     // log the received message details
     printf("Message arrived\n");
     printf("     Topic: %s\n", topicName);
     printf("     Message: %s\n", payloadBuf);
 
     // convert the payload to a std::string for easy manipulation.
    std::string payload(payloadBuf);
 
     
    std::string searchStr = "\"temp\":\"";
    std::size_t pos = payload.find(searchStr);
    float cpuTemp = 0.0;
    if (pos != std::string::npos) {
        pos += searchStr.length(); 
        std::size_t endPos = payload.find("\"", pos);
        if (endPos != std::string::npos) {
            std::string tempStr = payload.substr(pos, endPos - pos);
            cpuTemp = std::atof(tempStr.c_str());
        }
    } else {
        // if "temp" is not found, try converting the whole payload (fallback).
        cpuTemp = std::atof(payload.c_str());
    }
 
     
     currentTemp = cpuTemp; // update the global temperature variable
     // check if the CPU temperature exceeds 40°C; if so, blink the LED.
     
     if (cpuTemp > 40.0) {
         printf("CPU Temperature %.2f exceeds 40°C. CPU too hot!\n", cpuTemp);
         digitalWrite(LED_PIN, HIGH);  // turn the LED on
         delay(500);                   // delay for 500 milliseconds
         digitalWrite(LED_PIN, LOW);   // turn the LED off
     }    else {
        printf("CPU Temperature %.2f°C. CPU okay.\n", cpuTemp);
    }
 
     // free the allocated MQTT message and topic name resources
     MQTTClient_freeMessage(&message);
     MQTTClient_free(topicName);
     return 1;
 }
 
 
 void connlost(void *context, char *cause) { // called when the MQTT connection is lost.
     printf("\nConnection lost\n");
     printf("     Cause: %s\n", cause);
 }
 // this function continuously prints the latest CPU temperature every second.
 void periodicTemperatureDisplay(const std::atomic<bool>& running) {
    while (running.load()) {
        float temp = currentTemp.load();
        if (temp > 40.0) {
            printf("Periodic display: Temperature = %.2f°C -> CPU too hot!\n", temp);
        } else {
            printf("Periodic display: Temperature = %.2f°C -> CPU okay.\n", temp);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
 }
 int main(int argc, char* argv[]) {
     MQTTClient client;
     MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
     int rc;   // variable to hold return codes
     int ch;   // variable to capture user input from the console
     
     // initialize wiringPi for GPIO control; exit if setup fails.
     if (wiringPiSetup() == -1) {
         printf("WiringPi setup failed.\n");
         return 1;
     }
     
     // configure the LED GPIO pin as output and ensure the LED is initially off.
     pinMode(LED_PIN, OUTPUT);
     digitalWrite(LED_PIN, LOW);
     
     // create the MQTT client with no persistence.
     MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
     
     // set the MQTT connection options:
     opts.keepAliveInterval = 20;    // set the keep-alive interval (in seconds)
     opts.cleansession = 1;          // use a clean session (do not retain previous session state)
     opts.username = AUTHMETHOD;     // assign MQTT username
     opts.password = AUTHTOKEN;      // assign MQTT password
     
     // assign MQTT callback functions for connection loss, message arrival, and delivery confirmation.
     MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);
     
     // attempt to connect to the MQTT broker; exit if connection fails.
     if ((rc = MQTTClient_connect(client, &opts)) != MQTTCLIENT_SUCCESS) {
         printf("Failed to connect, return code %d\n", rc);
         exit(-1);
     }
     
     // subscribe to the specified topic and log the connection details.
     printf("Subscribing to topic %s\nfor client %s using QoS %d\n\n"
            "Press Q<Enter> to quit\n\n", TOPIC, CLIENTID, QOS);
     MQTTClient_subscribe(client, TOPIC, QOS);
 
     // set up an atomic flag to control the periodic temperature display thread.
    std::atomic<bool> running(true);
 
    // launch a thread that prints the temperature once every second.
    std::thread displayThread(periodicTemperatureDisplay, std::ref(running));
     
     // main loop: keep running until the user presses 'Q' or 'q'.
     
     do {
         ch = getchar();
     } while(ch != 'Q' && ch != 'q');
     // signal the display thread to exit and join it.
     running = false;
     displayThread.join();
     // disconnect from the MQTT broker and clean up the client resources before exiting.
     MQTTClient_disconnect(client, 10000);
     MQTTClient_destroy(&client);
     
     return rc;
 }
