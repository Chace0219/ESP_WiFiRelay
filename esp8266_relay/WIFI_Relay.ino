

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>

#include <PubSubClient.h>

#include <TimeLib.h>
#include <WiFiUdp.h>
#include <Timezone.h>

#include "MyFunc.h"
#include <math.h>
#include "Solarlib.h"

//  pins.
#define ESP8266_GPIO2    2 // Blue LED.
#define ESP8266_GPIO4    4 // Relay control.
#define ESP8266_GPIO5    5 // Optocoupler input.

#define RelayPIN ESP8266_GPIO4
#define InputPIN ESP8266_GPIO5
#define LED_PIN ESP8266_GPIO2

#define MAX_SRV_CLIENTS 1

// ID of the settings block
#define CONFIG_VERSION "ls6"
uint8_t mac[WL_MAC_ADDR_LENGTH];
String MainPageNoneAuth(String msg = "");
String WifiSettingPage();

static TON StartupTON, KeyTON;
static Rtrg KeyTrg;

void InitFBDs()
{
	StartupTON.IN = 0;
	StartupTON.ET = 0;
	StartupTON.PT = 8000;
	StartupTON.PRE = 0;
	StartupTON.Q = 0;

	KeyTON.IN = 0;
	KeyTON.PRE = 0;
	KeyTON.Q = 0;
	KeyTON.PT = 4000;
	KeyTON.ET = 0;

	KeyTrg.IN = 0;
	KeyTrg.PRE = 0;
	KeyTrg.Q = 0;
}
// Example settings structure
struct TimeVar {
	uint8_t nHour;
	uint8_t nMinute;
	uint8_t nSec;
};

struct TimeSegment {
	struct TimeVar StartTime; // 3 byte
	struct TimeVar EndTime; // 3 byte
	uint8_t nDaySetting;
	uint16_t nMonthSetting;
};

#define POS 0
#define NEG 1

#define NONE 0
#define OVR 1

struct StoreStruct {
	// This is for mere detection if they are your settingsI ned 
	char version[4];
	// The variables of your settings
	uint16_t nHttpPort;
	uint16_t nTcpPort;
	uint16_t nServerPort;
	uint16_t nMqttPort;

	int8_t nTimeZone;
	uint8_t nOperMode;

	// IO mode parameters
	uint8_t P1;
	uint8_t P2;

	struct TimeSegment nSchedule1[6];
	struct TimeSegment nSchedule2[6];
	struct TimeSegment nSchedule3[6];
} storage = {
	CONFIG_VERSION,
	// The default values
	80,
	8000,
	7000,
	1883,
	-5,
	1,
	0,
	0,
};
String DeviceName = "Litmath_WifiRelay";
String DevPass = "WifiRelay123456";
String Passhint = "WifiRelay123456";
String AP_ssid = "Litmath_";
String AP_pass = "Litmath123";

String STA_ssid = "TP-LINK_64FB80";
String STA_pass = "chunxing151201";

String Mqtt_IP = "test.mosquitto.org";
String Mqtt_Input = "Home/Litmath/WifiRelay/Input";
String Mqtt_Output = "Home/Litmath/WifiRelay/Output";

static char mqtt_server[32] = "test.mosquitto.org";
static char mqtt_In[32] = "Home/Litmath/WifiRelay/Input";
static char mqtt_Out[32] = "Home/Litmath/WifiRelay/Output";
String TCPServerIP = "192.168.1.138";

static double lon = 0.0F;
static double lat = 0.0F;

static char mqtt_msg[512];

//US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = { "EDT", Second, Sun, Mar, 2, -240 };  //Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = { "EST", First, Sun, Nov, 2, -300 };   //Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);

//US Central Time Zone (Chicago, Houston)
TimeChangeRule usCDT = { "CDT", Second, dowSunday, Mar, 2, -300 };
TimeChangeRule usCST = { "CST", First, dowSunday, Nov, 2, -360 };
Timezone usCT(usCDT, usCST);

//US Mountain Time Zone (Denver, Salt Lake City)
TimeChangeRule usMDT = { "MDT", Second, dowSunday, Mar, 2, -360 };
TimeChangeRule usMST = { "MST", First, dowSunday, Nov, 2, -420 };
Timezone usMT(usMDT, usMST);

//US Pacific Time Zone (Las Vegas, Los Angeles)
TimeChangeRule usPDT = { "PDT", Second, dowSunday, Mar, 2, -420 };
TimeChangeRule usPST = { "PST", First, dowSunday, Nov, 2, -480 };
Timezone usPT(usPDT, usPST);

//US Alaska Time Zone 
TimeChangeRule usADT = { "ADT", Second, dowSunday, Mar, 2, -480 };
TimeChangeRule usAST = { "AST", First, dowSunday, Nov, 2, -540 };
Timezone usAT(usADT, usAST);

//US Hawaii Time Zone 
TimeChangeRule usHDT = { "HDT", Second, dowSunday, Mar, 2, -540 };
TimeChangeRule usHST = { "HST", First, dowSunday, Nov, 2, -600 };
Timezone usHT(usHDT, usHST);

void SetNewTimezone(int8_t nTimezone)
{
	switch (nTimezone)
	{
	case -5:
	{
		lon = -75.4;
		lat = 40.38;
	}
	break;

	case -6:
	{
		lon = -91.3;
		lat = 38.2;
	}
	break;

	case -7:
	{
		lon = -107.5;
		lat = 38.2;
	}
	break;

	case -8:
	{
		lon = -118.3;
		lat = 38.2;
	}
	break;

	case -9:
	{
		lon = -149.3;
		lat = 61.1;
	}
	break;

	case -10:
	{
		lon = -157.5;
		lat = 21.12;
	}
	break;
	}
	initSolarCalc(nTimezone, lat, lon);
}

// NTP
WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets
time_t getNtpTime();
void sendNTPpacket(IPAddress &address);
// NTP Servers:
static const char ntpServerName[] = "us.pool.ntp.org";

WiFiClient TCPclient;

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
	IPAddress ntpServerIP; // NTP server's ip address

	while (Udp.parsePacket() > 0); // discard any previously received packets
	Serial.println("Transmit NTP Request");
	// get a random server from the pool
	WiFi.hostByName(ntpServerName, ntpServerIP);
	Serial.print(ntpServerName);
	Serial.print(": ");
	Serial.println(ntpServerIP);
	sendNTPpacket(ntpServerIP);
	uint32_t beginWait = millis();
	while (millis() - beginWait < 1500) {
		int size = Udp.parsePacket();
		if (size >= NTP_PACKET_SIZE) {
			Serial.println("Receive NTP Response");
			Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
			unsigned long secsSince1900;
			// convert four bytes starting at location 40 to a long integer
			secsSince1900 = (unsigned long)packetBuffer[40] << 24;
			secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
			secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
			secsSince1900 |= (unsigned long)packetBuffer[43];
			TimeChangeRule *tcr;        //pointer to the time change rule, use to get the TZ abbrev
			time_t utc;
			time_t currtime;
			utc = secsSince1900 - 2208988800UL;
			switch (storage.nTimeZone)
			{
			case -5:
			{
				currtime = usET.toLocal(utc, &tcr);
			}
			break;

			case -6:
			{
				currtime = usCT.toLocal(utc, &tcr);
			}
			break;

			case -7:
			{
				currtime = usMT.toLocal(utc, &tcr);
			}
			break;

			case -8:
			{
				currtime = usPT.toLocal(utc, &tcr);
			}
			break;

			case -9:
			{
				currtime = usAT.toLocal(utc, &tcr);
			}
			break;

			case -10:
			{
				currtime = usHT.toLocal(utc, &tcr);
			}
			break;
			}
			return currtime;
		}
	}
	Serial.println("No NTP Response :-(");
	return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
	// set all bytes in the buffer to 0
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011;   // LI, Version, Mode
	packetBuffer[1] = 0;     // Stratum, or type of clock
	packetBuffer[2] = 6;     // Polling Interval
	packetBuffer[3] = 0xEC;  // Peer Clock Precision
							 // 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;
	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:
	Udp.beginPacket(address, 123); //NTP requests are to port 123
	Udp.write(packetBuffer, NTP_PACKET_SIZE);
	Udp.endPacket();
}

// Tell it where to store your config data in EEPROM
#define CONFIG_START 4
#define CONFIG_STRING 200
ESP8266WebServer *server;   //Web server object. Will be listening in port 80 (default for HTTP)
WiFiServer TCPServer(storage.nTcpPort);
WiFiClient TCPClients[MAX_SRV_CLIENTS];
WiFiServer *pTCPServer;

WiFiClient espClient;
PubSubClient client(espClient);

// WiFi Definitions.
static char ssid[32] = "";
static char pswd[32] = "";

volatile int relayState = 0;      // Relay state.
volatile bool RelayPermant = false;

volatile uint32_t nTimeMs = 0;

String RefreshPage(String msg = "");

time_t prevDisplay = 0; // when the digital clock was displayed

void callback(char* topic, byte* payload, unsigned int length) {
	String strPacket = "";
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("], ");
	Serial.print(length);
	Serial.print(",");
	for (int i = 0; i < length; i++) {
		Serial.print((char)payload[i]);
		strPacket += (char)payload[i];
	}
	Serial.println();
	strPacket = MqttCommadProc(strPacket);
	if (strPacket != "")
	{
		Serial.println(strPacket.length());
		strPacket.toCharArray(mqtt_msg, strPacket.length());
		delay(100);
		client.publish(mqtt_Out, mqtt_msg);
	}
}

/*

*/
bool is_authentified() {
	Serial.println("Enter is_authentified");
	if (server->hasHeader("Cookie")) {
		Serial.print("Found cookie: ");
		String cookie = server->header("Cookie");
		Serial.println(cookie);
		if (cookie.indexOf("ESPSESSIONID=1") != -1) {
			Serial.println("Authentification Successful");
			return true;
		}
	}
	Serial.println("Authentification Failed");
	return false;
}

void handleLogin() {
	String msg;
	if (server->hasHeader("Cookie")) {
		String cookie = server->header("Cookie");
	}
	if (server->hasArg("DISCONNECT")) {
		String header = "HTTP/1.1 301 OK\r\nSet-Cookie: ESPSESSIONID=0\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
		server->sendContent(header);
		return;
	}
	if (server->hasArg("PASS")) {
		if (server->arg("PASS") == DevPass) {
			Serial.println(server->arg("PASS"));
			String header = "HTTP/1.1 301 OK\r\nSet-Cookie: ESPSESSIONID=1\r\nLocation: /\r\nCache-Control: no-cache\r\n\r\n";
			server->sendContent(header);
			return;
		}
		msg = "Wrong password! try again.";
	}
	server->send(200, "text/html", MainPageNoneAuth(msg));
}

/**/
void handleRoot()
{
	if (!is_authentified()) {
		String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
		server->sendContent(header);
		return;
	}
	// Main Page
	server->send(200, "text/html", MainPage());
}

void loadConfig()
{
	// To make sure there are settings, and they are YOURS!
	// If nothing is found it will use the default settings.
	Serial.println(sizeof(storage));

	if (EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
		EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
		EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2])
	{
		for (unsigned int t = 0; t < sizeof(storage); t++)
			*((char*)&storage + t) = EEPROM.read(CONFIG_START + t);


		// load string varaibles
		DeviceName = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read(CONFIG_STRING + idx);
			if (ch == '\0')
				break;
			else
				DeviceName += String(ch);
		}

		DevPass = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 32) + idx);
			if (ch == '\0')
				break;
			else
				DevPass += String(ch);
		}

		Passhint = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 64) + idx);
			if (ch == '\0')
				break;
			else
				Passhint += String(ch);
		}

		AP_ssid = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 96) + idx);
			if (ch == '\0')
				break;
			else
				AP_ssid += String(ch);
		}

		AP_pass = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 128) + idx);
			if (ch == '\0')
				break;
			else
				AP_pass += String(ch);
		}

		STA_ssid = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 160) + idx);
			if (ch == '\0')
				break;
			else
				STA_ssid += String(ch);
		}

		STA_pass = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 192) + idx);
			if (ch == '\0')
				break;
			else
				STA_pass += String(ch);
		}

		Mqtt_IP = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 224) + idx);
			if (ch == '\0')
				break;
			else
				Mqtt_IP += String(ch);
		}

		Mqtt_Input = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 256) + idx);
			if (ch == '\0')
				break;
			else
				Mqtt_Input += String(ch);
		}

		Mqtt_Output = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 288) + idx);
			if (ch == '\0')
				break;
			else
				Mqtt_Output += String(ch);
		}

		TCPServerIP = "";
		for (uint8_t idx = 0; idx < 32; idx++)
		{
			char ch = (char)EEPROM.read((CONFIG_STRING + 320) + idx);
			if (ch == '\0')
				break;
			else
				TCPServerIP += String(ch);
		}
		Serial.print("TCPServerIP");
		Serial.println(TCPServerIP);
	}
	else
	{
		String macID = "";
		for (uint8_t idx = 0; idx < WL_MAC_ADDR_LENGTH; idx++)
		{
			if (mac[idx] < 16)
				macID += "0";
			macID += String(mac[idx], HEX);
		}

		macID.toUpperCase();
		AP_ssid += macID;

		for (uint8_t idx = 0; idx < 6; idx++)
		{
			storage.nSchedule1[idx].StartTime.nHour = 0;
			storage.nSchedule1[idx].StartTime.nMinute = 0;
			storage.nSchedule1[idx].StartTime.nSec = 0;
			storage.nSchedule1[idx].EndTime.nHour = 0;
			storage.nSchedule1[idx].EndTime.nMinute = 0;
			storage.nSchedule1[idx].EndTime.nSec = 0;
			storage.nSchedule1[idx].nDaySetting = 0;
			storage.nSchedule1[idx].nMonthSetting = 0;

			storage.nSchedule2[idx].StartTime.nHour = 0;
			storage.nSchedule2[idx].StartTime.nMinute = 0;
			storage.nSchedule2[idx].StartTime.nSec = 0;
			storage.nSchedule2[idx].EndTime.nHour = 0;
			storage.nSchedule2[idx].EndTime.nMinute = 0;
			storage.nSchedule2[idx].EndTime.nSec = 0;
			storage.nSchedule2[idx].nDaySetting = 0;
			storage.nSchedule2[idx].nMonthSetting = 0;

			storage.nSchedule3[idx].StartTime.nHour = 0;
			storage.nSchedule3[idx].StartTime.nMinute = 0;
			storage.nSchedule3[idx].StartTime.nSec = 0;
			storage.nSchedule3[idx].EndTime.nHour = 0;
			storage.nSchedule3[idx].EndTime.nMinute = 0;
			storage.nSchedule3[idx].EndTime.nSec = 0;
			storage.nSchedule3[idx].nDaySetting = 0;
			storage.nSchedule3[idx].nMonthSetting = 0;
		}
	}
}

