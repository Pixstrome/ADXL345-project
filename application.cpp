/*
 * application.cpp  Created on: 29 Apr 2014
 * Copyright (c) 2014 Derek Molloy (www.derekmolloy.ie)
 * Made available for the book "Exploring Raspberry Pi"
 * See: www.exploringrpi.com
 * Licensed under the EUPL V.1.1
 *
 * This Software is provided to You under the terms of the European
 * Union Public License (the "EUPL") version 1.1 as published by the
 * European Union. Any use of this Software, other than as authorized
 * under this License is strictly prohibited (to the extent such use
 * is covered by a right of the copyright holder of this Software).
 *
 * This Software is provided under the License on an "AS IS" basis and
 * without warranties of any kind concerning the Software, including
 * without limitation merchantability, fitness for a particular purpose,
 * absence of defects or errors, accuracy, and non-infringement of
 * intellectual property rights other than copyright. This disclaimer
 * of warranty is an essential part of the License and a condition for
 * the grant of any rights to this Software.
 *
 * For more details, see http://www.derekmolloy.ie/
 */
#include <iostream> //input/output library
#include "ADXL345.h"// ADXL345 accelerometer
#include <sstream> // to build a JSON payload
#include <ctime>// handles timestamps
#include <fstream> // reads sytem files like cpu temperature
#include "mqtt/async_client.h" //MQTT client library for asynchronous communication between the devices
#include <chrono> // library for related funtion like sleep duration
#include <thread> // for thread handling
#include <csignal> // handles all system signals

using namespace std; // standard namespace to simplify code
using namespace exploringRPi; // namespace for the ADXL345 library

// defines the MQTT broker address, client ID, and topic
const string SERVER_ADDRESS("tcp://192.168.135.32:1883");// MQTT broker address (running on vm ip)
const string CLIENT_ID("ADXL345_Publisher"); // client ID for the MQTT connection
const string TOPIC("een1071/CPUTemp"); // MQTT topic changed from "sensor/adxl345" to "een1071/CPUTemp"

// MQTT Authentication credentials (as per assignment sample)
const string MQTT_USERNAME("molloyd");
const string MQTT_PASSWORD("password");

// QoS levels for demonstration
const int QOS0 = 0; // at most one delivery
const int QOS1 = 1; // at least one delivery
const int QOS2 = 2; // exactly one delivery

// global flag for graceful shutdown
volatile sig_atomic_t exitFlag = 0;

