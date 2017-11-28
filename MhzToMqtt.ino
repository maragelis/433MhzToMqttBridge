#include <FS.h>   
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <RCSwitch.h> // library for controling Radio frequency switch
#include <WiFiManager.h>
#include <ArduinoJson.h>

RCSwitch mySwitch = RCSwitch();

//Do we want to see trace for debugging purposes
#define TRACE 1  // 0= trace off 1 = trace on

// Update these with values suitable for your network.


#define mqtt_server       "192.168.2.230"
#define mqtt_port         "1883"
#define Hostname          "433MhzBridge2"



const char* root_topicOut = "home/433toMQTT";
const char* root_topicIn = "home/MQTTto433";

//#define mqtt_user "your_username" // not compulsory if you set it uncomment line 211 and comment line 213
//#define mqtt_password "your_password" // not compulsory if you set it uncomment line 211 and comment line 213

//adding this to bypass to problem of the arduino builder issue 50
//void callback(char*topic, byte* payload,unsigned int length);

WiFiClient espClient;
// client parameters
PubSubClient client(espClient);


bool shouldSaveConfig = false;
bool ResetConfig = false;

//MQTT last attemps reconnection number
long lastReconnectAttempt = 0;

struct JsonPayload{
  String Topic;
  String getReceivedValue;
  int getReceivedBitlength;
  int getReceivedProtocol;
  int  getReceivedDelay;
} ;


void saveConfigCallback () {
  trc("Should save config");
  shouldSaveConfig = true;
}

struct JsonPayload Decodejson(char* Payload) 
{
 JsonPayload data;
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(Payload);
  if (!root.success()) {
     data = {"","",0,0,0};
    Serial.println("JSON parsing failed!");
    return data;
  } else
 {
   String topic = root["topic"];
   Serial.println(topic);
   String payload1 = root["getReceivedValue"];
   int payload2 = root["getReceivedBitlength"];
   int payload3 = root["getReceivedProtocol"];
   int payload4 = root["getReceivedDelay"];

   Serial.println(payload1);

    JsonPayload data1 = {topic,payload1,payload2,payload3,payload4};
    return data1;
   
    
 }
    
  
  return data;
}

// Callback function, when the gateway receive an MQTT value on the topics subscribed this function is called
void callback(char* topic, byte* payload, unsigned int length) {
  // In order to republish this payload, a copy must be made
  // as the orignal payload buffer will be overwritten whilst
  // constructing the PUBLISH packet.
  trc("Hey I got a callback ");
  
  


  // Conversion to a printable string
  payload[length] = '\0';
  String callbackstring = String((char *) payload);
  String topicNameRec = String((char*) topic);
  
  JsonPayload data = Decodejson((char *) payload);
  Serial.println("JSON Returned! ====");
  Serial.println(data.getReceivedDelay);
  trc("launch the function to treat received data");
  receivingMQTT(topicNameRec,data.getReceivedValue,data.getReceivedDelay);

 
}

void setup()
{
  //Launch serial for debugging purposes
  Serial.begin(115200);
 
  pinMode(0,INPUT);
  pinMode(2,OUTPUT);

  trc("Waiting for pin 2 press");
  delay(10000);
  if (digitalRead(0)==LOW)
  {
    trc("Pin 0 Pressed");
    ResetConfig =true;
  }


  trc("Begining wifi connection");
  //SPIFFS.format();

  trc("Running MountFs");
  mountfs();

  setup_wifi();
  trc("Finnished wifi setup");
  delay(1500);
  lastReconnectAttempt = 0;
  
  mySwitch.enableTransmit(4); // RF Transmitter is connected to Pin D2 
  mySwitch.setRepeatTransmit(20); //increase transmit repeat to avoid lost of rf sendings
  mySwitch.enableReceive(5);  // Receiver on pin D1
  wifi_station_set_hostname( Hostname);

}