void saveConfig()
{

	for (unsigned int t = 0; t<sizeof(storage); t++)
		EEPROM.write(CONFIG_START + t, *((char*)&storage + t));

	// store string varaibles
	uint8_t idx = 0;
	for (idx = 0; idx < DeviceName.length(); idx++)
		EEPROM.write(CONFIG_STRING + idx, DeviceName.charAt(idx));
	EEPROM.write(CONFIG_STRING + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 32) + idx, DevPass.charAt(idx));
	EEPROM.write((CONFIG_STRING + 32) + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 64) + idx, Passhint.charAt(idx));
	EEPROM.write((CONFIG_STRING + 64) + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 96) + idx, AP_ssid.charAt(idx));
	EEPROM.write((CONFIG_STRING + 96) + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 128) + idx, AP_pass.charAt(idx));
	EEPROM.write((CONFIG_STRING + 128) + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 160) + idx, STA_ssid.charAt(idx));
	EEPROM.write((CONFIG_STRING + 160) + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 192) + idx, STA_pass.charAt(idx));
	EEPROM.write((CONFIG_STRING + 192) + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 224) + idx, Mqtt_IP.charAt(idx));
	EEPROM.write((CONFIG_STRING + 224) + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 256) + idx, Mqtt_Input.charAt(idx));
	EEPROM.write((CONFIG_STRING + 256) + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 288) + idx, Mqtt_Output.charAt(idx));
	EEPROM.write((CONFIG_STRING + 288) + idx, '\0');

	for (idx = 0; idx < 32; idx++)
		EEPROM.write((CONFIG_STRING + 320) + idx, TCPServerIP.charAt(idx));
	EEPROM.write((CONFIG_STRING + 320) + idx, '\0');

	EEPROM.commit();
}

void initHardware() {
	pinMode(RelayPIN, OUTPUT);
	pinMode(InputPIN, INPUT_PULLUP);
	pinMode(ESP8266_GPIO2, OUTPUT);
	digitalWrite(RelayPIN, LOW);
	nTimeMs = millis();
	Serial.begin(9600);
	Serial.println("Started!");
	EEPROM.begin(1024);
}

void connectWiFi() {
	byte ledStatus = LOW;

	uint8_t idx = 0;
	WiFi.disconnect();
	WiFi.mode(WIFI_OFF);
	delay(1000);

	WiFi.mode(WIFI_AP_STA);
	for (idx = 0; idx < STA_ssid.length(); idx++)
		ssid[idx] = STA_ssid.charAt(idx);
	ssid[idx] = '\0';

	for (idx = 0; idx < STA_pass.length(); idx++)
		pswd[idx] = STA_pass.charAt(idx);
	pswd[idx] = '\0';

	WiFi.begin(ssid, pswd);
	Serial.println("\nConnecting to: " + String(ssid));
	Serial.println("Password is " + String(pswd));

	uint32_t ntick = millis();
	while (WiFi.status() != WL_CONNECTED) {
		// Blink the LED.
		digitalWrite(LED_PIN, ledStatus); // Write LED high/low.
		ledStatus = (ledStatus == HIGH) ? LOW : HIGH;
		delay(100);
		if ((ntick + 15000) < millis()) // 10sec
		{
			WiFi.disconnect();
			break;
		}
	}
	digitalWrite(LED_PIN, LOW); // Write LED high/low.

	for (idx = 0; idx < AP_ssid.length(); idx++)
		ssid[idx] = AP_ssid.charAt(idx);
	ssid[idx] = '\0';

	for (idx = 0; idx < AP_pass.length(); idx++)
		pswd[idx] = AP_pass.charAt(idx);
	pswd[idx] = '\0';

	WiFi.softAP(ssid, pswd);

	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());

	if (WiFi.status() == WL_CONNECTED)
	{
		IPAddress Addr;
		Addr.fromString(TCPServerIP);
		Serial.print("I am conecting IP:");
		Serial.print(Addr);
		Serial.print(", Port:");
		Serial.println(storage.nServerPort);
		if (!TCPclient.connect(Addr, storage.nServerPort))
			Serial.println("I can't connect to specified TCP server!");
		else
			Serial.println("I have connected to specified TCP server!");

	}
}

void OpenRelayProc()
{
	String message;
	String strTimeOut = server->arg("t");
	// get timeout variable as second unit
	int nTimeOut = strTimeOut.toInt();

	if (is_authentified())
	{
		if (nTimeOut == 0)
		{
			RelayPermant = true;
			relayState = true;
		}
		else
		{
			RelayPermant = false;
			nTimeMs = millis() + (nTimeOut * 1000);
			relayState = true;
		}
		message += DeviceName;
		message += " Opened.\n";
	}
	else
	{
		if (!server->hasArg("pp") || server->arg("pp") != DevPass)
			message += "Invalid Command Format or invalid password!";
		else
		{
			if (nTimeOut == 0)
			{
				RelayPermant = true;
				relayState = true;
			}
			else
			{
				RelayPermant = false;
				nTimeMs = millis() + (nTimeOut * 1000);
				relayState = true;
			}
			message += DeviceName;
			message += " Opened.\n";
		}

	}
	server->send(200, "text/html", UtilityPage(message));
}

void InitServer()
{
	server = new ESP8266WebServer(storage.nHttpPort);

	server->on("/", handleRoot);
	server->on("/login", handleLogin);
	server->on("/defaultWifiSetting", DefaultSoftAP);
	server->on("/WifiSetting", WifiSetting);
	server->on("/Wifi", WifiAPProc);
	server->on("/SoftAP", SoftAPProc);
	server->on("/Settings", SettingProc);
	server->on("/RelayName", RelayNameProc);
	server->on("/Pass", PasswordProc);
	server->on("/Port", PortProc);
	server->on("/MODE", ModeTemp);
	server->on("/Mode", ModeProc);
	server->on("/Mqtt", MqttProc);
	server->on("/time", TimeProc);
	server->on("/timers", TimersProc);
	server->on("/Client", ClientProc);

	server->on("/OpenRelay", OpenRelayProc);
	server->on("/CloseRelay", CloseRelayProc);
	server->on("/MAC", MacProc);
	server->on("/status", statusProc);
	server->on("/iostatus", iostatusProc);


	//here the list of headers to be recorded
	const char * headerkeys[] = { "User-Agent","Cookie" };
	size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
	//ask server to track these headers
	server->collectHeaders(headerkeys, headerkeyssize);
	server->begin(); //Start the Web server
	pTCPServer = new WiFiServer(storage.nTcpPort);
	pTCPServer->begin();

	Serial.println("Server listening");
}

void reconnect() {
	// Loop until we're reconnected
	while (!client.connected()) {
		//Mqtt_Output.toCharArray(mqtt_Out, Mqtt_Output.length());
		//Mqtt_Input.toCharArray(mqtt_In, Mqtt_Input.length());
		Serial.print("Attempting MQTT connection...");
		// Create a random client ID
		String clientId = "Roberto-";
		clientId += String(random(0xffff), HEX);
		// Attempt to connect
		if (client.connect(clientId.c_str())) {
			Serial.println("connected");
			// Once connected, publish an announcement...
			client.publish(mqtt_Out, "I am ready!\r\n");
			// ... and resubscribe
			client.subscribe(mqtt_In);
		}
		else {
			Serial.print("failed, rc=");
			Serial.print(client.state());
			Serial.println(" try again in 5 seconds");
			// Wait 5 seconds before retrying
			delay(5000);
		}
	}
}

void mqttconnect() {
	// Loop until we're reconnected
	if (!client.connected()) {
		Serial.print("Attempting MQTT connection...");
		// Create a random client ID
		String clientId = "mytest-";
		clientId += String(random(0xffff), HEX);
		// Attempt to connect
		if (client.connect(clientId.c_str())) {
			Serial.println("connected");
			// Once connected, publish an announcement...
			client.publish(mqtt_Out, "I am ready!\r\n");
			// ... and resubscribe
			client.subscribe(mqtt_In);
		}
		else {
			Serial.print("failed, rc=");
			Serial.print(client.state());
			Serial.println(" try again in 5 seconds");
			// Wait 5 seconds before retrying
			delay(5000);
		}
	}
}

void setup()
{
	delay(1000); //
	initHardware();
	// Get Wifi Mac Address
	WiFi.macAddress(mac);
	loadConfig();
	connectWiFi();
	if (WiFi.status() != WL_CONNECTED)
	{
		delay(1000);
		connectWiFi();
	}
	InitServer();

	delay(500);
	// digital clock display of the time
	Udp.begin(localPort);
	time_t time = getNtpTime();
	if (time != 0)
		setTime(time);

	//setSyncProvider(getNtpTime);

	uint8_t idx = 0;
	for (idx = 0; idx < Mqtt_IP.length(); idx++)
		mqtt_server[idx] = Mqtt_IP.charAt(idx);
	mqtt_server[idx] = '\0';

	client.setServer(mqtt_server, storage.nMqttPort);
	client.setCallback(callback);
	mqttconnect();
	//reconnect();
	Serial.print(hour());
	Serial.print(" ");
	Serial.print(minute());
	Serial.print(" ");
	Serial.print(second());
	Serial.print(" ");
	Serial.print(day());
	Serial.print(".");
	Serial.print(month());
	Serial.print(".");
	Serial.print(year());
	Serial.println();

	InitFBDs();
	SetNewTimezone(storage.nTimeZone);
}


String GetQueryValue(String QueryName, String Query)
{

	String result = "";
	QueryName += "=";
	int nFirstPoint = Query.indexOf(QueryName);

	if (nFirstPoint < 0)
		return "";
	nFirstPoint += (QueryName.length() - 1);
	int nEndPoint = Query.indexOf(",", nFirstPoint + 1);
	if (nEndPoint < 0)
		result = Query.substring(nFirstPoint + 1, Query.length());
	else
		result = Query.substring(nFirstPoint + 1, nEndPoint);
	return result;
}

