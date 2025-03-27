#include <cstdlib>
#include <string.h>
#include <time.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <esp_task_wdt.h> // Include ESP32 Task Watchdog library
#include <esp_system.h>
#define WATCHDOG_TIMEOUT 100 // Set watchdog timeout to 10 seconds
#define MODEM_RESET_PIN 4   // GPIO pin to reset the Wi-Fi modem
#include <WiFi.h>
#include <mqtt_client.h>
#include <ModbusMaster.h>

#include <az_iot_hub_client.h>
#include <az_result.h>
#include <az_span.h>
#include <ArduinoJson.h>
#include "AzIoTSasToken.h"
#include "SerialLogger.h"
#include "ca.h"
#include "iot_configs.h"

//#define PULSE1 4
//#define PULSE2 2
//#define PULSE3 15
//ro cons upland 15
//ro cons downland 19
//ro cons lcv 16
//#define UPLAND 15
//#define DOWNLAND 19
//#define LCV 16
//#define ETP_RO 13
//#define P15 1
#define SWM 1
#define RXD2 16
#define TXD2 17 
uint8_t result;
#define RED 25
#define GREEN 26
#define BLUE 27

float myresetvalue = 0;
bool resetcounter = false;
int pingcounter = 0;
long previousRs485Millis = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
//unsigned long volatile pulse1_count=0, pulse2_count=0, pulse3_count=0;
//int next_serial_post=0;
String timestampcloud;

/*void IRAM_ATTR pulse1_isr(){
  pulse1_count++;
}

void IRAM_ATTR pulse2_isr(){
  pulse2_count++;
}

void IRAM_ATTR pulse3_isr(){
  pulse3_count++;
}*/

#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DST_DIFF   1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DST_DIFF) * 3600)

static const char* ssid = IOT_CONFIG_WIFI_SSID;
static const char* password = IOT_CONFIG_WIFI_PASSWORD;
static const char* host = IOT_CONFIG_IOTHUB_FQDN;
static const char* mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char* device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = 8883;

static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint8_t telemetry_payload[100];
static uint32_t telemetry_send_count = 0;
static DynamicJsonDocument doc(1024);
static DynamicJsonDocument docb(500);
String mac_id;
double totaliser; 
/////////////////////////////////////////////


////////////////////////////////////////////////
static AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));

static void connectToWiFi()
{
  Logger.Info("Connecting to WIFI SSID " + String(ssid));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

static void initializeTime()
{
  Logger.Info("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  Logger.Info("Time initialized!");
}

void receivedCallback(char* topic, byte* payload, unsigned int length)
{
  Logger.Info("Received [");
  Logger.Info(topic);
  Logger.Info("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    case MQTT_EVENT_ERROR:
      Logger.Info("MQTT event MQTT_EVENT_ERROR");
      break;
    case MQTT_EVENT_CONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_CONNECTED");
      break;
    case MQTT_EVENT_DISCONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_SUBSCRIBED");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_UNSUBSCRIBED");
      break;
    case MQTT_EVENT_PUBLISHED:
      Logger.Info("MQTT event MQTT_EVENT_PUBLISHED");
      break;
    case MQTT_EVENT_DATA:
      Logger.Info("MQTT event MQTT_EVENT_DATA");
      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      Logger.Info("MQTT event MQTT_EVENT_BEFORE_CONNECT");
      break;
    default:
      Logger.Error("MQTT event UNKNOWN");
      break;
  }
}
// Initialize GPIO-4 for modem reset
static void setupModemResetPin() {
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, LOW);
  delay(3000);
  Logger.Info("Resetting Wi-Fi modem...");
  digitalWrite(MODEM_RESET_PIN, HIGH);
  delay(3000); // Keep LOW for 5 seconds
  digitalWrite(MODEM_RESET_PIN, LOW);
  Logger.Info("Wi-Fi modem reset complete."); // Set HIGH initially
  delay(30000);
}

// Reset the Wi-Fi modem
static void resetModem() {
  Logger.Info("Resetting Wi-Fi modem...");
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, HIGH);
  delay(3000); // Keep LOW for 5 seconds
  digitalWrite(MODEM_RESET_PIN, LOW);
  Logger.Info("Wi-Fi modem reset complete.");
  
}

static void initializeIoTHubClient()
{
  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t*)host, strlen(host)),
          az_span_create((uint8_t*)device_id, strlen(device_id)),
          NULL)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Logger.Error("Failed getting client id");
    return;
  }

  // Get the MQTT user name used to connect to IoT Hub
  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    Logger.Error("Failed to get MQTT clientId, return code");
    return;
  }

  Logger.Info("Client ID: " + String(mqtt_client_id));
  Logger.Info("Username: " + String(mqtt_username));
}