void setup_wifi(){
  
    
  
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);

  
    WiFiManager wifiManager;
    if (ResetConfig)
    {
      trc("Resetting wifiManager");
      WiFi.disconnect();
      wifiManager.resetSettings();
    }
   
    
    if (mqtt_server=="" || mqtt_port=="")
    {
      trc("Resetting wifiManager");
      WiFi.disconnect();
      wifiManager.resetSettings();
      ESP.reset();
      delay(1000);
    }
    else
    {
      trc("values ar no null ");
    }


    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setConfigPortalTimeout(180);
    
    
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    

    
    if (!wifiManager.autoConnect("433Bridge_AP", "")) {
      trc("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }
  
  
  
    
  
  
    //if you get here you have connected to the WiFi
    trc("connected...yeey :)");
  
    //read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    
    //save the custom parameters to FS
    if (shouldSaveConfig) {
      trc("saving config");
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["mqtt_server"] = mqtt_server;
      json["mqtt_port"] = mqtt_port;
      
      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        trc("failed to open config file for writing");
      }
  
      json.printTo(Serial);
      json.printTo(configFile);
      configFile.close();
      //end save
    }
  
    Serial.print("local ip : ");
    Serial.println(WiFi.localIP());
  
    
    trc("Setting Mqtt Server values");
    Serial.print("mqtt_server : ");
    trc(mqtt_server);
    Serial.print("mqtt_server_port : ");
    trc(mqtt_port);

    trc("Setting Mqtt Server connection");
    unsigned int mqtt_port_x = atoi (mqtt_port); 
    client.setServer(mqtt_server, mqtt_port_x);
    
    client.setCallback(callback);
     reconnect();
    
    
    trc("");
    trc("WiFi connected");
    trc("IP address: ");
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
    String mqname =  WiFi.macAddress();
    char charBuf[50];
    mqname.toCharArray(charBuf, 50) ;

    if (client.connect(charBuf)) {
    // Once connected, publish an announcement...
      //client.publish(root_topicOut,"connected");
      trc("connected");
    //Topic subscribed so as to get data
    String topicNameRec = root_topicIn;
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
    digitalWrite(2,LOW);
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
    digitalWrite(2,HIGH);
    client.loop();
  }

  // Receive loop, if data received by RF433 send it by MQTT to MQTTsubject
  if (mySwitch.available()) {
    // Topic on which we will send data
    trc("Receiving 433Mhz signal");
    String MQTTsubject =root_topicOut;
    long MQTTvalue;
    MQTTvalue=mySwitch.getReceivedValue(); 
    int ReceivedBitlength = mySwitch.getReceivedBitlength();
    int ReceivedProtocol =mySwitch.getReceivedProtocol();
    int ReceivedDelay =mySwitch.getReceivedDelay();
    Serial.print("Rawdata=");
    Serial.println((int)mySwitch.getReceivedRawdata());
    String MQmessage = CreateJsonString(MQTTvalue,ReceivedBitlength,ReceivedProtocol,ReceivedDelay);
    //output(mySwitch.getReceivedValue(), mySwitch.getReceivedBitlength(), mySwitch.getReceivedDelay(), mySwitch.getReceivedRawdata(),mySwitch.getReceivedProtocol());
    
    mySwitch.resetAvailable();
    if (client.connected()) {
      trc("Sending 433Mhz signal to MQTT");
      trc(String(MQTTvalue));
     // sendMQTT(MQTTsubject,String(MQTTvalue));
      sendMQTT(MQTTsubject,MQmessage);
      
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


void receivingMQTT(String topicNameRec, String getReceivedValue, int getReceivedDelay) {
  trc("Receiving data by MQTT");
  trc(topicNameRec);
  char topicOri[26] = "";
  char topicStrAck[26] = "";
  char datacallback[26] = "";
  
  // Below you send RF signal following data value received by MQTT 
   getReceivedValue.toCharArray(datacallback,26);
    trc(getReceivedValue);
    long int data = atol(datacallback);
    trc("Send received data by RF 433");
    trc(String(data));
    //send received MQTT value by RF signal (example of signal sent data = 5264660)
    mySwitch.setPulseLength(getReceivedDelay);
    mySwitch.send(data, 24);
    trc("success sending data");

    const char* b = dec2binWzerofill(data, 24);
    trc(String(b));
    mySwitch.sendTriState(bin2tristate( b));
    trc(bin2tristate( b));
    trc("Finnished");
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
    trc("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      trc("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        trc("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          trc("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          
        } else {
          trc("failed to load json config");
          
        }
      }
    }
    else
    {
      trc("File /config.json doesnt exist");
      //SPIFFS.format();
      trc("Formatted Spiffs");    
     

    }
  } else {
    trc("failed to mount FS");
  }
}

String CreateJsonString(long ReceivedValue, int ReceivedBitlength,int ReceivedProtocol  ,int ReceivedDelay)
{
  String retval = "{ \"payload\":{ \"getReceivedValue\":" + String(ReceivedValue) + ", \"getReceivedBitlength\":" + String(ReceivedBitlength) + ", \"getReceivedProtocol\":" + String(ReceivedProtocol) + ", \"getReceivedDelay\":" + String(ReceivedDelay) + "}}";
  return retval;
}


//trace
void trc(String msg){
  if (TRACE) {
  Serial.println(msg);
  }
}