String CommandProc(String strPacket)
{
	String strPrefix;
	String strtemp = "";
	for (uint8_t idx = 0; idx < strPacket.length(); idx++)
	{
		char ch = strPacket.charAt(idx);
		if (ch != 0x0D && ch != 0x0A)
			strtemp += (char)ch;
	}
	strPacket = strtemp;

	String message;
	uint8_t nCmdIdx = strPacket.indexOf("?");
	strPrefix = strPacket.substring(0, nCmdIdx);
	strPacket = strPacket.substring(nCmdIdx + 1, strPacket.length());
	Serial.println(strPacket);
	Serial.println(GetQueryValue("Name", strPacket));
	Serial.println(DeviceName);

	if (GetQueryValue("Name", strPacket) != DeviceName)
	{
		return "";
	}
	if (strPrefix.equals("OpenRelay"))
	{
		String strtime = GetQueryValue("t", strPacket);
		String strPass = GetQueryValue("pp", strPacket);
		if (strtime.length() == 0 || strPass.length() == 0)
			message += "Invalid command format!";
		else
		{
			uint16_t nTimeOut = strtime.toInt();
			if (strPass == DevPass)
			{
				if (nTimeOut == 0)
				{
					RelayPermant = true;
					relayState = true;
				}
				else
				{
					RelayPermant = false;
					nTimeMs = millis() + (nTimeOut * 1000);
					relayState = true;
				}
			}
			else
				message = DeviceName + "Invalid password!\r\n";
		}
	}
	else if (strPrefix == "CloseRelay")
	{
		String strPass = GetQueryValue("pp", strPacket);
		if (strPass == DevPass)
		{
			RelayPermant = false;
			nTimeMs = millis();
			relayState = false;
		}
		else
			message = DeviceName + "Invalid password!\r\n";
	}
	else if (strPrefix == "timers")
	{
		uint8_t nUnit = GetQueryValue("u", strPacket).toInt();
		uint8_t nSchNum = GetQueryValue("sch", strPacket).toInt();
		String Startime = GetQueryValue("st", strPacket);
		String Stoptime = GetQueryValue("en", strPacket);
		String DaySetting = GetQueryValue("d", strPacket);
		String MonthSetting = GetQueryValue("m", strPacket);

		if (nUnit < 1 || nUnit > 6)
		{
			message = DeviceName + ",Error=02,Valid range=1~6\r\n";
			return message;
		}
		if (nSchNum < 1 || nSchNum > 3)
		{
			message = DeviceName + ",Error=01,Valid range=1~3\r\n";
			return message;
		}
		if (Startime.length() != 10 && ((Startime.substring(0, 2) != "SS") && (Startime.substring(0, 2) != "SR")))
		{
			message = DeviceName + ",Error=30,format:HH:MM:SSAM/PM or SS/SR()P/M(+/-)MMM\r\n";
			return message;
		}
		if (Stoptime.length() != 10 && ((Stoptime.substring(0, 2) != "SS") && (Stoptime.substring(0, 2) != "SR")))
		{
			message = DeviceName + ",Error=40,format:HH:MM:SSAM/PM or SS/SR()P/M(+/-)MMM\r\n";
			return message;
		}
		if (DaySetting.length() != 7)
		{
			message = DeviceName + ",Error=05,1111111:Sun,Mon,Tue,Wde,Tur,Fri,Sat\r\n";
			return message;
		}
		if (MonthSetting.length() != 12)
		{
			message = DeviceName + ",Error=06,111111111111:1,2,3,4,5,6,,8,9,10,11,12\r\n";
			return message;
		}

		nUnit--;

		switch (nSchNum)
		{

		case 1:
		{
			for (uint8_t idx = 0; idx < 7; idx++)
			{
				if (DaySetting.charAt(idx) == '1')
					bitSet(storage.nSchedule1[nUnit].nDaySetting, idx);
				else
					bitClear(storage.nSchedule1[nUnit].nDaySetting, idx);

			}

			for (uint8_t idx = 0; idx < 12; idx++)
			{
				if (MonthSetting.charAt(idx) == '1')
					bitSet(storage.nSchedule1[nUnit].nMonthSetting, idx);
				else
					bitClear(storage.nSchedule1[nUnit].nMonthSetting, idx);
			}

			if (Startime.substring(0, 2) == "SS" || Startime.substring(0, 2) == "SR")
			{
				time_t Settingtime;
				bool sign = true;
				if (Startime.charAt(2) == 'P')
					sign = true;
				else
					sign = false;
				uint16_t nMinute = Startime.substring(3, Startime.length()).toInt();


				if (Startime.substring(0, 2) == "SS")
				{ // Sunset
					Settingtime = getSunsetTime(now());
				}
				else
				{
					Settingtime = getSunriseTime(now());
				}
				if (sign)
					Settingtime += nMinute * 60;
				else
					Settingtime -= nMinute * 60;

				storage.nSchedule1[nUnit].StartTime.nHour = hour(Settingtime);
				storage.nSchedule1[nUnit].StartTime.nMinute = minute(Settingtime);
				storage.nSchedule1[nUnit].StartTime.nSec = second(Settingtime);
			}
			else
			{
				storage.nSchedule1[nUnit].StartTime.nHour = Startime.substring(0, 2).toInt();
				storage.nSchedule1[nUnit].StartTime.nMinute = Startime.substring(3, 5).toInt();
				storage.nSchedule1[nUnit].StartTime.nSec = Startime.substring(6, 8).toInt();
				if (Startime.substring(8, 10) == "PM")
					storage.nSchedule1[nUnit].StartTime.nHour += 12;
			}

			if (Stoptime.substring(0, 2) == "SS" || Stoptime.substring(0, 2) == "SR")
			{
				time_t Settingtime;
				bool sign = true;
				if (Stoptime.charAt(2) == 'P')
					sign = true;
				else
					sign = false;
				uint16_t nMinute = Stoptime.substring(3, Stoptime.length()).toInt();


				if (Stoptime.substring(0, 2) == "SS") // Sunset
					Settingtime = getSunsetTime(now());
				else
					Settingtime = getSunriseTime(now());

				if (sign)
					Settingtime += nMinute * 60;
				else
					Settingtime -= nMinute * 60;

				storage.nSchedule1[nUnit].EndTime.nHour = hour(Settingtime);
				storage.nSchedule1[nUnit].EndTime.nMinute = minute(Settingtime);
				storage.nSchedule1[nUnit].EndTime.nSec = second(Settingtime);
			}
			else
			{
				storage.nSchedule1[nUnit].EndTime.nHour = Stoptime.substring(0, 2).toInt();
				storage.nSchedule1[nUnit].EndTime.nMinute = Stoptime.substring(3, 5).toInt();
				storage.nSchedule1[nUnit].EndTime.nSec = Stoptime.substring(6, 8).toInt();
				if (Stoptime.substring(8, 10) == "PM")
					storage.nSchedule1[nUnit].EndTime.nHour += 12;
			}

			message = "Schedule1, ";
			message += "Unit";
			message += String(nUnit + 1);
			message += ", Start:";
			message += TimeVartoString(storage.nSchedule1[nUnit].StartTime);
			message += ", Stop:";
			message += TimeVartoString(storage.nSchedule1[nUnit].EndTime);
			message += "\r\n";

			message += "Day:";
			for (uint8_t index = 0; index < 7; index++)
				message += String(bitRead(storage.nSchedule1[nUnit].nDaySetting, index));
			message += ",Month:";
			for (uint8_t index = 0; index < 12; index++)
				message += String(bitRead(storage.nSchedule1[nUnit].nMonthSetting, index));
			message += "\r\n";
		}
		break;
		case 2:
		{
			for (uint8_t idx = 0; idx < 7; idx++)
			{
				if (DaySetting.charAt(idx) == '1')
					bitSet(storage.nSchedule2[nUnit].nDaySetting, idx);
				else
					bitClear(storage.nSchedule2[nUnit].nDaySetting, idx);

			}

			for (uint8_t idx = 0; idx < 12; idx++)
			{
				if (MonthSetting.charAt(idx) == '1')
					bitSet(storage.nSchedule2[nUnit].nMonthSetting, idx);
				else
					bitClear(storage.nSchedule2[nUnit].nMonthSetting, idx);
			}

			if (Startime.substring(0, 2) == "SS" || Startime.substring(0, 2) == "SR")
			{
				time_t Settingtime;
				bool sign = true;
				if (Startime.charAt(2) == 'P')
					sign = true;
				else
					sign = false;
				uint16_t nMinute = Startime.substring(3, Startime.length()).toInt();


				if (Startime.substring(0, 2) == "SS")
				{ // Sunset
					Settingtime = getSunsetTime(now());
				}
				else
				{
					Settingtime = getSunriseTime(now());
				}
				if (sign)
					Settingtime += nMinute * 60;
				else
					Settingtime -= nMinute * 60;

				storage.nSchedule2[nUnit].StartTime.nHour = hour(Settingtime);
				storage.nSchedule2[nUnit].StartTime.nMinute = minute(Settingtime);
				storage.nSchedule2[nUnit].StartTime.nSec = second(Settingtime);
			}
			else
			{
				storage.nSchedule2[nUnit].StartTime.nHour = Startime.substring(0, 2).toInt();
				storage.nSchedule2[nUnit].StartTime.nMinute = Startime.substring(3, 5).toInt();
				storage.nSchedule2[nUnit].StartTime.nSec = Startime.substring(6, 8).toInt();
				if (Startime.substring(8, 10) == "PM")
					storage.nSchedule2[nUnit].StartTime.nHour += 12;
			}

			if (Stoptime.substring(0, 2) == "SS" || Stoptime.substring(0, 2) == "SR")
			{
				time_t Settingtime;
				bool sign = true;
				if (Stoptime.charAt(2) == 'P')
					sign = true;
				else
					sign = false;
				uint16_t nMinute = Stoptime.substring(3, Stoptime.length()).toInt();


				if (Stoptime.substring(0, 2) == "SS") // Sunset
					Settingtime = getSunsetTime(now());
				else
					Settingtime = getSunriseTime(now());

				if (sign)
					Settingtime += nMinute * 60;
				else
					Settingtime -= nMinute * 60;

				storage.nSchedule2[nUnit].EndTime.nHour = hour(Settingtime);
				storage.nSchedule2[nUnit].EndTime.nMinute = minute(Settingtime);
				storage.nSchedule2[nUnit].EndTime.nSec = second(Settingtime);
			}
			else
			{
				storage.nSchedule2[nUnit].EndTime.nHour = Stoptime.substring(0, 2).toInt();
				storage.nSchedule2[nUnit].EndTime.nMinute = Stoptime.substring(3, 5).toInt();
				storage.nSchedule2[nUnit].EndTime.nSec = Stoptime.substring(6, 8).toInt();
				if (Stoptime.substring(8, 10) == "PM")
					storage.nSchedule2[nUnit].EndTime.nHour += 12;
			}

			message = "Schedule2, ";
			message += "Unit";
			message += String(nUnit + 1);
			message += ", Start:";
			message += TimeVartoString(storage.nSchedule2[nUnit].StartTime);
			message += ", Stop:";
			message += TimeVartoString(storage.nSchedule2[nUnit].EndTime);
			message += "\r\n";

			message += "Day:";
			for (uint8_t index = 0; index < 7; index++)
				message += String(bitRead(storage.nSchedule2[nUnit].nDaySetting, index));
			message += ",Month:";
			for (uint8_t index = 0; index < 12; index++)
				message += String(bitRead(storage.nSchedule2[nUnit].nMonthSetting, index));
			message += "\r\n";

		}
		break;
		case 3:
		{
			for (uint8_t idx = 0; idx < 7; idx++)
			{
				if (DaySetting.charAt(idx) == '1')
					bitSet(storage.nSchedule3[nUnit].nDaySetting, idx);
				else
					bitClear(storage.nSchedule3[nUnit].nDaySetting, idx);

			}

			for (uint8_t idx = 0; idx < 12; idx++)
			{
				if (MonthSetting.charAt(idx) == '1')
					bitSet(storage.nSchedule3[nUnit].nMonthSetting, idx);
				else
					bitClear(storage.nSchedule3[nUnit].nMonthSetting, idx);
			}

			if (Startime.substring(0, 2) == "SS" || Startime.substring(0, 2) == "SR")
			{
				time_t Settingtime;
				bool sign = true;
				if (Startime.charAt(2) == 'P')
					sign = true;
				else
					sign = false;
				uint16_t nMinute = Startime.substring(3, Startime.length()).toInt();


				if (Startime.substring(0, 2) == "SS")
				{ // Sunset
					Settingtime = getSunsetTime(now());
				}
				else
				{
					Settingtime = getSunriseTime(now());
				}
				if (sign)
					Settingtime += nMinute * 60;
				else
					Settingtime -= nMinute * 60;

				storage.nSchedule3[nUnit].StartTime.nHour = hour(Settingtime);
				storage.nSchedule3[nUnit].StartTime.nMinute = minute(Settingtime);
				storage.nSchedule3[nUnit].StartTime.nSec = second(Settingtime);
			}
			else
			{
				storage.nSchedule3[nUnit].StartTime.nHour = Startime.substring(0, 2).toInt();
				storage.nSchedule3[nUnit].StartTime.nMinute = Startime.substring(3, 5).toInt();
				storage.nSchedule3[nUnit].StartTime.nSec = Startime.substring(6, 8).toInt();
				if (Startime.substring(8, 10) == "PM")
					storage.nSchedule3[nUnit].StartTime.nHour += 12;
			}

			if (Stoptime.substring(0, 2) == "SS" || Stoptime.substring(0, 2) == "SR")
			{
				time_t Settingtime;
				bool sign = true;
				if (Stoptime.charAt(2) == 'P')
					sign = true;
				else
					sign = false;
				uint16_t nMinute = Stoptime.substring(3, Stoptime.length()).toInt();


				if (Stoptime.substring(0, 2) == "SS") // Sunset
					Settingtime = getSunsetTime(now());
				else
					Settingtime = getSunriseTime(now());

				if (sign)
					Settingtime += nMinute * 60;
				else
					Settingtime -= nMinute * 60;

				storage.nSchedule3[nUnit].EndTime.nHour = hour(Settingtime);
				storage.nSchedule3[nUnit].EndTime.nMinute = minute(Settingtime);
				storage.nSchedule3[nUnit].EndTime.nSec = second(Settingtime);
			}
			else
			{
				storage.nSchedule3[nUnit].EndTime.nHour = Stoptime.substring(0, 2).toInt();
				storage.nSchedule3[nUnit].EndTime.nMinute = Stoptime.substring(3, 5).toInt();
				storage.nSchedule3[nUnit].EndTime.nSec = Stoptime.substring(6, 8).toInt();
				if (Stoptime.substring(8, 10) == "PM")
					storage.nSchedule3[nUnit].EndTime.nHour += 12;
			}
			message = "Schedule3, ";
			message += "Unit";
			message += String(nUnit + 1);
			message += ", Start:";
			message += TimeVartoString(storage.nSchedule3[nUnit].StartTime);
			message += ", Stop:";
			message += TimeVartoString(storage.nSchedule3[nUnit].EndTime);
			message += "\r\n";

			message += "Day:";
			for (uint8_t index = 0; index < 7; index++)
				message += String(bitRead(storage.nSchedule3[nUnit].nDaySetting, index));
			message += ",Month:";
			for (uint8_t index = 0; index < 12; index++)
				message += String(bitRead(storage.nSchedule3[nUnit].nMonthSetting, index));
			message += "\r\n";
		}
		break;
		}
		saveConfig();

	}
	else if (strPrefix == "Mode")
	{
		String strPass = GetQueryValue("pp", strPacket);
		if (strPass.length() == 0)
			message += "Invalid command format!";
		else
		{
			if (strPass == DevPass)
			{
				if (GetQueryValue("Choice", strPacket).toInt() < 1 || GetQueryValue("Choice", strPacket).toInt() > 6)
				{
					message = DeviceName + ",Error=11,valid range 1~6\r\n";
					return message;
				}
				if (GetQueryValue("P1", strPacket).toInt() > 2)
				{
					message = DeviceName + ",Error=12,Parameter1 Error, 0:Pos, 1: Neg\r\n";
					return message;
				}
				if (GetQueryValue("P2", strPacket).toInt() > 2)
				{
					message = DeviceName + ",Error=13,Parameter2 Error, 0:OVR, 1: None\r\n";
					return message;
				}
				storage.nOperMode = GetQueryValue("Choice", strPacket).toInt();
				storage.P1 = GetQueryValue("P1", strPacket).toInt();
				storage.P2 = GetQueryValue("P2", strPacket).toInt();

				// 
				saveConfig();

				// Generate Response
				message = DeviceName + ",Change,Mode=";
				message += String(storage.nOperMode);
				message += ",pos=";
				message += String(storage.P1);
				message += ",ovr=";
				message += String(storage.P2);
				message += "\r\n";

				switch (storage.nOperMode)
				{
				case 3:
				{
					message += "Schedule1 Control Mode.\r\n";
					for (uint8_t idx = 0; idx < 6; idx++)
					{
						String strtemp = "Schedule1, ";
						strtemp += "Unit";
						strtemp += String(idx);
						strtemp += ":StartTime-";
						strtemp += TimeVartoString(storage.nSchedule1[idx].StartTime);
						strtemp += ",EndTime-";
						strtemp += TimeVartoString(storage.nSchedule1[idx].EndTime);
						strtemp += ",Day:";
						for (uint8_t index = 0; index < 7; index++)
							strtemp += String(bitRead(storage.nSchedule1[idx].nDaySetting, 6 - index));
						strtemp += ",Month:";
						for (uint8_t index = 0; index < 12; index++)
							strtemp += String(bitRead(storage.nSchedule1[idx].nMonthSetting, 11 - index));
						strtemp += "\r\n";

						strtemp.toCharArray(mqtt_msg, strtemp.length());
						if (client.connected())
							client.publish(mqtt_Out, mqtt_msg);

					}
					if (storage.P2 == OVR)
					{
						message += "Overlay, ";
						if (storage.P1 == POS)
							message += "Positive.\r\n";
						else
							message += "Negative.\r\n";
					}

				}
				break;

				case 4:
				{
					message += "Schedule2 Control Mode.\r\n";
					if (storage.P2 == OVR)
					{
						message += "Overlay, ";
						if (storage.P1 == POS)
							message += "Positive\r\n";
						else
							message += "Negative\r\n";
					}
					for (uint8_t idx = 0; idx < 6; idx++)
					{
						String strtemp = "Schedule2, ";
						strtemp += "Unit";
						strtemp += String(idx);
						strtemp += ":StartTime-";
						strtemp += TimeVartoString(storage.nSchedule2[idx].StartTime);
						strtemp += ",EndTime-";
						strtemp += TimeVartoString(storage.nSchedule2[idx].EndTime);
						strtemp += ",Day:";
						for (uint8_t index = 0; index < 7; index++)
							strtemp += String(bitRead(storage.nSchedule2[idx].nDaySetting, 6 - index));
						strtemp += ",Month:";
						for (uint8_t index = 0; index < 12; index++)
							strtemp += String(bitRead(storage.nSchedule2[idx].nMonthSetting, 11 - index));
						strtemp += "\r\n";

						strtemp.toCharArray(mqtt_msg, strtemp.length());
						if (client.connected())
							client.publish(mqtt_Out, mqtt_msg);

					}
				}
				break;

				case 5:
				{
					message += "Schedule3 Control Mode.\r\n";
					if (storage.P2 == OVR)
					{
						message += "Overlay, ";
						if (storage.P1 == POS)
							message += "Setting.\r\n";
						else
							message += "Setting.\r\n";
					}
					for (uint8_t idx = 0; idx < 6; idx++)
					{
						String strtemp = "Schedule3, ";
						strtemp += "Unit";
						strtemp += String(idx);
						strtemp += ":StartTime-";
						strtemp += TimeVartoString(storage.nSchedule3[idx].StartTime);
						strtemp += ",EndTime-";
						strtemp += TimeVartoString(storage.nSchedule3[idx].EndTime);
						strtemp += ",Day:";
						for (uint8_t index = 0; index < 7; index++)
							strtemp += String(bitRead(storage.nSchedule3[idx].nDaySetting, 6 - index));
						strtemp += ",Month:";
						for (uint8_t index = 0; index < 12; index++)
							strtemp += String(bitRead(storage.nSchedule3[idx].nMonthSetting, 11 - index));
						strtemp += "\r\n";

						strtemp.toCharArray(mqtt_msg, strtemp.length());
						if (client.connected())
							client.publish(mqtt_Out, mqtt_msg);

					}
				}
				break;
				}

			}
			else
				message += "Invalid password! \r\n You can not this action without correct password.";
		}
	}
	else if (strPrefix == "MAC")
	{
		String strPass = GetQueryValue("pp", strPacket);
		if (strPass.length() == 0)
			message = DeviceName + "Invalid command format!";
		else
		{
			if (strPass == DevPass)
			{
				String macID;
				for (uint8_t idx = 0; idx < WL_MAC_ADDR_LENGTH; idx++)
				{
					if (mac[idx] < 16)
						macID += "0";
					macID += String(mac[idx], HEX);
				}
				message = DeviceName;
				macID.toUpperCase();
				message += ",Mac=";
				message += macID;
				message += ".\r\n";
			}
			else
				message = DeviceName + "Invalid password!\r\n";
		}
	}
	else if (strPrefix == "status")
	{
		String strPass = GetQueryValue("pp", strPacket);
		if (strPass.length() == 0)
			message += "Invalid command format!";
		else
		{
			if (strPass == DevPass)
			{
				message = DeviceName + ",status=";
				message += String(relayState);
				message += "\r\n";

				/*
				if (relayState)
				message += " Opened.\r\n";
				else
				message += " Closed.\r\n";
				*/
			}
			else
				message = DeviceName + "Invalid password!\r\n";
		}
	}
	else if (strPrefix == "iostatus")
	{
		String strPassword = GetQueryValue("pp", strPacket);
		if (DevPass == strPassword)
		{
			bool bStatus = digitalRead(InputPIN);
			message = DeviceName + ",IO status=";
			message += String(bStatus);
			message += "\r\n";
			/*
			if (bStatus)
			message += ", IO Status HIGH.\r\n";
			else
			message += ", IO Status LOW.\r\n";
			*/
		}
		else
			message = DeviceName + "Invalid Password.\r\n";
	}
	else if (strPrefix == "Port")
	{
		String strPassword = GetQueryValue("pp", strPacket);

		if (DevPass == strPassword)
		{
			storage.nHttpPort = GetQueryValue("http", strPacket).toInt();
			storage.nTcpPort = GetQueryValue("tcp", strPacket).toInt();
			saveConfig();
			message += "http=";
			message += String(storage.nHttpPort);
			message += ",tcp=";
			message += String(storage.nTcpPort);
			message += "\r\n";
			ResponsetoCommand(message);
			ESP.restart();
		}
		else
			message = DeviceName + "Invalid Password.\r\n";
	}
	else
		message = DeviceName + "Invalid Command";
	return message;
}

