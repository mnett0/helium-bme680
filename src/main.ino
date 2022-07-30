#include <Arduino.h>
#include <device_secrets.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#include <Wire.h>
#include <bsec.h>

Bsec iaqSensor;

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
static const u1_t PROGMEM APPEUI[8] = SECRET_APPEUI;
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8] = SECRET_DEVEUI;
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from the TTN console can be copied as-is.
static const u1_t PROGMEM APPKEY[16] = SECRET_APPKEY;
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

// static uint8_t mydata[] = "Hello, world!";
byte mydata[8];
int count = 0;
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 900;

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = D3,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = D2,
    .dio = {D1, D0, LMIC_UNUSED_PIN},
};

const lmic_pinmap *pPinMap = &lmic_pins;

void printHex2(unsigned v)
{
    v &= 0xff;
    if (v < 16)
        Serial.print('0');
    Serial.print(v, HEX);
}

void onEvent(ev_t ev)
{
    Serial.print(os_getTime());
    Serial.print(": ");
    switch (ev)
    {
    case EV_SCAN_TIMEOUT:
        Serial.println(F("EV_SCAN_TIMEOUT"));
        break;
    case EV_BEACON_FOUND:
        Serial.println(F("EV_BEACON_FOUND"));
        break;
    case EV_BEACON_MISSED:
        Serial.println(F("EV_BEACON_MISSED"));
        break;
    case EV_BEACON_TRACKED:
        Serial.println(F("EV_BEACON_TRACKED"));
        break;
    case EV_JOINING:
        Serial.println(F("EV_JOINING"));
        break;
    case EV_JOINED:
        Serial.println(F("EV_JOINED"));
        {
            u4_t netid = 0;
            devaddr_t devaddr = 0;
            u1_t nwkKey[16];
            u1_t artKey[16];
            LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
            Serial.print("netid: ");
            Serial.println(netid, DEC);
            Serial.print("devaddr: ");
            Serial.println(devaddr, HEX);
            Serial.print("AppSKey: ");
            for (size_t i = 0; i < sizeof(artKey); ++i)
            {
                if (i != 0)
                    Serial.print("-");
                printHex2(artKey[i]);
            }
            Serial.println("");
            Serial.print("NwkSKey: ");
            for (size_t i = 0; i < sizeof(nwkKey); ++i)
            {
                if (i != 0)
                    Serial.print("-");
                printHex2(nwkKey[i]);
            }
            Serial.println();
        }
        // Disable link check validation (automatically enabled
        // during join, but because slow data rates change max TX
        // size, we don't use it in this example.
        LMIC_setLinkCheckMode(0);
        break;
    /*
    || This event is defined but not used in the code. No
    || point in wasting codespace on it.
    ||
    || case EV_RFU1:
    ||     Serial.println(F("EV_RFU1"));
    ||     break;
    */
    case EV_JOIN_FAILED:
        Serial.println(F("EV_JOIN_FAILED"));
        break;
    case EV_REJOIN_FAILED:
        Serial.println(F("EV_REJOIN_FAILED"));
        break;
    case EV_TXCOMPLETE:
        Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
        if (LMIC.txrxFlags & TXRX_ACK)
            Serial.println(F("Received ack"));
        if (LMIC.dataLen)
        {
            Serial.print(F("Received "));
            Serial.print(LMIC.dataLen);
            Serial.println(F(" bytes of payload"));
        }
        // Schedule next transmission
        os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
        break;
    case EV_LOST_TSYNC:
        Serial.println(F("EV_LOST_TSYNC"));
        break;
    case EV_RESET:
        Serial.println(F("EV_RESET"));
        break;
    case EV_RXCOMPLETE:
        // data received in ping slot
        Serial.println(F("EV_RXCOMPLETE"));
        break;
    case EV_LINK_DEAD:
        Serial.println(F("EV_LINK_DEAD"));
        break;
    case EV_LINK_ALIVE:
        Serial.println(F("EV_LINK_ALIVE"));
        break;
    /*
    || This event is defined but not used in the code. No
    || point in wasting codespace on it.
    ||
    || case EV_SCAN_FOUND:
    ||    Serial.println(F("EV_SCAN_FOUND"));
    ||    break;
    */
    case EV_TXSTART:
        Serial.println(F("EV_TXSTART"));
        break;
    case EV_TXCANCELED:
        Serial.println(F("EV_TXCANCELED"));
        break;
    case EV_RXSTART:
        /* do not print anything -- it wrecks timing */
        break;
    case EV_JOIN_TXCOMPLETE:
        Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
        break;

    default:
        Serial.print(F("Unknown event: "));
        Serial.println((unsigned)ev);
        break;
    }
}

void do_send(osjob_t *j)
{
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND)
    {
        Serial.println(F("OP_TXRXPEND, not sending"));
    }
    else
    {
        getTemperature();
        // Prepare upstream data transmission at the next possible time.
        LMIC_setTxData2(1, mydata, sizeof(mydata), 0);
        Serial.println(F("Packet queued"));
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void setup()
{
    Serial.begin(9600);
    delay(5000);

    Serial.println(F("Starting"));

    Wire.begin();

    iaqSensor.begin(BME680_I2C_ADDR_PRIMARY, Wire);

    bsec_virtual_sensor_t sensorList[10] = {
        BSEC_OUTPUT_RAW_TEMPERATURE,
        BSEC_OUTPUT_RAW_PRESSURE,
        BSEC_OUTPUT_RAW_HUMIDITY,
        BSEC_OUTPUT_RAW_GAS,
        BSEC_OUTPUT_IAQ,
        BSEC_OUTPUT_STATIC_IAQ,
        BSEC_OUTPUT_CO2_EQUIVALENT,
        BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    };

    iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);

    iaqSensor.run();

    // LMIC init
    os_init_ex(pPinMap);
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    LMIC_setClockError(1 * MAX_CLOCK_ERROR / 40);

    LMIC_setLinkCheckMode(0);

    LMIC_setDrTxpow(DR_SF7, 14);

    // Start job (sending automatically starts OTAA too)
    do_send(&sendjob);
}

void loop()
{
    iaqSensor.run();
    os_runloop_once();
}

void getTemperature()
{
    String data = String(iaqSensor.temperature * 100, 0) + formatPressure(iaqSensor.pressure / 100) + String(iaqSensor.humidity, 0) + String(iaqSensor.iaqAccuracy) + formatIAQ(iaqSensor.staticIaq) + String(iaqSensor.co2Equivalent, 0);
    // Serial.println(data);
    DecToHex(data);
    count = 0;
}

void DecToHex(String data)
{
    for (int i = 0; i <= data.length() - 1; i++)
    {
        String y = String(data[i]) + String(data[i + 1]);

        String toHex = String(y.toInt(), HEX);

        Serial.println(toHex);

        byte val = StrtoByte(toHex);

        mydata[count] = val;

        if (i <= data.length() - 1)
        {
            count++;
        }

        i++;
    }
}

// String to byte --> Example: String = "C4" --> byte = {0xC4}
byte StrtoByte(String str_value)
{
    char char_buff[3];
    str_value.toCharArray(char_buff, 3);
    byte byte_value = strtoul(char_buff, NULL, 16);
    return byte_value;
}

String formatPressure(int p_press)
{
    if (p_press < 1000)
    {
        return "0" + String(p_press);
    }
    else
    {
        return String(p_press);
    }
}

String formatIAQ(int p_iaq)
{
    if (p_iaq < 100)
    {
        return "0" + String(p_iaq);
    }
    else if (p_iaq < 10)
    {
        return "00" + String(p_iaq);
    }
    else
    {
        return String(p_iaq);
    }
}