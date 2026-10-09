/*
 * accesubscriber.cpp
 *
 * this application subscribes to an mqtt topic ("een1071/CPUTemp") to receive accelerometer data.
 * when the received accelerometer data (provided as a json payload) shows that the y-axis value has changed 
 * by more than 5 compared to the previous reading, it triggers an actuation:
 * the led connected to gpio pin 29 is blinked. the application uses wiringpi for gpio control and the
 * paho mqtt c client library for mqtt messaging.
 *
 */

 #include "stdio.h"        // for standard i/o functions (printf)
 #include "stdlib.h"       // for standard library functions (exit, atof)
 #include "string.h"       // for memory functions (memset, memcpy)
 #include "MQTTClient.h"   // paho mqtt c client library header for mqtt operations
 #include <wiringPi.h>     // wiringpi library for gpio access
 #include <thread>
 #include <chrono>
 #include <atomic>
 #include <string>
  
  
 #define ADDRESS     "tcp://192.168.135.32:1883"   // mqtt broker address and port
 #define CLIENTID    "Accesubscriber"         // unique mqtt client identifier
 #define AUTHMETHOD  "molloyd"                // mqtt username
 #define AUTHTOKEN   "password"               // mqtt password
 #define TOPIC       "een1071/CPUTemp"         // mqtt topic to subscribe to
 #define QOS         1                        // mqtt quality of service level
 #define TIMEOUT     10000L                   // timeout in milliseconds
  
 // gpio settings
 // (if using wiringpi numbering, ensure the led is wired to the correct pin; 
 // if physical pin 40 is used, wiringpi pin 29 should be used)
 // here we are using gpio pin 29
 #define LED_PIN 29
  
 
 volatile MQTTClient_deliveryToken deliveredtoken = 0;
  
 void delivered(void *context, MQTTClient_deliveryToken dt) { 
     printf("Message with token value %d delivery confirmed\n", dt); 
     deliveredtoken = dt; 
 } 
  
 int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) { 
     int i; 
     char* payloadptr; 
     printf("Message arrived\n"); 
     printf("     topic: %s\n", topicName); 
     printf("     message: "); 
     payloadptr = (char*) message->payload; 
     for(i = 0; i < message->payloadlen; i++) { 
         putchar(*payloadptr++); 
     } 
     putchar('\n'); 
      
     // extract the y-axis value from the json payload
     // the expected json format is:
     // {"time":"...","acceleration":{"x":..., "y":..., "z":...}, ...}
     // we search for the substring "\"y\":" to get the y-axis value
     char *yPtr = strstr((char*) message->payload, "\"y\":");
     if(yPtr != NULL) {
         yPtr += 4; // move past the characters: "y":
         float currentY = atof(yPtr);
         static float previousY = 0.0;
         static int firstReading = 1;
         if (!firstReading) {
             float diff = currentY - previousY;
             if(diff < 0) diff = -diff; // absolute value
             if(diff > 5.0) {
                 printf("y-axis change of %.2f detected (threshold exceeded). blinking led...\n", diff);
                 digitalWrite(LED_PIN, HIGH);  // turn the led on
                 delay(50);                   // delay for 50 milliseconds
                 digitalWrite(LED_PIN, LOW);   // turn the led off
             }
         } else {
             firstReading = 0;
         }
         previousY = currentY;
     } else {
         printf("could not find y-axis value in payload.\n");
     }
  
     MQTTClient_freeMessage(&message); 
     MQTTClient_free(topicName); 
     return 1; 
 } 
  
 void connlost(void *context, char *cause) { // called when the mqtt connection is lost.
     printf("\nConnection lost\n");
     printf("     Cause: %s\n", cause);
 }
  
 int main(int argc, char* argv[]) { 
     MQTTClient client; 
     MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer; 
     int rc; 
     int ch; 
      
     // initialize wiringpi for gpio control; exit if setup fails.
     if (wiringPiSetup() == -1) { 
         printf("WiringPi setup failed.\n"); 
         return 1; 
     }
     // configure the led gpio pin as output and ensure the led is initially off.
     pinMode(LED_PIN, OUTPUT);
     digitalWrite(LED_PIN, LOW);
      
     MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL); 
     opts.keepAliveInterval = 20; 
     opts.cleansession = 1; 
     opts.username = AUTHMETHOD; 
     opts.password = AUTHTOKEN; 
      
     MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered); 
     if ((rc = MQTTClient_connect(client, &opts)) != MQTTCLIENT_SUCCESS) { 
         printf("Failed to connect, return code %d\n", rc); 
         exit(-1); 
     } 
     printf("Subscribing to topic %s\nfor client %s using QoS%d\n\n"
            "Press Q<Enter> to quit\n\n", TOPIC, CLIENTID, QOS); 
     MQTTClient_subscribe(client, TOPIC, QOS); 
      
     do { 
         ch = getchar(); 
     } while(ch != 'Q' && ch != 'q'); 
     MQTTClient_disconnect(client, 10000); 
     MQTTClient_destroy(&client); 
     return rc; 
 }
 