String TCPCommandProc(String strPacket)
{
	return CommandProc(strPacket);
}

String MqttCommadProc(String strPacket)
{
	return CommandProc(strPacket);
}

bool PreRelstatus = false;
bool PreInputStatus = false;
void loop()
{

	server->handleClient();    //Handling of incoming requests

							   /**/
	StartupTON.IN = 1;
	TONFunc(&StartupTON);
	KeyTON.IN = StartupTON.Q == 0 && digitalRead(InputPIN) == LOW;
	TONFunc(&KeyTON);
	KeyTrg.IN = KeyTON.Q;
	RTrgFunc(&KeyTrg);

	if (KeyTrg.Q)
	{// Default device Password Process
		DevPass = "WifiRelay123456";
		AP_pass = "Litmath123";
		Serial.println("I will reset password.");
		saveConfig();
		ESP.restart();
	}

	long time = millis();
	uint8_t i;
	if (pTCPServer->hasClient())
	{
		for (i = 0; i < MAX_SRV_CLIENTS; i++)
		{
			//find free/disconnected spot
			if (!TCPClients[i] || !TCPClients[i].connected())
			{
				if (TCPClients[i])
					TCPClients[i].stop();
				TCPClients[i] = pTCPServer->available();
				continue;
			}
		}
		//no free/disconnected spot so reject
		WiFiClient serverClient = pTCPServer->available();
		serverClient.stop();
	}

	//check clients for data
	for (i = 0; i < MAX_SRV_CLIENTS; i++) {
		if (TCPClients[i] && TCPClients[i].connected()) {
			if (TCPClients[i].available()) {
				//get data from the telnet client and push it to the UART
				while (TCPClients[i].available())
				{
					String strCommand = TCPClients[i].readString();
					strCommand = TCPCommandProc(strCommand);
					TCPClients[i].println(strCommand);
				}
			}
		}
	}

	// Control loop
	if (StartupTON.Q)
	{
		switch (storage.nOperMode)
		{
		case 1:// Normal Mode
		{
			if (nTimeMs < millis() && !RelayPermant)
				relayState = false;
			else
				relayState = true;

			if (storage.P2 == OVR)
			{
				if (storage.P1 == POS && digitalRead(InputPIN) == HIGH)
					relayState = false;
				else if (storage.P1 == NEG && digitalRead(InputPIN) == LOW)
					relayState = false;
			}
		}
		break;

		case 2: // IO MODE
		{
			if (storage.P1 == POS)
			{
				if (digitalRead(InputPIN))
					relayState = true;
				else
					relayState = false;
			}
			else
			{
				if (digitalRead(InputPIN))
					relayState = false;
				else
					relayState = true;
			}
		}
		break;

		case 3: // Schedule1 Mode
		{
			bool bresult = false;
			for (uint8_t idx = 0; idx < 6; idx++)
			{
				if (bitRead(storage.nSchedule1[idx].nMonthSetting, (month() - 1)))
				{
					if (bitRead(storage.nSchedule1[idx].nDaySetting, (weekday(now()) - 1)))
					{
						uint32_t Starttime = 0;
						uint32_t Endtime = 0;
						uint32_t Currtime = 0;
						Starttime = storage.nSchedule1[idx].StartTime.nHour;
						Starttime <<= 8;
						Starttime += storage.nSchedule1[idx].StartTime.nMinute;
						Starttime <<= 8;
						Starttime += storage.nSchedule1[idx].StartTime.nSec;

						Endtime = storage.nSchedule1[idx].EndTime.nHour;
						Endtime <<= 8;
						Endtime += storage.nSchedule1[idx].EndTime.nMinute;
						Endtime <<= 8;
						Endtime += storage.nSchedule1[idx].EndTime.nSec;

						Currtime = ((uint8_t)hour() << 16);
						Currtime += (minute() << 8);
						Currtime += second();

						if (Currtime > Starttime && Currtime < Endtime)
							bresult = true;
					}
				}
			}
			relayState = bresult;
			if (storage.P2 == OVR)
			{
				if (storage.P1 == POS && digitalRead(InputPIN) == HIGH)
					relayState = false;
				else if (storage.P1 == NEG && digitalRead(InputPIN) == LOW)
					relayState = false;
			}
		}
		break;

		case 4: // Schedule2 Mode
		{
			bool bresult = false;
			for (uint8_t idx = 0; idx < 6; idx++)
			{
				if (bitRead(storage.nSchedule2[idx].nMonthSetting, month() - 1))
				{
					if (bitRead(storage.nSchedule2[idx].nDaySetting, weekday(now()) - 1))
					{
						uint32_t Starttime = 0;
						uint32_t Endtime = 0;
						uint32_t Currtime = 0;
						Starttime = storage.nSchedule2[idx].StartTime.nHour;
						Starttime <<= 8;
						Starttime += storage.nSchedule2[idx].StartTime.nMinute;
						Starttime <<= 8;
						Starttime += storage.nSchedule2[idx].StartTime.nSec;

						Endtime = storage.nSchedule2[idx].EndTime.nHour;
						Endtime <<= 8;
						Endtime += storage.nSchedule2[idx].EndTime.nMinute;
						Endtime <<= 8;
						Endtime += storage.nSchedule2[idx].EndTime.nSec;

						Currtime = ((uint8_t)hour() << 16);
						Currtime += (minute() << 8);
						Currtime += second();

						if (Currtime >= Starttime && Currtime <= Endtime)
							bresult = true;
					}
				}
			}

			relayState = bresult;
			if (storage.P2 == OVR)
			{
				if (storage.P1 == POS && digitalRead(InputPIN) == HIGH)
					relayState = false;
				else if (storage.P1 == NEG && digitalRead(InputPIN) == LOW)
					relayState = false;
			}
		}
		break;
		case 5: // Schedule5 Mode
		{
			bool bresult = false;
			for (uint8_t idx = 0; idx < 6; idx++)
			{
				if (bitRead(storage.nSchedule3[idx].nMonthSetting, month() - 1))
				{
					if (bitRead(storage.nSchedule3[idx].nDaySetting, weekday(now()) - 1))
					{
						uint32_t Starttime = 0;
						uint32_t Endtime = 0;
						uint32_t Currtime = 0;
						Starttime = storage.nSchedule3[idx].StartTime.nHour;
						Starttime <<= 8;
						Starttime += storage.nSchedule3[idx].StartTime.nMinute;
						Starttime <<= 8;
						Starttime += storage.nSchedule3[idx].StartTime.nSec;

						Endtime = storage.nSchedule3[idx].EndTime.nHour;
						Endtime <<= 8;
						Endtime += storage.nSchedule3[idx].EndTime.nMinute;
						Endtime <<= 8;
						Endtime += storage.nSchedule3[idx].EndTime.nSec;

						Currtime = ((uint8_t)hour() << 16);
						Currtime += (minute() << 8);
						Currtime += second();
						if (Currtime >= Starttime && Currtime <= Endtime)
							bresult = true;
					}
				}
			}

			relayState = bresult;
			if (storage.P2 == OVR)
			{
				if (storage.P1 == POS && digitalRead(InputPIN) == HIGH)
					relayState = false;
				else if (storage.P1 == NEG && digitalRead(InputPIN) == LOW)
					relayState = false;
			}
		}
		break;
		}

		// Output Process
		digitalWrite(RelayPIN, relayState);

		// 
		if (PreRelstatus != relayState)
		{
			String message = DeviceName;
			if (relayState)
				ResponsetoCommand(message + " Opened!\r\n");
			else
				ResponsetoCommand(message + " Closed!\r\n");
			PreRelstatus = relayState;
		}


		if (PreInputStatus != digitalRead(InputPIN))
		{
			String message = DeviceName + ",change,IO=";
			PreInputStatus = digitalRead(InputPIN);
			/*
			if (PreInputStatus)
			ResponsetoCommand(message + String(" Current Status-HIGH.\r\n");
			else
			ResponsetoCommand(message + " Current Status-LOW.\r\n");
			*/
			message += String(PreInputStatus);
			message += "\r\n";
			ResponsetoCommand(message);
		}
	}
	// Mqtt Reveive part
	/*
	if (!client.connected() && WiFi.status() == WL_CONNECTED)
	{
	reconnect();
	}*/
	if (client.connected())
		client.loop();

}

void ResponsetoCommand(String message)
{

	for (uint8_t i = 0; i < MAX_SRV_CLIENTS; i++) {
		if (TCPClients[i] && TCPClients[i].connected())
			TCPClients[i].println(message);
	}
	message.toCharArray(mqtt_msg, message.length());
	if (client.connected())
		client.publish(mqtt_Out, mqtt_msg);
	if (TCPclient.connected())
		TCPclient.println(message);
}