void signalHandler(int signum) {
    exitFlag = 1;
    cout << "\nInterrupt signal (" << signum << ") received. Shutting down gracefully..." << endl;
}
int main() {
    // register the signal handler for SIGINT (Ctrl+C)
    signal(SIGINT, signalHandler);

   // initializing the ADXL345 accelerometer sensor (using I2C bus 1, device address 0x53)
    ADXL345 sensor(1, 0x53);
    sensor.setResolution(ADXL345::NORMAL); // resolution set to normal
    sensor.setRange(ADXL345::PLUSMINUS_4_G); // set range to +/-4g

    // set up the MQTT client
    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);

    // configure the Last Will message: in the event of an unexpected disconnect,
    // the broker will publish this message to TOPIC.
    string lastWillPayload = "{\"status\":\"offline\"}";
    auto willMsg = mqtt::make_message(TOPIC, lastWillPayload, QOS1, false);

    mqtt::connect_options connOpts;
    connOpts.set_clean_session(true); // for a fresh session change it to false for persistent connections on subscribers
    connOpts.set_keep_alive_interval(20); // interval for 20 secondds
    connOpts.set_will_message(willMsg);// to assign last will message
    connOpts.set_user_name(MQTT_USERNAME); 
    connOpts.set_password(MQTT_PASSWORD);

    // connect to the MQTT broker
    try {
        cout << "Connecting to the MQTT broker..." << flush;
        auto tok = client.connect(connOpts);// connect using specific options
        tok->wait();
        cout << "Connected." << endl;
    }
    catch (const mqtt::exception& exc) {
        cerr << "Error connecting to the broker: " << exc.what() << endl;
        return 1;
    }
      // main loop: read sensor/system data and publish JSON payloads
      int qosCycle = QOS0; // starts with QoS 0 and then cycles through QoS 0,1 and 2
      while (!exitFlag) {
          // update sensor readings
          sensor.readSensorState();// retrive data for each axis (X,Y and Z)
          short ax = sensor.getAccelerationX();
          short ay = sensor.getAccelerationY();
          short az = sensor.getAccelerationZ();
          float pitch = sensor.getPitch(); // orinetation data
          float roll = sensor.getRoll();// orientation data
  
          // get current system time
          time_t now = time(0);
          string timeStr = string(ctime(&now));
          if (!timeStr.empty() && timeStr.back() == '\n') {
              timeStr.pop_back();
          }
  
          // read CPU temperature (assumes Raspberry Pi)
          string cpuTemp;
          ifstream tempFile("/sys/class/thermal/thermal_zone0/temp");// file which has cpu temps
          if (tempFile.is_open()){
              getline(tempFile, cpuTemp);
              tempFile.close();
              try {
                  float tempVal = stof(cpuTemp) / 1000.0f; // convert milli°C to °C
                  cpuTemp = to_string(tempVal); // covert temps back to string for JSON format
              }
              catch(...) {
                  cpuTemp = "N/A"; // if covertion fails then show as not available
              }
          } else {
              cpuTemp = "N/A"; // if file is missing then show as not available
          }
           // read CPU load from /proc/loadavg (first token is the 1-min load average)
        string cpuLoad;
        ifstream loadFile("/proc/loadavg");// load files with avg
        if (loadFile.is_open()){
            getline(loadFile, cpuLoad);// read first line which has avg
            loadFile.close();
            istringstream iss(cpuLoad);// use an input string stream to parse line
            iss >> cpuLoad; // extract first token (that 1 min load avg)
        } else {
            cpuLoad = "N/A"; // mark cpu load not available if file cannot be opened
        }

        // build the JSON payload
        ostringstream payload;
        payload << "{"
                << "\"time\":\"" << timeStr << "\"," // current time stamp
                << "\"acceleration\":{\"x\":" << ax << ",\"y\":" << ay << ",\"z\":" << az << "}," // acceleration data of X,Y and Z axis 
                << "\"orientation\":{\"pitch\":" << pitch << ",\"roll\":" << roll << "}," // orientation pitch and roll
                << "\"cpu\":{\"load\":\"" << cpuLoad << "\",\"temp\":\"" << cpuTemp << "\"}"// system cpu data, load and temp
                << "}";
        string payloadStr = payload.str(); // convert to string and upload in the stream

        // create the MQTT message with a QoS level that cycles through 0, 1, and 2
        auto msg = mqtt::make_message(TOPIC, payloadStr);
        msg->set_qos(qosCycle);
        qosCycle = (qosCycle + 1) % 3; // Cycle: 0, 1, 2

        // publish the message and wait for confirmation upto 10 sec
        try {
            client.publish(msg)->wait_for(chrono::seconds(10));
            cout << "Published (QoS " << msg->get_qos() << "): " << payloadStr << endl;
        }
        catch (const mqtt::exception& exc) {
            cerr << "Error publishing message: " << exc.what() << endl;
        }
           // sleep for 1 second before the next iteration
           this_thread::sleep_for(chrono::seconds(1));
        }
    
        // graceful disconnect
        try {
            cout << "Disconnecting from the MQTT broker..." << flush;
            client.disconnect()->wait(); // intitial disconnect and  wait for completion 
            cout << "Disconnected." << endl;
        }
        catch (const mqtt::exception& exc) {
            cerr << "Error disconnecting: " << exc.what() << endl;
        }
        return 0;
    }
