#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <RCSwitch.h> // library for controling Radio frequency switch
#include <WiFiManager.h>
#include <FS.h>   
#include <ArduinoJson.h>

RCSwitch mySwitch = RCSwitch();

//Do we want to see trace for debugging purposes
#define TRACE 1  // 0= trace off 1 = trace on

// Update these with values suitable for your network.
//#define wifi_ssid "Maragos2Home"
//#define wifi_password "@th@nas1a2093"
//#define mqtt_server "192.168.2.230"

char mqtt_server[40];
char mqtt_port[6] = "1883";

const char* root_topicOut = "home/433toMQTT";
const char* root_topicIn = "home/MQTTto433/";

//#define mqtt_user "your_username" // not compulsory if you set it uncomment line 127 and comment line 129
//#define mqtt_password "your_password" // not compulsory if you set it uncomment line 127 and comment line 129

//adding this to bypass to problem of the arduino builder issue 50
void callback(char*topic, byte* payload,unsigned int length);
WiFiClient espClient;

// client parameters
PubSubClient client(espClient);

bool shouldSaveConfig = false;

//MQTT last attemps reconnection number
long lastReconnectAttempt = 0;

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// Callback function, when the gateway receive an MQTT value on the topics subscribed this function is called
void callback(char* topic, byte* payload, unsigned int length) {
  // In order to republish this payload, a copy must be made
  // as the orignal payload buffer will be overwritten whilst
  // constructing the PUBLISH packet.
  trc("Hey I got a callback ");
  // Allocate the correct amount of memory for the payload copy
  byte* p = (byte*)malloc(length);
  // Copy the payload to the new buffer
  memcpy(p,payload,length);
  
  // Conversion to a printable string
  p[length] = '\0';
  String callbackstring = String((char *) p);
  String topicNameRec = String((char*) topic);
  
  //launch the function to treat received data
  receivingMQTT(topicNameRec,callbackstring);

  // Free the memory
  free(p);
}

void setup()
{
  //Launch serial for debugging purposes
  Serial.begin(9600);
  //Begining wifi connection
  setup_wifi();

  delay(1500);
  lastReconnectAttempt = 0;
  
  mySwitch.enableTransmit(4); // RF Transmitter is connected to Pin D2 
  mySwitch.setRepeatTransmit(20); //increase transmit repeat to avoid lost of rf sendings
  mySwitch.enableReceive(5);  // Receiver on pin D1
}

void setup_wifi(){
  
  
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  
    WiFiManager wifiManager;
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setConfigPortalTimeout(180);
    
    wifiManager.addParameter(&custom_mqtt_server);
    
    wifiManager.autoConnect(ESP.getChipId() + "433Bridge_AP");
  
  
  strcpy(mqtt_server, custom_mqtt_server.getValue());
    
  
  
    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
  
    //read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    
    //save the custom parameters to FS
    if (shouldSaveConfig) {
      Serial.println("saving config");
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["mqtt_server"] = mqtt_server;
      
      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        Serial.println("failed to open config file for writing");
      }
  
      json.printTo(Serial);
      json.printTo(configFile);
      configFile.close();
      //end save
    }
  
    Serial.println("local ip");
    Serial.println(WiFi.localIP());
  
    
    Serial.println(custom_mqtt_server.getValue());
    
    client.setServer( mqtt_server, 1883);
    
    client.setCallback(callback);
    reconnect();
  
    
    
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  
  }