String RefreshPage(String msg)
{
	String HtmlContent = "<html lang = 'en'>";
	HtmlContent += "<head><meta charset = 'utf-8'>";
	HtmlContent += "<meta name = 'viewport' content = 'width=device-width, initial-scale=1'>";
	HtmlContent += "<title>ESP8266 WIFI Relay Control Page</title>";
	HtmlContent += "<link rel = 'stylesheet' href='http://netdna.bootstrapcdn.com/bootstrap/3.1.1/css/bootstrap.min.css'>";
	HtmlContent += "<style>body{ padding-top:20px;padding-bottom:20px }.header{ border-bottom:1px solid #e5e5e5;margin-bottom:0 }";
	HtmlContent += ".jumbotron{ text-align:center }.marketing{ margin:40px 0 }";
	HtmlContent += ".arduino h4{ font-size:25px;color:#2ecc71;margin-top:28px;padding-right:10px;padding-left:0; display:inline-block; }";
	HtmlContent += ".arduino h5{ font-size:28px;color:#2ecc71;margin-top:28px;padding-right:0;padding-left:0px; display:inline-block; }";
	HtmlContent += ".arduino h6{ font-size:18px;color:#ff0000;margin-top:28px;padding-right:0;padding-left:0px; display:inline-block; }";
	HtmlContent += ".clear{ clear:both; }";
	HtmlContent += ".align-center{ text-align:center; }";
	HtmlContent += "</style></head><body><div class = 'container align-center'>";
	HtmlContent += "<div class = 'header'><h3 class = 'text-muted'>ESP8266 WIFI Relay Control</h3>";
	HtmlContent += "</diainv><div class = 'row arduino'><div class = 'col-lg-12'><div class = 'clear'></div><h4>Relay status : </h4>";

	// Relay Status Present
	if (relayState)
		HtmlContent += "<h5>On</h5>";
	else
		HtmlContent += "<h5>Off</h5>";

	HtmlContent += "<div class = 'clear'></div>";
	HtmlContent += "<h4>Relay Control</h4>";
	HtmlContent += "<div class = 'clear'></div>";
	HtmlContent += "<h4></h4><button onclick = \"location.href='/OpenRelay'\">Open Relay</button>";

	HtmlContent += "<div class = 'clear'></div><h4></h4>";
	HtmlContent += "<button onclick = \"location.href='/CloseRelay'\">Close Relay</button>";

	HtmlContent += "<div class = 'clear'></div>";
	HtmlContent += "<h4>Input Status: </h4>";
	bool bIOStatus = digitalRead(InputPIN);
	if (bIOStatus)
		HtmlContent += "<h5>On</h5>";
	else
		HtmlContent += "<h5>Off</h5>";
	HtmlContent += "<div class = 'clear'></div><h4></h4>";
	HtmlContent += "<button onclick = \"location.href='/'\">Refresh</button>";
	HtmlContent += "<div class = 'clear'></div>";
	HtmlContent += "<h4> You can look <a href = '/DevSettings'>Device Info.</a></h4>";
	HtmlContent += "<div class = 'clear'></div>";
	HtmlContent += "<h6>";
	HtmlContent += msg;
	HtmlContent += "</h6>";

	HtmlContent += "<div class = 'clear'></div>";
	HtmlContent += "<h4><a href = '/Wifi'>Wifi Setting</a></h4>";
	HtmlContent += "</div></div></div></body></html>";

	return HtmlContent;
}


void SetupPortProc()
{
	String message = "";
	if (server->hasArg("a") && server->hasArg("b"))
	{
		uint16_t nHttpPort = server->arg("a").toInt();
		uint16_t nTcpPort = server->arg("b").toInt();
		storage.nHttpPort = nHttpPort;
		storage.nTcpPort = nTcpPort;
		saveConfig();
		message += "http=";
		message += String(nHttpPort);
		message += "tcp=";
		message += String(nTcpPort);
		server->send(200, "text/plain", message);
		ESP.restart();
	}
	else
		server->send(200, "text/plain", "Invalid Arguments!");
}

// 
void SoftAPProc()
{
	String message = "";
	if (server->hasArg("PASS"))
	{
		AP_pass = server->arg("PASS");
		saveConfig();
		message += "New Soft AP PASSWORD is ";
		message += AP_pass;
		message += ", Setting is changed, Device will restart!\n";
		server->send(200, "text/plain", message);       //Response to the HTTP request
		ESP.restart();
	}
	else
	{
		message += "Invalid Command Format!\n";
		server->send(200, "text/plain", message);       //Response to the HTTP request
	}
}

// 
void WifiAPProc()
{
	String message = "";
	if (server->hasArg("SSID") && server->hasArg("PASS"))
	{
		STA_ssid = server->arg("SSID");
		STA_pass = server->arg("PASS");
		saveConfig();
		message += "New SSID for Connection is ";
		message += STA_ssid;
		message += ", New PASSWORD for Connection is ";
		message += STA_pass;
		message += "\nSetting is changed, Device will restart!\n";
		server->send(200, "text/plain", message);       //Response to the HTTP request
		ESP.restart();

		//WiFi.disconnect();
		//WiFi.mode(WIFI_OFF);
		//delay(1000);

		//WiFi.mode(WIFI_AP_STA);
		/*
		uint8_t idx = 0;
		for (idx = 0; idx < STA_ssid.length(); idx++)
		ssid[idx] = STA_ssid.charAt(idx);
		ssid[idx] = '\0';

		for (idx = 0; idx < STA_pass.length(); idx++)
		pswd[idx] = STA_pass.charAt(idx);
		pswd[idx] = '\0';

		WiFi.begin(ssid, pswd);
		Serial.println("\nConnecting to: " + String(ssid));

		Serial.println("Password is " + String(pswd));
		uint32_t ntick = millis();
		bool ledStatus = false;
		while (WiFi.status() != WL_CONNECTED) {
		// Blink the LED.
		digitalWrite(LED_PIN, ledStatus); // Write LED high/low.
		ledStatus = (ledStatus == HIGH) ? LOW : HIGH;
		delay(100);
		if ((ntick + 15000) < millis()) // 10sec
		{
		WiFi.disconnect();
		break;
		}
		}
		digitalWrite(LED_PIN, LOW); // Write LED high/low.
		server->send(200, "text/html", WifiSettingPage());       //Response to the HTTP request*/

	}
	else
	{
		message += "Invalid Command Format!\n";
		server->send(200, "text/plain", message);       //Response to the HTTP request
	}
}

void DefaultSoftAP()
{
	String macID = "";
	for (uint8_t idx = 0; idx < WL_MAC_ADDR_LENGTH; idx++)
	{
		if (mac[idx] < 16)
			macID += "0";
		macID += String(mac[idx], HEX);
	}

	macID.toUpperCase();
	AP_ssid = "Litmath_" + macID;
	AP_pass = "Litmath123";
	saveConfig();
	String msg = "New SOFT AP SSID is ";
	msg += AP_ssid;
	msg += ", New PASSWORD is ";
	msg += AP_pass;
	msg += "\nSetting is changed, Device will restart!\n";
	server->send(200, "text/plain", msg);       //Response to the HTTP request

	ESP.restart();
}

String MainPageNoneAuth(String msg)
{
	String HttpContent = "<html lang='en'>\
    <head>\
    <meta charset='utf-8'>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <title>ESP8266 WIFI Relay Control Page</title>\
    <link rel='stylesheet' href='http://netdna.bootstrapcdn.com/bootstrap/3.1.1/css/bootstrap.min.css'>\
    <style>body{padding-top:25px;padding-bottom:20px}.header{border-bottom:1px solid #e5e5e5;margin-bottom:0;color:#D4AC0D}\
      .jumbotron{text-align:center}.marketing{margin:40px 0}\
      .arduino h4{font-size:22px;color:#2ecc71;margin-top:20px;padding-right:10px;padding-left:0; display:inline-block;}\
      .arduino h5{font-size:16px;color:#E74C3C;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .arduino h6{font-size:16px;color:#2ecc71;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .clear{ clear:both;}\
      .align-center {text-align:center;}\
    </style>\
    </head>\
    <body style='background-color:#E8F8F5'>\
    <div class='container align-center'>\
      <div class='header'>\
      <h1>Welcome to Litmath, WiFi Relay</h1>\
      </div>\
      <form action='/login' method='POST'>\
        <div class='container'>\
          <div class='row arduino'>\
            <div class='col-lg-12'>\
                        <div class='clear'></div>\
                        <h4>Enter Password:</h4>\
                        <h4><input type='password' name='PASS'></h4>\
                        <h4><input type='submit' value='Login'></h4>\
                        <div class='clear'></div>\
                        <h4>Password Hint ";
	HttpContent += Passhint;
	HttpContent += ".</h4>\
                        <div class='clear'></div>\
                        <h5>";
	HttpContent += msg;
	HttpContent += "</h5>\
                        <h6>Forgotten password? No problem. Hardware reset to default passwords is available. <br /> Hold IO port active during power-up and watch the blue light at the board blinking during power up. Keep holding the IO active for 4 seconds after the blue light stops blinking and release. The most simple way to activate IO is to connect 9V battery to the IO terminals (observe polarity). Use longer wires to avoid interference caused by hands touching the board. You can use also the power source at the board but keep in mind that grounds are separated for the board and IO terminals. <br /> Default password is 'WifiRelay123456' for the board and 'Litmath123' for the AdHoc WiFi network.</h6>\
                        <div class='clear'></div>\
            </div>\
          </div>\
        </div>\
      </form>\
      <div class='row arduino'>\
      <div class='col-lg-12'>\
        <div class='clear'></div>\
        <h4><br /><br /></h4>\
        <h6> <a href='http://www.litmath.com'>Visit our web site Litmath, LLC</a></h6>\
        &nbsp&nbsp&nbsp&nbsp\
        <h6> Copyright Litmath, LLC, 2017 </h6>\
      </div>\
      </div>\
    </div>\
    </body>\
  </html>";

	return HttpContent;
}

String MainPage()
{
	String HttpContent = "<html lang='en'>\
    <head>\
    <meta charset='utf-8'>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <title>ESP8266 WIFI Relay Control Page</title>\
    <link rel='stylesheet' href='http://netdna.bootstrapcdn.com/bootstrap/3.1.1/css/bootstrap.min.css'>\
    <style>body{padding-top:25px;padding-bottom:20px}.header{border-bottom:1px solid #e5e5e5;margin-bottom:0;color:#D4AC0D}\
      .jumbotron{text-align:center}.marketing{margin:40px 0}\
      .arduino h4{font-size:22px;color:#2ecc71;margin-top:20px;padding-right:10px;padding-left:0; display:inline-block;}\
      .arduino h5{font-size:16px;color:#E74C3C;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .arduino h6{font-size:16px;color:#2ecc71;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .clear{ clear:both;}\
      .align-center {text-align:center;}\
    </style>\
    </head>\
    <body style='background-color:#E8F8F5'>\
    <div class='container align-center'>\
      <div class='header'>\
      <h1>Welcome to Litmath, WiFi Relay</h1>\
      </div>\
      <div class='row arduino'>\
      <div class='col-lg-12'>\
        <div class='clear'></div>\
        <h5>You have to <a href='/login?DISCONNECT=YES'>logoff</a> when you exit from this page for security.</h5>\
        <div class='clear'></div>\
        <h4>Device Name:";
	HttpContent += DeviceName;
	HttpContent += "</h4>\
        <div class='clear'></div>\
        <h4>Relay status:</h4>";
	if (relayState)
		HttpContent += "<h4>On!</h4>";
	else
		HttpContent += "<h4>Off!</h4>";
	HttpContent += "<div class='clear'></div>\
            <h4>I/O status:</h4>";
	if (digitalRead(InputPIN))
		HttpContent += "<h4>On!</h4>";
	else
		HttpContent += "<h4>Off!</h4>";

	HttpContent += "<div class='clear'></div>\
        <h6>Local IP Address is ";
	HttpContent += WiFi.localIP().toString();
	HttpContent += ".</h6>\
        <div class='clear'></div>";
	HttpContent += "<h6>MAC Address is ";
	String macID = "";
	for (uint8_t idx = 0; idx < (WL_MAC_ADDR_LENGTH - 1); idx++)
	{
		if (mac[idx] < 16)
			macID += "0";
		macID += String(mac[idx], HEX);
		macID += ":";
	}
	if (mac[5] < 16)
		macID += "0";
	macID += String(mac[5], HEX);
	macID.toUpperCase();
	HttpContent += macID;
	HttpContent += ".</h6>\
        <div class='clear'></div>\
        <h4></h4>\
            <div class='clear'></div>\
            <h4>Relay Operation</h4>\
            <div class='clear'></div>\
            <button style='width:100px' onclick=\"location.href='/OpenRelay?t=0'\">Open Relay</button>\
            &nbsp;&nbsp;&nbsp;&nbsp;\
            <button style='width:100px' onclick=\"location.href='/CloseRelay'\">Close Relay</button>\
            <div class='clear'></div>\
            <h4>Settings</h4>\
            <div class='clear'></div>\
            <button style='width:100px' onclick=\"location.href ='/Settings'\">Relay Setup</button>\
            &nbsp;&nbsp;&nbsp;&nbsp;\
            <button style='width:100px' onclick=\"location.href ='/WifiSetting'\">Wifi Setup</button>\
        <br/>\
        <h6> <a href='http://www.litmath.com'>Visit our web site Litmath, LLC</a></h6>\
        &nbsp&nbsp&nbsp&nbsp\
        <h6> Copyright Litmath, LLC, 2017 </h6>\
      </div>\
      </div>\
    </div>\
    </body>\
  </html>";
	return HttpContent;
}

void WifiSetting()
{
	if (!is_authentified()) {
		String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
		server->sendContent(header);
		return;
	}
	server->send(200, "text/html", WifiSettingPage());
}

