#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>
#include <Ethernet.h>
#include <ArduinoHA.h>
#if USE_INA3221
#include <Wire.h>
#include <Beastdevices_INA3221.h>
#endif
#if USE_ACS712
#include <ACS712.h>
#endif
#if USE_METRIFUL
#include <Metriful_sensor.h>
#endif

#define SERIAL_SPEED 115200 // serial baud rate
#define PRINT_DEC_POINTS 3  // decimal points to print

#if SD_IN_MKRZERO
        const unsigned int chipSelect = SDCARD_SS_PIN;
#else
        const unsigned int chipSelect = 4;
#endif


#define BROKER_ADDR IPAddress(192, 168, 1, 186)
// IPAddress brokerAddr;

byte mac[] = {0x00, 0x10, 0xFA, 0x6E, 0x38, 0x4C};
EthernetClient client;

HADevice haDevice(mac, sizeof(mac));
HAMqtt mqtt(client, haDevice);

// Define relays as switches
// Set initial state of off (false)
void onBeforeSwitchStateChanged(bool state, HASwitch *s)
{
  // this callback will be called before publishing new state to HA
  // in some cases there may be delay before onStateChanged is called due to network latency
}

// Configure Relay 1

const unsigned int RELAY_1_PIN = 1;

HASwitch relay_1("relay_1", false);

void relay1onSwitchStateChanged(bool state, HASwitch *s)
{
  digitalWrite(RELAY_1_PIN, (state ? HIGH : LOW));
}

// Configure Relay 2

const unsigned int RELAY_2_PIN = 2;

HASwitch relay_2("relay_2", false);

void relay2onSwitchStateChanged(bool state, HASwitch *s)
{
  digitalWrite(RELAY_2_PIN, (state ? HIGH : LOW));
}

#if USE_METRIFUL
// Configure INA3221 Sensor
const unsigned long METRIFUL_PUBLISH_INTERVAL = 5000UL;
unsigned long metrifulPreviousMillis = 0UL;

// How often to read and report the data (every 3, 100 or 300 seconds)
uint8_t cycle_period = CYCLE_PERIOD_100_S;

// Structs for data
AirData_t airData = {0};
AirQualityData_t airQualityData = {0};
LightData_t lightData = {0};
ParticleData_t particleData = {0};
SoundData_t soundData = {0};

HASensor metriful_temperature("metriful_temperature");
HASensor metriful_pressure("metriful_pressure");
HASensor metriful_humidity("metriful_humidity");
HASensor metriful_illuminance("metriful_illuminance");
HASensor metriful_soundLevel("metriful_current");
HASensor metriful_peakAmplitude("metriful_peakAmplitude");
HASensor metriful_AQI("metriful_AQI");
HASensor metriful_AQ_assessment("metriful_AQ_assessment");
HASensor metriful_particulates("metriful_particulates");
#endif

#if USE_INA3221
// Configure INA3221 Sensor
const unsigned long INA3221_PUBLISH_INTERVAL = 5000UL;
unsigned long ina3221PreviousMillis = 0UL;

// Set I2C address to 0x41 (A0 pin -> VCC)
Beastdevices_INA3221 ina3221(INA3221_ADDR41_VCC);

HASensor ina3221_channel_1_current("ina3221_channel_1_current");
HASensor ina3221_channel_1_voltage("ina3221_channel_1_voltage");
HASensor ina3221_channel_2_current("ina3221_channel_2_current");
HASensor ina3221_channel_2_voltage("ina3221_channel_2_voltage");
HASensor ina3221_channel_3_current("ina3221_channel_3_current");
HASensor ina3221_channel_3_voltage("ina3221_channel_3_voltage");
#endif

#if USE_ACS712
// Configure ACS712 Sensor
const unsigned long ACS712_PUBLISH_INTERVAL = 300000UL;
unsigned long acs712PreviousMillis = 0UL;

// Arduino UNO has 5.0 volt with a max ADC value of 1023 steps
// ACS712 5A  uses 185 mV per A
// ACS712 20A uses 100 mV per A
// ACS712 30A uses  66 mV per A
ACS712 ACS(A1, 3.3, 1023, 66);

HASensor acs712_current("acs712_current");
#endif

StaticJsonDocument<256> doc;

void read_config()
{
  // Open file
  File file = SD.open("config.txt");

  if (file)
  {
    DeserializationError error = deserializeJson(doc, file);

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
  }
  else
  {
    Serial.println("Failed to open file");
  }
}

