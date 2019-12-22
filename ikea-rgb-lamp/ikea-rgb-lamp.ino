#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <credentials.h>

#define USE_OTA

#ifdef USE_OTA
# include <ArduinoOTA.h>   
#endif

#define FASTLED_INTERRUPT_RETRY_COUNT 0
#define FASTLED_ESP8266_NODEMCU_PIN_ORDER
#define FASTLED_INTERNAL

#include "FastLED.h" 

/*********************************** FastLED Defintions ********************************/
#define LED_COUNT    73 //72 + 1 for the level shifter led
#define LED_DATA_PIN    2
//#define LED_CLOCK_PIN 5    
struct CRGB s_leds[LED_COUNT];  


String s_client_id;

WiFiClient s_wifi_client;
PubSubClient s_mqtt_client(s_wifi_client);

#define NODE_LOCATION "home"
#define NODE_TYPE "ikea_rgb"
#define NODE_NAME "disco_light"

const char* s_status_topic = "/"NODE_LOCATION"/"NODE_NAME"/status";
const char* s_switch_topic = "/"NODE_LOCATION"/"NODE_NAME"/switch";

const char* s_color_topic = "/"NODE_LOCATION"/"NODE_NAME"/color";
const char* s_color_set_topic = "/"NODE_LOCATION"/"NODE_NAME"/color/set";

bool s_status = true;
int32_t s_last_interpolation_tp = 0;

struct Color
{
  Color() = default;
  Color(const Color&) = default;
  Color(float r, float g, float b) : r(r), g(g), b(b) {}

  Color& operator=(const Color&) = default;
  
  float r = 0;
  float g = 0;
  float b = 0;
};
Color s_color;
Color s_target_color;
Color s_saved_color;

float clamp(float t, float _min, float _max)
{
  return min(max(t, _min), _max);
}

Color clamp(Color color, Color _min, Color _max)
{
  Color res;
  res.r = clamp(color.r, _min.r, _max.r);
  res.g = clamp(color.g, _min.g, _max.g);
  res.b = clamp(color.b, _min.b, _max.b);
  return res;
}

void mqtt_publish(const char* topic, const Color& c)
{
  char buffer[128] = {0};
  sprintf(buffer, "%d,%d,%d", (int)(c.r*255.f), (int)(c.g*255.f), (int)(c.b*255.f));
  s_mqtt_client.publish(topic, buffer, strlen(buffer));
}
void mqtt_publish(const char* topic, float t)
{
  char buffer[128] = {0};
  sprintf(buffer, "%f", t);
  s_mqtt_client.publish(topic, buffer, strlen(buffer));
}
void mqtt_publish(const char* topic, bool t)
{
  char buffer[128] = {0};
  sprintf(buffer, "%s", t ? "true" : "false");
  s_mqtt_client.publish(topic, buffer, strlen(buffer));
}

void set_color(const Color& color)
{
  Color c;
  c.r = clamp(color.r, 0.f, 1.f);
  c.g = clamp(color.g, 0.f, 1.f);
  c.b = clamp(color.b, 0.f, 1.f);
  if (fabs(s_color.r - c.r) < 0.00001f &&
      fabs(s_color.g - c.g) < 0.00001f &&
      fabs(s_color.b - c.b) < 0.00001f)
  {
    return;
  }

  /*
  Serial.print("c: ");
  Serial.print(c.r);
  Serial.print(" ");
  Serial.print(c.g);
  Serial.print(" ");
  Serial.print(c.b);
  Serial.println(" ");
  */

  s_color = c;

  c.r = powf(c.r, 2.2f);
  c.g = powf(c.g, 2.2f);
  c.b = powf(c.b, 2.2f);
  
  float brightnessLimit = 0.1f;

  float brightness = clamp(sqrt(c.r*c.r + c.g*c.g + c.b*c.b), 0.f, 1.f);
  if (brightness < 0.00001f)
  {
    FastLED.setBrightness(0);
    CRGB crgb(0, 0, 0);
    for (size_t i = 0; i < LED_COUNT; i++) 
    {
      s_leds[i] = crgb;
    }
  }
  else if (brightness > brightnessLimit)
  {
    FastLED.setBrightness(255);
    CRGB crgb((uint8_t)(c.r * 255.f), (uint8_t)(c.g * 255.f), (uint8_t)(c.b * 255.f));
    for (size_t i = 0; i < LED_COUNT; i++) 
    {
      s_leds[i] = crgb;
    }
  }
  else
  {
    FastLED.setBrightness(brightnessLimit * 255);
    c.r /= brightnessLimit;
    c.g /= brightnessLimit;
    c.b /= brightnessLimit;
    CRGB crgb((uint8_t)(c.r * 255.f), (uint8_t)(c.g * 255.f), (uint8_t)(c.b * 255.f));
    for (size_t i = 0; i < LED_COUNT; i++) 
    {
      s_leds[i] = crgb;
    }
  }
  FastLED.show();
}  

void set_target_color(const Color& color)
{
  Color c;
  c.r = clamp(color.r, 0.f, 1.f);
  c.g = clamp(color.g, 0.f, 1.f);
  c.b = clamp(color.b, 0.f, 1.f);

  if (fabs(s_target_color.r - c.r) > 0.00001f ||
      fabs(s_target_color.g - c.g) > 0.00001f ||
      fabs(s_target_color.b - c.b) > 0.00001f)
  {
    mqtt_publish(s_color_topic, c);
  }

  s_target_color = c;
}