String WifiSettingPage()
{
	int nCnt = WiFi.scanNetworks();

	String HttpContent = "<html lang='en'>\
    <head>\
    <meta charset='utf-8'>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <title>ESP8266 WIFI Relay Control Page</title>\
    <link rel='stylesheet' href='http://netdna.bootstrapcdn.com/bootstrap/3.1.1/css/bootstrap.min.css'>\
    <style>body{padding-top:25px;padding-bottom:20px}.header{border-bottom:1px solid #e5e5e5;margin-bottom:0;color:#D4AC0D}\
      .jumbotron{text-align:center}.marketing{margin:40px 0}\
      .arduino h4{font-size:22px;color:#2ecc71;margin-top:20px;padding-right:10px;padding-left:0; display:inline-block;}\
      .arduino h5{font-size:16px;color:#E74C3C;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .arduino h6{font-size:16px;color:#2ecc71;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .clear{ clear:both;}\
      .align-center {text-align:center;}\
    </style>\
    </head>\
    <body style='background-color:#E8F8F5'>\
    <div class='container align-center'>\
      <div class='header'>\
      <h1>Welcome to Litmath, WiFi Relay</h1>\
      </div>\
      <form action='/Wifi' method='POST'>\
        <div class='container'>\
          <div class='row arduino'>\
            <div class='col-lg-12'>\
              <div class='clear'></div>\
              <h4>Select WiFi Network to connect to</h4>\
              <div class='clear'></div>\
              <h6>Usable APs:\
              <select name='SSID'>";
	if (nCnt == 0)
		HttpContent += "<option value='None'>None</option>";
	else
	{
		for (uint8_t idx = 0; idx < nCnt; idx++)
		{
			HttpContent += "<option value='";
			HttpContent += WiFi.SSID(idx);
			HttpContent += "'>";
			HttpContent += WiFi.SSID(idx);
			HttpContent += "</option>";
		}
		HttpContent += "</select>";

	}
	HttpContent += "</h6>&nbsp&nbsp&nbsp&nbsp\
              <h6>Password:<input type='password' name='PASS' width='30'></h6>\
              <h6><input type='submit' value='Connect'></h6>\
            </div>\
          </div>\
        </div>\
      </form>\
      <form action='/SoftAP' method='POST'>\
        <div class='container'>\
          <div class='row arduino'>\
            <div class='col-lg-12'>\
              <div class='clear'></div>\
              <h4>AdHoc network</h4>\
              <div class='clear'></div>\
              <h6>AP SSID:</h6>&nbsp\
              <h5>";
	HttpContent += AP_ssid;
	HttpContent += "</h5>&nbsp&nbsp\
              <h6 style='width:100px;'>Password:</h6>\
              <h5><input type='text' name='PASS' value='";
	HttpContent += AP_pass;
	HttpContent += "'></h5>\
              <h6><input type='submit' value='Save'></h6>\
            </div>\
          </div>\
        </div>\
      </form>\
      <div class='row arduino'>\
      <div class='col-lg-12'>\
        <div class='clear'></div>\
        <h6> You can go <a href='/'>back.</a></h6>\
        <div class='clear'></div>\
        <h6> <a href='http://www.litmath.com'>Visit our web site Litmath, LLC</a></h6>\
        &nbsp&nbsp&nbsp&nbsp\
        <h6> Copyright Litmath, LLC, 2017 </h6>\
      </div>\
      </div>\
    </div>\
    </body>\
  </html>\
    ";
	return HttpContent;
}

void SettingProc()
{
	if (!is_authentified()) {
		String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
		server->sendContent(header);
		return;
	}
	Serial.println("Setting Proc");
	SettingPage();
}

void SettingPage()
{
	String HttpContent = F("<html lang='en'><head>\
    <meta charset='utf-8'>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <title>ESP8266 WIFI Relay Control Page</title>\
    <link rel='stylesheet' href='http://netdna.bootstrapcdn.com/bootstrap/3.1.1/css/bootstrap.min.css'>\
    <style>body{padding-top:25px;padding-bottom:20px}.header{border-bottom:1px solid #e5e5e5;margin-bottom:0;color:#D4AC0D}\
      .jumbotron{text-align:center}.marketing{margin:40px 0}\
      .arduino h4{font-size:22px;color:#2ecc71;margin-top:20px;padding-right:10px;padding-left:0; display:inline-block;}\
      .arduino h5{font-size:16px;color:#E74C3C;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .arduino h6{font-size:16px;color:#2ecc71;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .clear{ clear:both;}\
      .align-center {text-align:center;}\
    </style>\
    </head>\
    <body style='background-color:#E8F8F5'>\
    <div class='container align-center'>\
      <div class='header'>\
      <h1>Welcome to Litmath, WiFi Relay</h1>\
      </div>\
        <form action='/RelayName' method='POST'>\
            <div class='container'>\
                <div class='row arduino'>\
                    <div class='col-lg-12'>\
                        <div class='clear'></div>\
                        <h6></h6>\
                        <div class='clear'></div>\
                        <h6>Current Relay Name is ");
	Serial.println("Step1");
	HttpContent += DeviceName;
	HttpContent += F(".</h6>\
                        <div class='clear'></div>\
                        <h6>Change the name?(option)</h6>\
                        <h5><input type='text' name='NewName'></h5>\
                        <h6><input type='submit' value='Save'></h6>\
                    </div>\
                </div>\
            </div>\
        </form>\
      <form action='/Pass' method='POST'>\
        <div class='container'>\
          <div class='row arduino'>\
            <div class='col-lg-12'>\
                        <div class='header'></div>\
              <div class='clear'></div>\
              <h4>Password Setting</h4>\
              <div class='clear'></div>\
              <h6 style='width:140px;'>Current Password</h6>\
              <h5><input type='password' name='ppo'></h5>\
              <div class='clear'></div>\
              <h6 style='width:140px;'>New Password</h6>\
              <h5><input type='password' name='pp'></h5>\
              <div class='clear'></div>\
              <h6 style='width:140px;'>Confirm Password</h6>\
              <h5><input type='password' name='ppc'></h5>\
              <div class='clear'></div>\
              <h6 style='width:140px;'>Password Hint</h6>\
              <h5><input type='text' name='pph' value='");
	HttpContent += Passhint;
	HttpContent += F("'></h5>\
              <div class='clear'></div>\
              <h6><input type='submit' value='Save Password'></h6>\
            </div>\
          </div>\
        </div>\
      </form>\
      <form action='/Port' method='POST'>\
        <div class='container'>\
          <div class='row arduino'>\
            <div class='col-lg-12'>\
                        <div class='header'></div>\
            <div class='clear'></div>\
              <h4>Port Setting</h4>\
              <div class='clear'></div>\
              <h6>Http Port</h6>\
              <h5><input type='text' name='http' value='");
	HttpContent += String(storage.nHttpPort);
	HttpContent += F("'></h5>\
              <h6>Tcp Port</h6>\
              <h5><input type='text' name='tcp' value='");
	HttpContent += String(storage.nTcpPort);
	HttpContent += F("'></h5><h6><input type='submit' value='Save'></h6>\
            </div>\
          </div>\
        </div>\
      </form>\
        <form action='/Client' method='POST'>\
            <div class='container'>\
                <div class='row arduino'>\
                    <div class='col-lg-12'>\
                        <div class='header'>\
                        </div>\
                        <div class='clear'></div>\
                        <h6>TCP Connection Setting</h6>\
                        <div class='clear'></div>\
                        <h6>Server IP</h6>\
                        <h5><input type='text' name='IP' value='");
	HttpContent += TCPServerIP;
	HttpContent += F("'></h5>\
                        <h6>Server Port</h6>\
                        <h5><input type='text' name='Port' value='");
	HttpContent += String(storage.nServerPort);
	HttpContent += F("'></h5>\
                        <h6><input type='submit' value='Save'></h6>\
                    </div>\
                </div>\
            </div>\
        </form>\
    <form action='/MODE' method='POST'>\
        <div class='container'>\
          <div class='row arduino'>\
            <div class='col-lg-12'>\
                        <div class='header'>\
                        </div>\
              <div class='clear'></div>\
                <h6>Mode of operation</h6>\
                <h5>\
                <select name = 'Choice'>");
	Serial.println("Step2");

	for (uint8_t idx = 0; idx < 5; idx++)
	{
		HttpContent += F("<option value='");
		HttpContent += String(idx + 1);
		HttpContent += "'";
		if ((idx + 1) == storage.nOperMode)
			HttpContent += F(" selected = 'selected'");
		HttpContent += F(">");
		switch (idx + 1)
		{
		case 1:
			HttpContent += F("Normal Mode");
			break;
		case 2:
			HttpContent += F("IO Control Mode");
			break;
		case 3:
			HttpContent += F("Schedule1 Mode");
			break;
		case 4:
			HttpContent += F("Schedule2 Mode");
			break;
		case 5:
			HttpContent += F("Schedule3 Mode");
			break;
		}
		HttpContent += F("</option>");
	}
	HttpContent += F("</select>\
              </h5>\
                        <h6>Parameter1</h6>\
                        <h5>\
                            <select name='P1'>");
	if (storage.P1 > 0)
	{
		HttpContent += F("<option value='0'>Pos</option>");
		HttpContent += F("<option value='1' selected = 'selected'>Neg</option>");
	}
	else
	{
		HttpContent += F("<option value='0' selected = 'selected'>Pos</option>");
		HttpContent += F("<option value='1'>Neg</option>");
	}
	Serial.println("Step3");

	HttpContent += F("</select>\
                        </h5>\
                        <h6>Parameter2</h6>\
                        <h5>\
                            <select name='P2'>");
	switch (storage.P2)
	{
	case NONE:
	{
		HttpContent += F("<option value = '0' selected = 'selected'>None</option>\
        <option value = '1'>OVR</option>");
	}
	break;
	case OVR:
	{
		HttpContent += F("<option value = '0'>None</option>\
        <option value = '1' selected = 'selected'>OVR</option>");
	}
	break;
	}
	HttpContent += F("</select>\
                        </h5>\
              <h6><input type='submit' value='OK'></h6>\
            </div>\
          </div>\
        </div>\
      </form>\
            <form action='/Mqtt' method='POST'>\
                <div class='container'>\
                    <div class='row arduino'>\
                        <div class='col-lg-12'>\
                            <div class='header'>\
                            </div>\
                            <div class='header'></div>\
                            <div class='clear'></div>\
                            <h6>Mqtt Setting</h6>\
                            <div class='clear'></div>\
                            <h6>Mqtt Server</h6>&nbsp\
                            <h5><input type='text' name='Server' value='");
	HttpContent += Mqtt_IP;
	HttpContent += F("'></h5>\
                            &nbsp&nbsp&nbsp&nbsp\
                            <h6>Port</h6>&nbsp\
                            <h5><input type='text' name='Port' value='");
	HttpContent += String(storage.nMqttPort);
	HttpContent += F("'></h5>\
                            &nbsp&nbsp&nbsp&nbsp\
                            <h6>Input</h6>&nbsp\
                            <h5><input type='text' name='Input' value='");
	HttpContent += Mqtt_Input;
	HttpContent += F("'></h5>\
                            <h6>Output</h6>&nbsp\
                            <h5><input type='text' name='Output' value='");
	HttpContent += Mqtt_Output;
	HttpContent += F("'></h5>\
                            <h5><input type='submit' value='Save'></h5>\
                        </div>\
                    </div>\
                </div>\
            </form>\
      <form action='/time' method='POST'>\
        <div class='container'>\
          <div class='row arduino'>\
            <div class='col-lg-12'>\
                            <div class='header'></div>\
                            <h6>Location and Datetime Setting</h6>\
              <div class='clear'></div>\
              <h6>Current DateTime</h6>\
              <h6>\
              <input type='datetime-local' name='currtime' value='");
	HttpContent += CurrenttimeToStr();
	HttpContent += "' step='1'>\
              </h6>\
              <h5>\
                <select name='Location'>";
	for (uint8_t idx = 0; idx < 6; idx++)
	{
		HttpContent += "<option value='";
		HttpContent += String(-5 - idx);
		HttpContent += "'";
		if (idx == (abs(storage.nTimeZone) - 5))
			HttpContent += " selected = 'selected'";
		HttpContent += ">";
		switch (idx)
		{
		case 0:
			HttpContent += "Eastern";
			break;
		case 1:
			HttpContent += "Central";
			break;
		case 2:
			HttpContent += "Mountain";
			break;
		case 3:
			HttpContent += "Pacific";
			break;
		case 4:
			HttpContent += "Alaska";
			break;
		case 5:
			HttpContent += "Hawaii";
			break;
		}
		HttpContent += "</option>";
	}
	Serial.println("Step3");
	HttpContent += F("</select>\
              </h5>\
              <h6><input type='submit' value='Set'></h6>\
            </div>\
          </div>\
        </div>\
      </form>\
      <div class='row arduino'>\
      <div class='col-lg-12'>\
        <div class='clear'></div>\
        <h6> You can go <a href='/'>back.</a></h6>\
        <div class='clear'></div>\
        <h6> <a href='http://www.litmath.com'>Visit our web site Litmath, LLC</a></h6>\
        &nbsp&nbsp&nbsp&nbsp\
        <h6> Copyright Litmath, LLC, 2017 </h6>\
      </div>\
      </div>\
    </div>\
    </body>\
  </html>\
    ");
	server->send(200, "text/html", HttpContent);
	//return HttpContent;
}

String timetoString(tmElements_t t)
{
	String DateTime = "";
	DateTime += String(t.Year + 2000);
	DateTime += "-";
	if (t.Month < 10)
		DateTime += "0";
	DateTime += String(t.Month);
	DateTime += "-";
	if (t.Day < 10)
		DateTime += "0";
	DateTime += String(t.Day);
	DateTime += "T";
	if (t.Hour < 10)
		DateTime += "0";
	DateTime += String(t.Hour);
	DateTime += ":";
	if (t.Minute < 10)
		DateTime += "0";
	DateTime += String(t.Minute);
	DateTime += ":";
	if (t.Second < 10)
		DateTime += "0";
	DateTime += String(t.Second);
	Serial.println(DateTime);
	return DateTime;
}

String CurrenttimeToStr()
{
	String DateTime = "";
	DateTime += String(year());
	DateTime += "-";
	if (month() < 10)
		DateTime += "0";
	DateTime += String(month());
	DateTime += "-";
	if (day() < 10)
		DateTime += "0";
	DateTime += String(day());
	DateTime += "T";
	if (hour() < 10)
		DateTime += "0";
	DateTime += String(hour());
	DateTime += ":";
	if (minute() < 10)
		DateTime += "0";
	DateTime += String(minute());
	DateTime += ":";
	if (second() < 10)
		DateTime += "0";
	DateTime += String(second());
	return DateTime;
}

void timeFromString(tmElements_t &t, String str)
{ // 2000-01-01T00%3A00%3A00
	Serial.println(str);
	t.Year = str.substring(0, 4).toInt() - 2000;
	t.Month = str.substring(5, 7).toInt();
	t.Day = str.substring(8, 10).toInt();
	t.Hour = str.substring(11, 13).toInt();
	t.Minute = str.substring(14, 16).toInt();
	t.Second = str.substring(17, 19).toInt();
}

void RelayNameProc()
{
	if (server->hasArg("NewName"))
	{
		DeviceName = server->arg("NewName");
		saveConfig();
	}
	SettingPage();
	//server->send(200, "text/html", SettingPage());
}

void PasswordProc()
{
	if ((server->hasArg("ppo") && server->hasArg("ppc")) && server->hasArg("pp"))
	{
		if ((server->arg("ppo") == DevPass) && (server->arg("ppc") == server->arg("pp")))
		{
			Passhint = server->arg("pph");
			DevPass = server->arg("pp");
			saveConfig();
			String header = "HTTP/1.1 301 OK\r\nSet-Cookie: ESPSESSIONID=0\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
			server->sendContent(header);
			return;
		}
	}
	SettingPage();
	//server->send(200, "text/html", SettingPage());
}