void setup()
{
  Serial.begin(115200); // open the serial port at 115200 bps:

  pinMode(RELAY_1_PIN, OUTPUT);
  // relay_1_lastInputState = digitalRead(RELAY_1_PIN);
  pinMode(RELAY_2_PIN, OUTPUT);
  // relay_2_lastInputState = digitalRead(RELAY_2_PIN);

#if USE_METRIFUL
  // Initialize the host's pins, set up the serial port and reset:
  SensorHardwareSetup(I2C_ADDRESS);

  // Apply settings to the MS430 and enter cycle mode
  uint8_t particleSensorCode = PARTICLE_SENSOR;
  TransmitI2C(I2C_ADDRESS, PARTICLE_SENSOR_SELECT_REG, &particleSensorCode, 1);
  TransmitI2C(I2C_ADDRESS, CYCLE_TIME_PERIOD_REG, &cycle_period, 1);
  ready_assertion_event = false;
  TransmitI2C(I2C_ADDRESS, CYCLE_MODE_CMD, 0, 0);
#endif

#if USE_INA3221
  ina3221.begin();
  ina3221.reset();

  // Set shunt resistors to 10 mOhm for all channels
  ina3221.setShuntRes(10, 10, 10);
#endif

#if USE_ACS712
  ACS.autoMidPoint();
#endif

  Serial.println("\nInitializing SD card...");

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect))
  {
    Serial.println("Card initialization failed, or not present");
    // don't do anything more:
    while (1);
  }
  Serial.println("Card is present and initialized.");

  read_config();

  // set device's details (optional)
  JsonObject device = doc["device"];

  haDevice.setName(device["name"]);
  haDevice.setModel(device["model"]);
  haDevice.setManufacturer(device["manufacturer"]);
  haDevice.setSoftwareVersion(device["version"]);

  // This method enables availability for all device types registered on the device.
  // For example, if you have 5 sensors on the same device, you can enable
  // shared availability and change availability state of all sensors using
  // single method call "device.setAvailability(false|true)"
  haDevice.enableSharedAvailability();

  // Optionally, you can enable MQTT LWT feature. If device will lose connection
  // to the broker, all device types related to it will be marked as offline in
  // the Home Assistant Panel.
  haDevice.enableLastWill();

  // Configuratin for sensor INA3221
  // sensor.setName("12 volt system"); // optional

  JsonArray relays = doc["relays"].as<JsonArray>();

  // Configure relay_1 specifics
  JsonObject relay = relays.getElement(0);
  relay_1.setName(relay["name"]);
  relay_1.setRetain(true);
  // handle switch state
  relay_1.onBeforeStateChanged(onBeforeSwitchStateChanged);
  relay_1.onStateChanged(relay1onSwitchStateChanged);

  // Configure relay_2 specifics
  relay = relays.getElement(1);
  relay_2.setName(relay["name"]);
  relay_2.setRetain(true);
  // handle switch state
  relay_2.onBeforeStateChanged(onBeforeSwitchStateChanged);
  relay_2.onStateChanged(relay2onSwitchStateChanged);

#if USE_METRIFUL
  metriful_temperature.setName("Temperature");
  metriful_temperature.setUnitOfMeasurement("??C");
  metriful_temperature.setDeviceClass("temperature");
  metriful_temperature.setIcon("mdi:thermometer");
  metriful_pressure.setName("Pressure");
  metriful_pressure.setUnitOfMeasurement("Pa");
  metriful_pressure.setDeviceClass("pressure");
  metriful_pressure.setIcon("mdi:weather-cloudy");
  metriful_humidity.setName("Humidity");
  metriful_humidity.setUnitOfMeasurement("%");
  metriful_humidity.setDeviceClass("pressure");
  metriful_humidity.setIcon("mdi:water-percent");
  metriful_illuminance.setName("Illuminance");
  metriful_illuminance.setUnitOfMeasurement("lx");
  metriful_illuminance.setDeviceClass("illuminance");
  metriful_illuminance.setIcon("mdi:white-balance-sunny");
  metriful_soundLevel.setName("Sound level");
  metriful_soundLevel.setUnitOfMeasurement("dBA");
  metriful_soundLevel.setDeviceClass("pressure");
  metriful_soundLevel.setIcon("mdi:microphone");
  metriful_peakAmplitude.setName("Sound peak");
  metriful_peakAmplitude.setUnitOfMeasurement("mPa");
  metriful_peakAmplitude.setDeviceClass("pressure");
  metriful_peakAmplitude.setIcon("mdi:waveform");
  metriful_AQI.setName("Air Quality Index");
  metriful_AQI.setDeviceClass("aqi");
  metriful_AQI.setIcon("mdi:thought-bubble-outline");
  metriful_AQ_assessment.setName("Air quality assessment");
  metriful_AQ_assessment.setIcon("mdi:flower-tulip");
  metriful_particulates.setName("Particle concentration");
  metriful_particulates.setUnitOfMeasurement("??g/m??");
  metriful_particulates.setDeviceClass("pm25");
  metriful_particulates.setIcon("mdi:chart-bubble");
#endif