void save_color()
{
  s_saved_color = s_target_color;
}

void save_color(const Color& color)
{
  s_saved_color = color;
}

void load_color()
{
  set_target_color(s_saved_color);
}

void set_status(bool status)
{
  if (status == s_status)
  {
    return;
  }

  if (status == false)
  {
    s_status = status;
    mqtt_publish(s_status_topic, s_status);
    
    save_color();
    set_target_color({ 0, 0, 0 });
    Serial.println("Turning off");
  }
  else
  {
    s_status = status;
    mqtt_publish(s_status_topic, s_status);
    
    load_color();
    Serial.println("Turning on");
  }  
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) 
{
  char buffer[128] = { 0, };
  length = min(length, 126u);
  memcpy(buffer, payload, length);
  buffer[length] = 0;

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.print(length);
  Serial.print(" bytes '");
  Serial.print(buffer);
  Serial.print("'");
  Serial.println();
  
  if (strcmp(topic, s_switch_topic) == 0)
  {
    set_status(strcmp(buffer, "true") == 0);
  }
  if (strcmp(topic, s_color_set_topic) == 0)
  {
    size_t index = 0;
    float values[3];
    char* pch = strtok(buffer," ,");
    while (pch != NULL && index < 3)
    {
      float v = atoi(pch) / 255.f;
      values[index++] = isnan(v) ? 0.f : v;
      pch = strtok(NULL, " ,");
    }
    Color c = s_target_color;
    c.r = values[0];
    c.g = values[1];
    c.b = values[2];

    if (s_status)
    {
      set_target_color(c);
    }
  }
}

void reconnect_mqtt() 
{
  // Loop until we're reconnected
  while (!s_mqtt_client.connected()) 
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (s_mqtt_client.connect(s_client_id.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) 
    {
      Serial.println("connected");

      // Once connected, publish an announcement...
      mqtt_publish(s_status_topic, s_status);
      mqtt_publish(s_color_topic, s_target_color);
      // ... and resubscribe
      s_mqtt_client.subscribe(s_switch_topic);
      s_mqtt_client.subscribe(s_color_set_topic);
    } 
    else 
    {
      Serial.print("failed, rc=");
      Serial.print(s_mqtt_client.state());
      Serial.println(" try again");
    }
    delay(1000);
  }
}

void setup_wifi() 
{
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) 
  {
    Serial.print(".");
    delay(500);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}  

void setup() 
{
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(s_leds, LED_COUNT).setCorrection(TypicalSMD5050); 
  FastLED.setDither();

  s_client_id = NODE_LOCATION"/"NODE_NAME"/"NODE_TYPE"/";
  s_client_id += String(random(0xffff), HEX);

  Serial.print("Node location: "); Serial.println(NODE_LOCATION);
  Serial.print("Node type: "); Serial.println(NODE_TYPE);
  Serial.print("Node name: "); Serial.println(NODE_NAME);
  Serial.print("Client ID: "); Serial.println(s_client_id.c_str());
  Serial.print("MQTT status topic: "); Serial.println(s_status_topic);
  Serial.print("MQTT switch topic: "); Serial.println(s_switch_topic);
  Serial.print("MQTT color topic: "); Serial.println(s_color_topic);
  Serial.print("MQTT color set topic: "); Serial.println(s_color_set_topic);

  setup_wifi();
  s_mqtt_client.setServer(MQTT_SERVER, MQTT_PORT);
  s_mqtt_client.setCallback(mqtt_callback);

#ifdef USE_OTA
  //OTA SETUP
  ArduinoOTA.setPort(OTA_PORT);
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(s_client_id.c_str());

  // No authentication by default
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() 
  {
    Serial.println("Starting");
  });
  ArduinoOTA.onEnd([]() 
  {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) 
  {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) 
  {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
#endif

  //set_target_color(Color(0.5f, 0.5f, 0.5f), true);
}

void process_interpolation()
{
  int32_t tp = millis();
  if (tp < s_last_interpolation_tp)
  {
    s_last_interpolation_tp = tp;
  }

  if (tp - s_last_interpolation_tp < 10)
  {
    return;
  }
  s_last_interpolation_tp = tp;

  Color color = s_color;
  color.r += (s_target_color.r - s_color.r) * 0.05f;
  color.g += (s_target_color.g - s_color.g) * 0.05f;
  color.b += (s_target_color.b - s_color.b) * 0.05f;
  set_color(color);
}

void process_mqtt()
{
  if (WiFi.status() != WL_CONNECTED) 
  {
    delay(1);
    Serial.print("WIFI Disconnected. Attempting reconnection.");
    setup_wifi();
    return;
  }
  
  if (!s_mqtt_client.connected()) 
  {
    reconnect_mqtt();
  }
  s_mqtt_client.loop();
}


void loop() 
{
  process_interpolation();
  process_mqtt();
#ifdef USE_OTA
  ArduinoOTA.handle(); 
#endif
  FastLED.delay(10);
}