void PortProc()
{
	String message = "";
	if (is_authentified())
	{
		if (server->hasArg("http") && server->hasArg("tcp"))
		{
			storage.nHttpPort = server->arg("http").toInt();
			storage.nTcpPort = server->arg("tcp").toInt();
			saveConfig();
			message += "http=";
			message += String(storage.nHttpPort);
			message += ",tcp=";
			message += String(storage.nTcpPort);
			server->send(200, "text/html", UtilityPage(message));
			ESP.restart();
		}
		else
		{
			server->send(200, "text/html", UtilityPage("Invalid Command Format"));
		}
	}
	else
	{

		if ((server->hasArg("http") && server->hasArg("tcp")) && server->arg("pp"))
		{
			if (server->arg("pp") == DevPass)
			{
				storage.nHttpPort = server->arg("http").toInt();
				storage.nTcpPort = server->arg("tcp").toInt();
				saveConfig();
				message += "New Port Setting \r\nhttp=";
				message += String(storage.nHttpPort);
				message += ",tcp=";
				message += String(storage.nTcpPort);
				server->send(200, "text/plain", message);
				ESP.restart();
				return;
			}
		}
		else
			message += "Invalid Command Format!";
		server->send(200, "text/plain", message);
		return;
	}
	SettingPage();
	//server->send(200, "text/html", SettingPage());
}

void ModeTemp()
{
	if (is_authentified())
	{
		if (server->hasArg("Choice"))
		{
			storage.nOperMode = server->arg("Choice").toInt();
			storage.P1 = server->arg("P1").toInt();
			storage.P2 = server->arg("P2").toInt();
			saveConfig();
		}
	}
	SettingPage();
	//server->send(200, "text/html", SettingPage());
}

void ModeProc()
{
	String message;
	if (server->arg("pp") == DevPass)
	{
		storage.nOperMode = server->arg("Choice").toInt();
		storage.P1 = server->arg("P1").toInt();
		storage.P2 = server->arg("P2").toInt();
		saveConfig();
		message += "Operation Mode is changed, Current Mode is ";
		switch (storage.nOperMode)
		{
		case 1:
		{
			message += "Normal Mode. ";
			if (storage.P2 == OVR)
			{
				message += "Overlay, ";
				if (storage.P1 == POS)
					message += "Positive.\r\n";
				else
					message += "Negative.\r\n";
			}
		}
		break;

		case 2:
		{
			message += "IO Control Mode. ";
			if (storage.P1 == POS)
				message += "Positive!\r\n";
			else
				message += "Negative!\r\n";
		}
		break;

		case 3:
		{
			message += "Schedule1 Control Mode.\r\n";
			if (storage.P2 == OVR)
			{
				message += "Overlay, ";
				if (storage.P1 == POS)
					message += "Positive.\r\n";
				else
					message += "Negative.\r\n";
			}
			for (uint8_t idx = 0; idx < 6; idx++)
			{
				String strtemp = "Schedule1, ";
				strtemp += "Unit";
				strtemp += String(idx);
				strtemp += ":StartTime-";
				strtemp += TimeVartoString(storage.nSchedule1[idx].StartTime);
				strtemp += ",EndTime-";
				strtemp += TimeVartoString(storage.nSchedule1[idx].EndTime);
				strtemp += ",Day:";
				for (uint8_t index = 0; index < 7; index++)
					strtemp += String(bitRead(storage.nSchedule1[idx].nDaySetting, 6 - index));
				strtemp += ",Month:";
				for (uint8_t index = 0; index < 12; index++)
					strtemp += String(bitRead(storage.nSchedule1[idx].nMonthSetting, 11 - index));
				strtemp += "\r\n";

				strtemp.toCharArray(mqtt_msg, strtemp.length());
				message += strtemp;
			}

		}
		break;

		case 4:
		{
			message += "Schedule2 Control Mode.\r\n";
			if (storage.P2 == OVR)
			{
				message += "Overlay, ";
				if (storage.P1 == POS)
					message += "Positive\r\n";
				else
					message += "Negative\r\n";
			}
			for (uint8_t idx = 0; idx < 6; idx++)
			{
				String strtemp = "Schedule2, ";
				strtemp += "Unit";
				strtemp += String(idx);
				strtemp += ":StartTime-";
				strtemp += TimeVartoString(storage.nSchedule2[idx].StartTime);
				strtemp += ",EndTime-";
				strtemp += TimeVartoString(storage.nSchedule2[idx].EndTime);
				strtemp += ",Day:";
				for (uint8_t index = 0; index < 7; index++)
					strtemp += String(bitRead(storage.nSchedule2[idx].nDaySetting, 6 - index));
				strtemp += ",Month:";
				for (uint8_t index = 0; index < 12; index++)
					strtemp += String(bitRead(storage.nSchedule2[idx].nMonthSetting, 11 - index));
				strtemp += "\r\n";

				strtemp.toCharArray(mqtt_msg, strtemp.length());
				message += strtemp;
			}
		}
		break;

		case 5:
		{
			message += "Schedule3 Control Mode.\r\n";
			if (storage.P2 == OVR)
			{
				message += "Overlay, ";
				if (storage.P1 == POS)
					message += "Setting.\r\n";
				else
					message += "Setting.\r\n";
			}
			for (uint8_t idx = 0; idx < 6; idx++)
			{
				String strtemp = "Schedule3, ";
				strtemp += "Unit";
				strtemp += String(idx);
				strtemp += ":StartTime-";
				strtemp += TimeVartoString(storage.nSchedule3[idx].StartTime);
				strtemp += ",EndTime-";
				strtemp += TimeVartoString(storage.nSchedule3[idx].EndTime);
				strtemp += ",Day:";
				for (uint8_t index = 0; index < 7; index++)
					strtemp += String(bitRead(storage.nSchedule3[idx].nDaySetting, 6 - index));
				strtemp += ",Month:";
				for (uint8_t index = 0; index < 12; index++)
					strtemp += String(bitRead(storage.nSchedule3[idx].nMonthSetting, 11 - index));
				strtemp += "\r\n";

				strtemp.toCharArray(mqtt_msg, strtemp.length());
				message += strtemp;
			}
		}
		break;
		}

	}
	else
		message += "Invalid Password.\r\n";
	server->send(200, "text/plain", message);
}

void MqttProc()
{
	if (is_authentified())
	{
		if (server->hasArg("Server"))
		{
			Mqtt_IP = server->arg("Server");
			Mqtt_Input = server->arg("Input");
			Mqtt_Output = server->arg("Output");
			storage.nMqttPort = server->arg("Port").toInt();
			saveConfig();
			String message = "";
			message += "new MQTT IP ";
			message += Mqtt_IP;
			message += ", topic ";
			message += Mqtt_Input;
			server->send(200, "text/html", UtilityPage(message));
			ESP.restart();
		}
	}
	else
	{

	}
	SettingPage();
	//server->send(200, "text/html", SettingPage());
}

void TimeProc()
{
	if (is_authentified())
	{
		// Time Zone
		storage.nTimeZone = server->arg("Location").toInt();
		/*
		time_t time = getNtpTime();
		Serial.print("Current Timezone is ");
		Serial.println(storage.nTimeZone);
		if (time != 0)
			setTime(time);
		else
		{
			String strTime = server->arg("currtime");
			tmElements_t t;
			timeFromString(t, strTime);
			setTime(t.Hour, t.Minute, t.Second, t.Day, t.Month, t.Year);
		}*/
		Serial.println("Time setting is saved!");
		saveConfig();
	}
	Serial.println("Changed Timezone setting!");

	server->send(200, "text/html", UtilityPage("Changed Timezone setting!, Device will restar!"));
	ESP.restart();
}