static int initializeMqttClient()
{
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
  {
    Logger.Error("Failed generating SAS token");
    return 1;
  }

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));
  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;
  mqtt_config.password = (const char*)az_span_ptr(sasToken.Get());
  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char*)ca_pem;

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Logger.Error("Failed creating mqtt client");
    return 1;
  }

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK)
  {
    Logger.Error("Could not start mqtt client; error code:" + start_result);
    return 1;
  }
  else
  {
    Logger.Info("MQTT client started");
    return 0;
  }
}

static uint32_t getEpochTimeInSecs() 
{ 
  return (uint32_t)time(NULL);
}

static int establishConnection()
{
  connectToWiFi();
  initializeTime();
  initializeIoTHubClient();
  (void)initializeMqttClient();
}

void getcreated_on()
{
    time_t     created_now;
    struct tm  ts;
    char buf[80];

    // Get current time
    time(&created_now);

    // Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
    ts = *gmtime(&created_now);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &ts);
    timestampcloud = (char *)buf;

  // sprintf (timestamp, "%4d-%02d-%02d %02d:%02d:%02d", year(), month(),day(), hour(), minute(), second());
  Logger.Info(timestampcloud);

}

static char *getTelemetryPayload(int sid)
{
  az_span temp_span = az_span_create(telemetry_payload, sizeof(telemetry_payload));
  getcreated_on();

//   unsigned long epochTime = timeClient.getEpochTime();
// struct tm * timeinfo;
// timeinfo = localtime((time_t *)&epochTime);
// char buffer[20];
// strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", timeinfo);
// formattedDate = String(buffer);
  docb.clear();
  if(sid==1) docb["unit_id"] = "FG24083F";
  else if(sid==2)  docb["unit_id"] = "FG24084F";
  //else if(sid==3)  docb["unit_id"] = "3";
  docb["consumption"] = String(totaliser);
  docb["created_on"] = timestampcloud;

  // docb["industry_id"] = industry_id;
  // docb["category"] = devcategory;
  // docb["value"] = flowratevalue;
  serializeJson(docb, telemetry_payload, sizeof(telemetry_payload));
  serializeJson(docb, Serial);
  // temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR());
  // temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("{ \"deviceId\": \"" IOT_CONFIG_DEVICE_ID "\", \"industry_id\": "industry_id"\", \"category\":"devcategory));
  // (void)az_span_u32toa(temp_span, industry_id, &temp_span);
  // temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" }"));
  // temp_span = az_span_copy_u8(temp_span, '\0');
  // Serial.print(temp_span.c_str());

  return (char *)telemetry_payload;
}

// static void getTelemetryPayload(az_span payload, az_span* out_payload)
// {
//   az_span original_payload = payload;

//   payload = az_span_copy(
//       payload, AZ_SPAN_FROM_STR("{\"deviceId\":\"tmp\",\"msgCount\":\"anm\""));
//   // (void)az_span_u32toa(payload, telemetry_send_count++, &payload);
//   payload = az_span_copy(payload, AZ_SPAN_FROM_STR("}"));
//   payload = az_span_copy_u8(payload, '\0');

//   *out_payload = az_span_slice(original_payload, 0, az_span_size(original_payload) - az_span_size(payload));
// }