#if USE_INA3221
  // Configure ina3221_channel_1 specifics
  ina3221_channel_1_current.setName("Channel 1 Current");
  ina3221_channel_1_current.setUnitOfMeasurement("A");
  ina3221_channel_1_current.setDeviceClass("current");
  ina3221_channel_1_current.setIcon("mdi:current-dc");
  ina3221_channel_1_voltage.setName("Channel 1 Voltage");
  ina3221_channel_1_voltage.setUnitOfMeasurement("V");
  ina3221_channel_1_voltage.setDeviceClass("voltage");
  ina3221_channel_1_voltage.setIcon("mdi:home-battery-outline");

  // Configure ina3221_channel_2 specifics
  ina3221_channel_2_current.setName("Channel 2 Current");
  ina3221_channel_2_current.setUnitOfMeasurement("A");
  ina3221_channel_2_current.setDeviceClass("current");
  ina3221_channel_2_current.setIcon("mdi:current-dc");
  ina3221_channel_2_voltage.setName("Channel 2 Voltage");
  ina3221_channel_2_voltage.setUnitOfMeasurement("V");
  ina3221_channel_2_voltage.setDeviceClass("voltage");
  ina3221_channel_2_voltage.setIcon("mdi:home-battery-outline");

  // Configure ina3221_channel_3 specifics
  ina3221_channel_3_current.setName("Channel 3 Current");
  ina3221_channel_3_current.setUnitOfMeasurement("A");
  ina3221_channel_3_current.setDeviceClass("current");
  ina3221_channel_3_current.setIcon("mdi:current-dc");
  ina3221_channel_3_voltage.setName("Channel 3 Voltage");
  ina3221_channel_3_voltage.setUnitOfMeasurement("V");
  ina3221_channel_3_voltage.setDeviceClass("voltage");
  ina3221_channel_3_voltage.setIcon("mdi:home-battery-outline");
#endif

#if USE_ACS712
  acs712_current.setName("Solar Current");
  acs712_current.setUnitOfMeasurement("A");
  acs712_current.setDeviceClass("current");
  acs712_current.setIcon("mdi:current-dc");
#endif

  // you don't need to verify return status
  Ethernet.begin(mac);

  // print your local IP address:
  Serial.println(Ethernet.localIP());

  // String ipAddStr = String(doc["mqtt"]["broker"]);
  // mqtt.begin(brokerAddr.fromString(String(doc["mqtt"]["broker"])));
  mqtt.begin(BROKER_ADDR);
}

void loop()
{
  Ethernet.maintain();
  mqtt.loop();

  unsigned long now = millis();

#if USE_METRIFUL
  // publish metriful values
  if (now - metrifulPreviousMillis >= METRIFUL_PUBLISH_INTERVAL)
  {
    metrifulPreviousMillis = now;
    // Read data from the MS430 into the data structs.
    ReceiveI2C(I2C_ADDRESS, AIR_DATA_READ, (uint8_t *)&airData, AIR_DATA_BYTES);
    ReceiveI2C(I2C_ADDRESS, AIR_QUALITY_DATA_READ, (uint8_t *)&airQualityData, AIR_QUALITY_DATA_BYTES);
    ReceiveI2C(I2C_ADDRESS, LIGHT_DATA_READ, (uint8_t *)&lightData, LIGHT_DATA_BYTES);
    ReceiveI2C(I2C_ADDRESS, SOUND_DATA_READ, (uint8_t *)&soundData, SOUND_DATA_BYTES);
    ReceiveI2C(I2C_ADDRESS, PARTICLE_DATA_READ, (uint8_t *)&particleData, PARTICLE_DATA_BYTES);

    metriful_pressure.setValue((uint32_t)airData.P_Pa);
    metriful_humidity.setValue((uint32_t)airData.H_pc_int);
  }
#endif

#if USE_INA3221

  // publish ina3221 values
  if (now - ina3221PreviousMillis >= INA3221_PUBLISH_INTERVAL)
  {
    ina3221PreviousMillis = now;
    ina3221_channel_1_current.setValue(ina3221.getCurrent(INA3221_CH1));
    ina3221_channel_1_voltage.setValue(ina3221.getVoltage(INA3221_CH1));
    ina3221_channel_2_current.setValue(ina3221.getCurrent(INA3221_CH2));
    ina3221_channel_2_voltage.setValue(ina3221.getVoltage(INA3221_CH2));
    ina3221_channel_3_current.setValue(ina3221.getCurrent(INA3221_CH3));
    ina3221_channel_3_voltage.setValue(ina3221.getVoltage(INA3221_CH3));
  }
#endif

#if USE_ACS712
  // publish acs712 values
  if (now - acs712PreviousMillis >= ACS712_PUBLISH_INTERVAL)
  {
    acs712PreviousMillis = now;
    acs712_current.setValue((double)ACS.mA_DC()/1000.0);
  }
#endif
}