void TimersProc()
{
	String msg = "";
	if (server->hasArg("sch") && server->hasArg("u"))
	{
		Serial.println("My proc!");
		if (server->arg("pp") == DevPass)
		{
			// Check Command syntax
			uint8_t nUnit = server->arg("u").toInt();
			uint8_t nSchNum = server->arg("sch").toInt();
			String Startime = server->arg("st");
			String Stoptime = server->arg("en");
			String DaySetting = server->arg("d");
			String MonthSetting = server->arg("m");
			if (((nUnit < 1 || nUnit > 6) || (nSchNum < 1 || nSchNum > 3)) || (DaySetting.length() != 7 || MonthSetting.length() != 12))
			{
				msg += "Invalid Command syntax.\r\n";
				msg += "Valid Command Syntax is http://ipAddr/timers?Name=devicename&sch=schnum&u=unitnum&st=starttime&en=endtime&d=daysetting&m=monthsetting&pp=password.\r\n";
				msg += "You have to keep input rule.\r\n";
				msg += "schnum=1~3, unitnum=1~6, starttime:HH:MM:SS:A/PM or SSA/Pmmm(sunset P/M, P:+, M:- minutes), SR+/-mmm(sunrise P/M, P:+, M:- minutes)\
          \r\ndaysetting=1111111(each variable means that say of week),monthsetting=111111111111(12 months).";
				server->send(200, "text/plain", msg);
				return;
			}

			if ((Startime.length() != 10 && (Startime.substring(0, 2) != "SS" && Startime.substring(0, 2) != "SR")) || (Stoptime.length() != 10 && (Stoptime.substring(0, 2) != "SS" && Stoptime.substring(0, 2) != "SR")))
			{
				msg += "Invalid Command syntax.\r\n";
				msg += "Valid Command Syntax is http://ipAddr/timers?Name=devicename&sch=schnum&u=unitnum&st=starttime&en=endtime&d=daysetting&m=monthsetting&pp=password.\r\n";
				msg += "You have to keep input rule.\r\n";
				msg += "schnum=1~3, unitnum=1~6, starttime:HH:MM:SS:A/PM or SSA/Pmmm(sunset P/M, P:+, M:- minutes), SR+/-mmm(sunrise P/M, P:+, M:- minutes)\
          \r\ndaysetting=1111111(each variable means that say of week),monthsetting=111111111111(12 months).";
				server->send(200, "text/plain", msg);
				return;
			}

			nUnit--;
			switch (nSchNum)
			{
			case 1:
			{
				for (uint8_t idx = 0; idx < 7; idx++)
				{
					if (DaySetting.charAt(idx) == '1')
						bitSet(storage.nSchedule1[nUnit].nDaySetting, idx);
					else
						bitClear(storage.nSchedule1[nUnit].nDaySetting, idx);
				}

				for (uint8_t idx = 0; idx < 12; idx++)
				{
					if (MonthSetting.charAt(idx) == '1')
						bitSet(storage.nSchedule1[nUnit].nMonthSetting, idx);
					else
						bitClear(storage.nSchedule1[nUnit].nMonthSetting, idx);
				}
				if (Startime.substring(0, 2) == "SS" || Startime.substring(0, 2) == "SR")
				{
					time_t Settingtime;
					bool sign = true;
					if (Startime.charAt(2) == 'P')
						sign = true;
					else
						sign = false;
					uint16_t nMinute = Startime.substring(3, Startime.length()).toInt();


					if (Startime.substring(0, 2) == "SS")
					{ // Sunset
						Settingtime = getSunsetTime(now());
					}
					else
					{
						Settingtime = getSunriseTime(now());
					}
					if (sign)
						Settingtime += nMinute * 60;
					else
						Settingtime -= nMinute * 60;

					storage.nSchedule1[nUnit].StartTime.nHour = hour(Settingtime);
					storage.nSchedule1[nUnit].StartTime.nMinute = minute(Settingtime);
					storage.nSchedule1[nUnit].StartTime.nSec = second(Settingtime);
				}
				else
				{
					storage.nSchedule1[nUnit].StartTime.nHour = Startime.substring(0, 2).toInt();
					storage.nSchedule1[nUnit].StartTime.nMinute = Startime.substring(3, 5).toInt();
					storage.nSchedule1[nUnit].StartTime.nSec = Startime.substring(6, 8).toInt();
					if (Startime.substring(8, 10) == "PM")
						storage.nSchedule1[nUnit].StartTime.nHour += 12;
				}


				if (Stoptime.substring(0, 2) == "SS" || Stoptime.substring(0, 2) == "SR")
				{
					time_t Settingtime;
					bool sign = true;
					if (Stoptime.charAt(2) == 'P')
						sign = true;
					else
						sign = false;
					uint16_t nMinute = Stoptime.substring(3, Stoptime.length()).toInt();


					if (Stoptime.substring(0, 2) == "SS") // Sunset
						Settingtime = getSunsetTime(now());
					else
						Settingtime = getSunriseTime(now());

					if (sign)
						Settingtime += nMinute * 60;
					else
						Settingtime -= nMinute * 60;

					storage.nSchedule1[nUnit].EndTime.nHour = hour(Settingtime);
					storage.nSchedule1[nUnit].EndTime.nMinute = minute(Settingtime);
					storage.nSchedule1[nUnit].EndTime.nSec = second(Settingtime);
				}
				else
				{
					storage.nSchedule1[nUnit].EndTime.nHour = Stoptime.substring(0, 2).toInt();
					storage.nSchedule1[nUnit].EndTime.nMinute = Stoptime.substring(3, 5).toInt();
					storage.nSchedule1[nUnit].EndTime.nSec = Stoptime.substring(6, 8).toInt();
					if (Stoptime.substring(8, 10) == "PM")
						storage.nSchedule1[nUnit].EndTime.nHour += 12;
				}

				msg = "Schedule1,";
				msg += "Unit";
				msg += String(nUnit + 1);
				msg += " Setting: Start Time - ";
				msg += TimeVartoString(storage.nSchedule1[nUnit].StartTime);
				msg += ", Stop Time-";
				msg += TimeVartoString(storage.nSchedule1[nUnit].EndTime);

				msg += ",Day:";
				for (uint8_t index = 0; index < 7; index++)
					msg += String(bitRead(storage.nSchedule1[nUnit].nDaySetting, index));
				msg += ",Month:";
				for (uint8_t index = 0; index < 12; index++)
					msg += String(bitRead(storage.nSchedule1[nUnit].nMonthSetting, index));
				msg += "\r\n";
			}
			break;
			case 2:
			{
				for (uint8_t idx = 0; idx < 7; idx++)
				{
					if (DaySetting.charAt(6 - idx) == '1')
						bitSet(storage.nSchedule2[nUnit].nDaySetting, idx);
					else
						bitClear(storage.nSchedule2[nUnit].nDaySetting, idx);

				}

				for (uint8_t idx = 0; idx < 12; idx++)
				{
					if (MonthSetting.charAt(11 - idx) == '1')
						bitSet(storage.nSchedule2[nUnit].nMonthSetting, idx);
					else
						bitClear(storage.nSchedule2[nUnit].nMonthSetting, idx);
				}

				if (Startime.substring(0, 2) == "SS" || Startime.substring(0, 2) == "SR")
				{
					time_t Settingtime;
					bool sign = true;
					if (Startime.charAt(2) == 'P')
						sign = true;
					else
						sign = false;
					uint16_t nMinute = Startime.substring(3, Startime.length()).toInt();


					if (Startime.substring(0, 2) == "SS")
					{ // Sunset
						Settingtime = getSunsetTime(now());
					}
					else
					{
						Settingtime = getSunriseTime(now());
					}
					if (sign)
						Settingtime += nMinute * 60;
					else
						Settingtime -= nMinute * 60;

					storage.nSchedule2[nUnit].StartTime.nHour = hour(Settingtime);
					storage.nSchedule2[nUnit].StartTime.nMinute = minute(Settingtime);
					storage.nSchedule2[nUnit].StartTime.nSec = second(Settingtime);
				}
				else
				{
					storage.nSchedule2[nUnit].StartTime.nHour = Startime.substring(0, 2).toInt();
					storage.nSchedule2[nUnit].StartTime.nMinute = Startime.substring(3, 5).toInt();
					storage.nSchedule2[nUnit].StartTime.nSec = Startime.substring(6, 8).toInt();
					if (Startime.substring(8, 10) == "PM")
						storage.nSchedule2[nUnit].StartTime.nHour += 12;
				}


				if (Stoptime.substring(0, 2) == "SS" || Stoptime.substring(0, 2) == "SR")
				{
					time_t Settingtime;
					bool sign = true;
					if (Stoptime.charAt(2) == 'P')
						sign = true;
					else
						sign = false;
					uint16_t nMinute = Stoptime.substring(3, Stoptime.length()).toInt();

					if (Stoptime.substring(0, 2) == "SS")
					{ // Sunset
						Settingtime = getSunsetTime(now());
					}
					else
					{
						Settingtime = getSunriseTime(now());
					}
					if (sign)
						Settingtime += nMinute * 60;
					else
						Settingtime -= nMinute * 60;

					storage.nSchedule2[nUnit].EndTime.nHour = hour(Settingtime);
					storage.nSchedule2[nUnit].EndTime.nMinute = minute(Settingtime);
					storage.nSchedule2[nUnit].EndTime.nSec = second(Settingtime);
				}
				else
				{
					storage.nSchedule2[nUnit].EndTime.nHour = Stoptime.substring(0, 2).toInt();
					storage.nSchedule2[nUnit].EndTime.nMinute = Stoptime.substring(3, 5).toInt();
					storage.nSchedule2[nUnit].EndTime.nSec = Stoptime.substring(6, 8).toInt();
					if (Stoptime.substring(8, 10) == "PM")
						storage.nSchedule2[nUnit].EndTime.nHour += 12;
				}


				msg = "Schedule2,";
				msg += "Unit";
				msg += String(nUnit + 1);
				msg += " Setting: Start Time - ";
				msg += TimeVartoString(storage.nSchedule2[nUnit].StartTime);
				msg += ", Stop Time-";
				msg += TimeVartoString(storage.nSchedule2[nUnit].EndTime);

				msg += ",Day:";
				for (uint8_t index = 0; index < 7; index++)
					msg += String(bitRead(storage.nSchedule2[nUnit].nDaySetting, index));
				msg += ",Month:";
				for (uint8_t index = 0; index < 12; index++)
					msg += String(bitRead(storage.nSchedule2[nUnit].nMonthSetting, index));
				msg += "\r\n";

			}
			break;
			case 3:
			{
				for (uint8_t idx = 0; idx < 7; idx++)
				{
					if (DaySetting.charAt(6 - idx) == '1')
						bitSet(storage.nSchedule3[nUnit].nDaySetting, idx);
					else
						bitClear(storage.nSchedule3[nUnit].nDaySetting, idx);

				}

				for (uint8_t idx = 0; idx < 12; idx++)
				{
					if (MonthSetting.charAt(11 - idx) == '1')
						bitSet(storage.nSchedule3[nUnit].nMonthSetting, idx);
					else
						bitClear(storage.nSchedule3[nUnit].nMonthSetting, idx);
				}


				if (Startime.substring(0, 2) == "SS" || Startime.substring(0, 2) == "SR")
				{
					time_t Settingtime;
					bool sign = true;
					if (Startime.charAt(2) == 'P')
						sign = true;
					else
						sign = false;
					uint16_t nMinute = Startime.substring(3, Startime.length()).toInt();


					if (Startime.substring(0, 2) == "SS")
					{ // Sunset
						Settingtime = getSunsetTime(now());
					}
					else
					{
						Settingtime = getSunriseTime(now());
					}

					if (sign)
						Settingtime += nMinute * 60;
					else
						Settingtime -= nMinute * 60;

					storage.nSchedule3[nUnit].StartTime.nHour = hour(Settingtime);
					storage.nSchedule3[nUnit].StartTime.nMinute = minute(Settingtime);
					storage.nSchedule3[nUnit].StartTime.nSec = second(Settingtime);
				}
				else
				{
					storage.nSchedule3[nUnit].StartTime.nHour = Startime.substring(0, 2).toInt();
					storage.nSchedule3[nUnit].StartTime.nMinute = Startime.substring(3, 5).toInt();
					storage.nSchedule3[nUnit].StartTime.nSec = Startime.substring(6, 8).toInt();
					if (Startime.substring(8, 10) == "PM")
						storage.nSchedule3[nUnit].StartTime.nHour += 12;
				}


				if (Stoptime.substring(0, 2) == "SS" || Stoptime.substring(0, 2) == "SR")
				{
					time_t Settingtime;
					bool sign = true;
					if (Stoptime.charAt(2) == 'P')
						sign = true;
					else
						sign = false;
					uint16_t nMinute = Stoptime.substring(3, Stoptime.length()).toInt();


					if (Stoptime.substring(0, 2) == "SS")
					{ // Sunset
						Settingtime = getSunsetTime(now());
					}
					else
					{
						Settingtime = getSunriseTime(now());
					}
					if (sign)
						Settingtime += nMinute * 60;
					else
						Settingtime -= nMinute * 60;

					storage.nSchedule3[nUnit].EndTime.nHour = hour(Settingtime);
					storage.nSchedule3[nUnit].EndTime.nMinute = minute(Settingtime);
					storage.nSchedule3[nUnit].EndTime.nSec = second(Settingtime);
				}
				else
				{
					storage.nSchedule3[nUnit].EndTime.nHour = Stoptime.substring(0, 2).toInt();
					storage.nSchedule3[nUnit].EndTime.nMinute = Stoptime.substring(3, 5).toInt();
					storage.nSchedule3[nUnit].EndTime.nSec = Stoptime.substring(6, 8).toInt();
					if (Stoptime.substring(8, 10) == "PM")
						storage.nSchedule3[nUnit].EndTime.nHour += 12;
				}

				msg = "Schedule3,";
				msg += "Unit";
				msg += String(nUnit + 1);
				msg = " Setting: Start Time-";
				msg += TimeVartoString(storage.nSchedule3[nUnit].StartTime);
				msg += ", Stop Time-";
				msg += TimeVartoString(storage.nSchedule3[nUnit].EndTime);

				msg += ",Day:";
				for (uint8_t index = 0; index < 7; index++)
					msg += String(bitRead(storage.nSchedule3[nUnit].nDaySetting, index));
				msg += ",Month:";
				for (uint8_t index = 0; index < 12; index++)
					msg += String(bitRead(storage.nSchedule3[nUnit].nMonthSetting, index));
				msg += "\r\n";
			}
			break;
			}
		}
		else
		{
			msg = "Invalid Password!";
			msg += "Invalid Command syntax.\r\n";
			msg += "Valid Command Syntax is http://ipAddr/timers?Name=devicename&sch=schnum&u=unitnum&st=starttime&en=endtime&d=daysetting&m=monthsetting&pp=password.\r\n";
			msg += "You have to keep input rule.\r\n";
			msg += "schnum=1~3, unitnum=1~6, starttime:HH:MM:SS:A/PM or SS+/-mmm(sunset +- minutes), SR+/-mmm(sunrise +- minutes)\
          \r\ndaysetting=1111111(each variable means that say of week),monthsetting=111111111111(12 months).";
		}
	}
	else
		msg = "Invalid Command Format!";
	server->send(200, "text/plain", msg);

}

void CloseRelayProc()
{
	String message;
	String strPassword = server->arg("pp");
	if (is_authentified())
	{
		RelayPermant = false;
		nTimeMs = millis();
		relayState = false;
		message += "Relay ";
		message += DeviceName;
		message += "\r\nClosed!";
	}
	else
	{
		if (DevPass == server->arg("pp"))
		{
			RelayPermant = false;
			nTimeMs = millis();
			relayState = false;
			message += "Relay ";
			message += DeviceName;
			message += "\r\nClosed!";
		}
		else
			message += "Invalid Password.\nYou can not this action without correct password!";
	}
	server->send(200, "text/html", UtilityPage(message));
}

void MacProc()
{
	String strPassword = server->arg("pp");
	String message;
	if (DevPass == strPassword)
	{
		String macID;
		for (uint8_t idx = 0; idx < WL_MAC_ADDR_LENGTH; idx++)
		{
			if (mac[idx] < 16)
				macID += "0";
			macID += String(mac[idx], HEX);
		}
		macID.toUpperCase();
		message += "Mac Address is ";
		message += macID;
		message += ".\r\n";
	}
	else
		message += "Invalid Password.\nYou can not this action without correct password!";

	server->send(200, "text/plain", message);
}

void statusProc()
{
	String strPassword = server->arg("pp");
	String message;
	if (DevPass == strPassword)
	{
		message = DeviceName;
		if (relayState)
			message += " Opened";
		else
			message += " Closed";
	}
	else
		message += "Invalid Password.\nYou can not this action without correct password!";
	server->send(200, "text/plain", message);
}

void iostatusProc()
{
	String message;
	String strPassword = server->arg("pp");
	if (DevPass.equals(strPassword))
	{
		bool bStatus = digitalRead(InputPIN);
		if (bStatus)
			message += "1";
		else
			message += "0";
	}
	else
		message += "Invalid Password.\nYou can not this action without correct password!";
	server->send(200, "text/plain", message);
}

void ClientProc()
{
	if (is_authentified())
	{
		if (server->hasArg("IP") && server->hasArg("Port"))
		{
			TCPServerIP = server->arg("IP");
			storage.nServerPort = server->arg("Port").toInt();
			saveConfig();
			String message = "";
			message += "New TCP Server IP is ";
			message += TCPServerIP;
			message += ", Port is ";
			message += String(storage.nServerPort);
			message += ", Port is ";
			server->send(200, "text/html", UtilityPage(message));
			ESP.restart();
		}
	}
}

String TimeVartoString(struct TimeVar t)
{
	String result = "";
	if (t.nHour > 12)
	{// PM
		if ((t.nHour - 12) < 10)
			result += "0";
		result += String(t.nHour - 12);
		result += ":";

		if (t.nMinute < 10)
			result += "0";
		result += String(t.nMinute);
		result += ":";

		if (t.nSec < 10)
			result += "0";
		result += String(t.nSec);
		result += " ";

		result += "PM";
	}
	else if (t.nHour == 12)
	{
		result += String(12);
		result += ":";

		if (t.nMinute < 10)
			result += "0";
		result += String(t.nMinute);
		result += ":";

		if (t.nSec < 10)
			result += "0";
		result += String(t.nSec);
		result += " ";

		result += "PM";
	}
	else if (t.nHour == 0)
	{
		result += String(12);
		result += ":";

		if (t.nMinute < 10)
			result += "0";
		result += String(t.nMinute);
		result += ":";

		if (t.nSec < 10)
			result += "0";
		result += String(t.nSec);
		result += " ";

		result += "AM";
	}
	else
	{
		if (t.nHour < 10)
			result += "0";
		result += String(t.nHour);
		result += ":";

		if (t.nMinute < 10)
			result += "0";
		result += String(t.nMinute);
		result += ":";

		if (t.nSec < 10)
			result += "0";
		result += String(t.nSec);
		result += " ";

		result += "AM";
	}

	return result;
}

String UtilityPage(String Detailed)
{
	String HttpContent = "";
	HttpContent = "<html lang='en'>\
    <head>\
    <meta charset='utf-8'>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <title>ESP8266 WIFI Relay Control Page</title>\
    <link rel='stylesheet' href='http://netdna.bootstrapcdn.com/bootstrap/3.1.1/css/bootstrap.min.css'>\
    <style>body{padding-top:25px;padding-bottom:20px}.header{border-bottom:1px solid #e5e5e5;margin-bottom:0;color:#D4AC0D}\
      .jumbotron{text-align:center}.marketing{margin:40px 0}\
      .arduino h4{font-size:22px;color:#2ecc71;margin-top:20px;padding-right:10px;padding-left:0; display:inline-block;}\
      .arduino h5{font-size:16px;color:#E74C3C;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .arduino h6{font-size:16px;color:#2ecc71;margin-top:15px;padding-right:0;padding-left:0px; display:inline-block;}\
      .clear{ clear:both;}\
      .align-center {text-align:center;}\
    </style>\
  <script>\
  function goBack() {\
    window.history.back()\
  }\
  </script>\
    </head>\
    <body style='background-color:#E8F8F5'>\
    <div class='container align-center'>\
      <div class='header'>\
      <h1>Welcome to Litmath, WiFi Relay</h1>\
      </div>\
      <div class='row arduino'>\
      <div class='col-lg-12'>\
        <div class='clear'></div>\
        <br />\
        <h4>You have changed settings of your system.</h4>\
        <div class='clear'></div>\
        <h5>";
	HttpContent += Detailed;
	HttpContent += "</h5>\
        <div class='clear'></div>\
        <h6> You can go <a href='javascript:history.back(-1);'>back.</a></h6>\
        <br /><br />\
        <h6> <a href='http://www.litmath.com'>Visit our web site Litmath, LLC</a></h6>\
        &nbsp&nbsp&nbsp&nbsp\
        <h6> Copyright Litmath, LLC, 2017 </h6>\
      </div>\
      </div>\
    </div>\
    </body>\
  </html>";
	return HttpContent;
}