boolean reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    trc("Attempting MQTT connection...");
    // Attempt to connect
    // If you  want to use a username and password, uncomment next line and comment the line if (client.connect("433toMQTTto433")) {
    //if (client.connect("433toMQTTto433", mqtt_user, mqtt_password)) {
    // and set username and password at the program beginning
    if (client.connect("433toMQTTto433c")) {
    // Once connected, publish an announcement...
      client.publish("outTopic","hello world");
      trc("connected");
    //Topic subscribed so as to get data
    String topicNameRec = String("home/MQTTto433/");
    //Subscribing to topic(s)
    subscribing(topicNameRec);
    } else {
      trc("failed, rc=");
      trc(String(client.state()));
      trc(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  return client.connected();
}

void loop()
{
  //MQTT client connexion management
  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      trc("client mqtt not connected, trying to connect");
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // MQTT loop
    client.loop();
  }

  // Receive loop, if data received by RF433 send it by MQTT to MQTTsubject
  if (mySwitch.available()) {
    // Topic on which we will send data
    trc("Receiving 433Mhz signal");
    String MQTTsubject = "home/433toMQTT";
    long MQTTvalue;
    MQTTvalue=mySwitch.getReceivedValue();  
    mySwitch.resetAvailable();
    if (client.connected()) {
      trc("Sending 433Mhz signal to MQTT");
      trc(String(MQTTvalue));
      sendMQTT(MQTTsubject,String(MQTTvalue));
    } else {
      if (reconnect()) {
        sendMQTT(MQTTsubject,String(MQTTvalue));
        lastReconnectAttempt = 0;
      }
    }
  }
}

void subscribing(String topicNameRec){ // MQTT subscribing to topic
  char topicStrRec[26];
  topicNameRec.toCharArray(topicStrRec,26);
  // subscription to topic for receiving data
  boolean pubresult = client.subscribe(topicStrRec);
  if (pubresult) {
    trc("subscription OK to");
    trc(topicNameRec);
  }
}
static char * dec2binWzerofill(unsigned long Dec, unsigned int bitLength) {
  static char bin[64]; 
  unsigned int i=0;

  while (Dec > 0) {
    bin[32+i++] = ((Dec & 1) > 0) ? '1' : '0';
    Dec = Dec >> 1;
  }

  for (unsigned int j = 0; j< bitLength; j++) {
    if (j >= bitLength - i) {
      bin[j] = bin[ 31 + i - (j - (bitLength - i)) ];
    } else {
      bin[j] = '0';
    }
  }
  bin[bitLength] = '\0';
  
  return bin;
}
static const char* bin2tristate(const char* bin) {
  static char returnValue[50];
  int pos = 0;
  int pos2 = 0;
  while (bin[pos]!='\0' && bin[pos+1]!='\0') {
    if (bin[pos]=='0' && bin[pos+1]=='0') {
      returnValue[pos2] = '0';
    } else if (bin[pos]=='1' && bin[pos+1]=='1') {
      returnValue[pos2] = '1';
    } else if (bin[pos]=='0' && bin[pos+1]=='1') {
      returnValue[pos2] = 'F';
    } else {
      return "not applicable";
    }
    pos = pos+2;
    pos2++;
  }
  returnValue[pos2] = '\0';
  return returnValue;
}


void receivingMQTT(String topicNameRec, String callbackstring) {
  trc("Receiving data by MQTT");
  trc(topicNameRec);
  char topicOri[26] = "";
  char topicStrAck[26] = "";
  char datacallback[26] = "";
  
  // Below you send RF signal following data value received by MQTT 
    callbackstring.toCharArray(datacallback,26);
    trc(datacallback);
    long int data = atol(datacallback);
    trc("Send received data by RF 433");
    trc(String(data));
    //send received MQTT value by RF signal (example of signal sent data = 5264660)
    mySwitch.setPulseLength(199);
    mySwitch.send(data, 24);
    
    const char* b = dec2binWzerofill(data, 24);
    trc(String(b));
    mySwitch.sendTriState(bin2tristate( b));
    trc(bin2tristate( b));
}

//send MQTT data dataStr to topic topicNameSend
void sendMQTT(String topicNameSend, String dataStr){

    char topicStrSend[26];
    topicNameSend.toCharArray(topicStrSend,26);
    char dataStrSend[200];
    dataStr.toCharArray(dataStrSend,200);
    boolean pubresult = client.publish(topicStrSend,dataStrSend);
    trc("sending ");
    trc(dataStr);
    trc("to ");
    trc(topicNameSend);

}

void mountfs()
{
   if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
}

//trace
void trc(String msg){
  if (TRACE) {
  Serial.println(msg);
  }
}