static void sendTelemetry(int sid)
{
  az_span telemetry = AZ_SPAN_FROM_BUFFER(telemetry_payload);

  Logger.Info("Sending telemetry ...");

  // The topic could be obtained just once during setup,
  // however if properties are used the topic need to be generated again to reflect the
  // current values of the properties.
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  // getTelemetryPayload(telemetry, &telemetry);
  // Logger.Info("telemetry "+String((const char*)az_span_ptr(telemetry)));

  // if (esp_mqtt_client_publish(
  //         mqtt_client,
  //         telemetry_topic,
  //         (const char*)az_span_ptr(telemetry),
  //         az_span_size(telemetry),
  //         MQTT_QOS1,
  //         DO_NOT_RETAIN_MSG)
  //     == 0)
  // {
  //   Logger.Error("Failed publishing");
  // }
  // else
  // {
  //   Logger.Info("Message published successfully");
  // }
  const char *payload_ptr = getTelemetryPayload(sid);

  if (esp_mqtt_client_publish(
          mqtt_client,
          telemetry_topic,
          payload_ptr,
          strlen(payload_ptr),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG)
      == 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("Message published successfully");
  }
}
static void initializeWatchdog() {
  if (esp_task_wdt_init(WATCHDOG_TIMEOUT, true) != ESP_OK) {
    Logger.Error("Failed to initialize Task Watchdog Timer.");
  } else {
    Logger.Info("Task Watchdog Timer initialized.");
  }
  if (esp_task_wdt_add(NULL) != ESP_OK) {
    Logger.Error("Failed to add task to Task Watchdog Timer.");
  } else {
    Logger.Info("Task added to Watchdog Timer.");
    setupModemResetPin();
    
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600);
  Serial.print("MAC ID: ");
  Serial.println(WiFi.macAddress());
  Logger.Info("Initializing...");
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, LOW);
  //setupModemResetPin();    // Set up the modem reset pin
  initializeWatchdog(); // Initialize the watchdog timer
  establishConnection();   // Establish Wi-Fi and MQTT connection
}
int getTotalflow(int slave_id)
{
  ModbusMaster node;
  
  node.begin(slave_id, Serial2);
   int registerAddress = (slave_id == 1) ? 0x08 :2006;
   result = node.readHoldingRegisters(registerAddress, 2);
  Serial.println(result);
  Serial.println(result);
  if (result == node.ku8MBSuccess)
  {


  //float flowratefloat= 0.0;
  //unsigned long totdecimallong = 0;  
  //long tot = 0;
  
  Serial.println("\nBuffer Start");
  for(int i = 0; i< 4 ; i++)
  {
    Serial.println(node.getResponseBuffer(i),HEX); 
  }
  Serial.println("Buffer End\n");
   //uint32_t flowratehex = (node.getResponseBuffer(0x00)<<16)| node.getResponseBuffer(0x01);
   unsigned long totdecimallong = (node.getResponseBuffer(0x01)<<16)|node.getResponseBuffer(0x00);
   uint32_t tothex = (node.getResponseBuffer(0x03)<<16)|node.getResponseBuffer(0x02);
   //flowratefloat = *((float*)&flowratehex);
   float totdecimalfloat = *((float*)&tothex);
   //tot = *((long*)&tothex);
   //Serial.println(node.getResponseBuffer(0x00),HEX);
   //Serial.println(node.getResponseBuffer(0x01),HEX); 

    //Serial.println("data collected    new    ");
    //Serial.println(flowratehex,HEX);
    //Serial.println(totdecimalhex,HEX);
    //Serial.println(tot);

//                for(int i = 0; i< 24 ; i++)
//                {
//                   Serial.println(node.getResponseBuffer(i),HEX);delay(10);
//                  }

   //flowrate =flowratefloat;
   Serial.print("Data int = ");
   Serial.println(totdecimallong);
   Serial.print("Data dec = ");
   Serial.println(totdecimalfloat);
   totaliser =  totdecimallong ;
    Serial.print("Before multiplier");
    Serial.println(totaliser);
    return(1);
  }
  else
  {
    Serial.println("RS485 fail");
    return(0);
  }
}

int getMultiplier(int slave_id)
{
  ModbusMaster node;
  
  node.begin(slave_id, Serial2);
  result = node.readHoldingRegisters(0x059D, 2);
  Serial.println(result);
  if (result == node.ku8MBSuccess)
  {
    int mult, unit;
    unit=node.getResponseBuffer(0x00);
    mult=node.getResponseBuffer(0x01);
    Serial.print("mult = ");
    Serial.println(mult);
    Serial.print("unit = ");
    Serial.println(unit);
    mult=mult-3;
    if(mult>0)
    {
      for(int i=0;i<mult;i++)
        totaliser*=10;
    }
    else
    {
      for(int i=0;i<mult;i++)
        totaliser/=10;
    }
    switch(unit)
    {
      case 1:
        totaliser/=1000;
        break;
      case 2:
        totaliser=(totaliser*3.785)/1000;
        break;
      case 3:
        totaliser=(totaliser*28.3168)/1000;
        break;
    }
    return(1);
  }
  else
  {
    Serial.println("RS485 fail");
    return(0);
  }
}

int Post_Data_on_UART(int slave_id) {

  if(getTotalflow(slave_id)==0) return(0);
  delay(5000);
  if(slave_id == 1){
  if(getMultiplier(slave_id)==0) return(0);
  }
     Serial.print("Slave ID:");
     Serial.println(slave_id);
     Serial.println(totaliser);
     return(1);


 }


int RS485loop(int slave_id)
{

  int stat;
   digitalWrite(GREEN, LOW);
//    if ((millis() - previousRs485Millis >= 0))
    //{previousRs485Millis = millis();
     Serial.println("pinging");
//      value(0x01, 0x04, 0x10, 0x18, 0x00, 0x04, 0x75, 0x0E);
      Serial.println("pingning ");
      stat=Post_Data_on_UART(slave_id);
      pingcounter++;
    //}
    digitalWrite(GREEN, HIGH);
    return(stat);
}


void loop() {
  // put your main code here, to run repeatedly:
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWiFi();
  }
  else if (sasToken.IsExpired())
  {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    (void)esp_mqtt_client_destroy(mqtt_client);
    initializeMqttClient();
  }
  else if (millis() > next_telemetry_send_time_ms)
  {
    //delay(5000);
    //sendTelemetry(1,pulse1_count);
    /*delay(5000);
    sendTelemetry(2,pulse2_count);
    delay(5000);
    sendTelemetry(3,pulse3_count);*/
    Serial.println("start");
    if(RS485loop(1)){
      sendTelemetry(1);
    }
    if(RS485loop(2)){
      sendTelemetry(2);
    }
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
      esp_task_wdt_reset();// Reset the watchdog timer
  delay(300);// Small delay to avoid watchdog timeout 
  }
}
