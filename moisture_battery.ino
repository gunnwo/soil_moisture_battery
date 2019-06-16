
#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include <WiFiManager.h> 
#include <PubSubClient.h>

#include <ArduinoJson.h>

WiFiServer server(80);

char mqtt_server[50] = "heizung.fritz.box";
char name_sensor[50] = "WLAN-sensor";
char logger_version[15] = __DATE__;


int PinPower = 4; // D2
int PinSwitch = 5; // D1

long lSoilMin = 450;   // moisture min air
long lSoilMax = 850;   // moisture max water

unsigned long lMinutes = 1;    // x minutes wait until next measure
long lHumOff = 10;



char topic_moisture[20] = "sensor/soil";  //mqtt path
char topic_ip[20] = "sensor/ip";  //mqtt path
char topic_name[20] = "sensor/name";  //mqtt path


WiFiClient espclient;
PubSubClient mqttclient(espclient);


bool mqtt_reconnect()
{
	// Loop until we're reconnected
	if (!mqttclient.connected())
	{
		String host;
		host = WiFi.hostname();
		host.toLowerCase();

		// Attempt to connect
		if (mqttclient.connect( host.c_str() ))
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	return true;
}


//////////////////////////
// create HTTP 1.1 header
//////////////////////////
String MakeHTTPHeader(unsigned long ulLength)
{
	String sHeader;

	sHeader = F("HTTP/1.1 200 OK\r\nContent-Length: ");
	sHeader += ulLength;
	sHeader += F("\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");

	//Serial.println(sHeader);

	return(sHeader);
}


void handleSetup(WiFiClient *pclient)
{
	String sResponse1="", sResponse2="", sResponse3="";
	Serial.println("page setup");

	sResponse1 = F("<html><head><title>bodenfeuchte setup</title>\n");
	sResponse1 += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0,user-scalable=yes\">");
	sResponse1 += F("<style>body{text-align:center;font-family:verdana;background-color:#ffffff;}");
	sResponse1 += F("input{width:300px}");
	sResponse1 += F("button{border:0;border-radius:0.3rem;background-color:#669900;color:#ffffff;line-height:2.4rem;font-size:1.2rem;width:300px}");
	sResponse1 += F("table{border:1px solid black;border-collapse:collapse;width:300px}");
	sResponse1 += F("th{border:1px solid black;border-collapse:collapse;padding:5px;background-color:#e6e6e6}");
	sResponse1 += F("td{border:1px solid black;border-collapse:collapse;padding:5px;}</style>");
	sResponse1 += F("\n<body><h1>Bodenfeuchte Einstellungen</h1><br>\r\n");

	sResponse2 =  F("<form action=\"/\" method='get' id='form1'>\r\n");

	sResponse2 += F("<p>Anzeigename:<br><input name='disp' value='");
	sResponse2 += name_sensor;
	sResponse2 += F("' type='text'><br></p>\r\n");

	sResponse2 += F("<p>Interval(Minuten):<br><input name='scan' value='");
	sResponse2 += lMinutes;
	sResponse2 += F("' type='number' min='1' max='15'><br></p>");

	sResponse2 += F("<p>Messwert untere Grenze (Wasser):<br><input name='soilmin' value='");
	sResponse2 += lSoilMin;
	sResponse2 += F("' type='text'><br></p>");

	sResponse2 += F("<p>Messwert obere Grenze (Luft):<br><input name='soilmax' value='");
	sResponse2 += lSoilMax;
	sResponse2 += F("' type='text'><br></p>");

	sResponse2 += F("<p>MQTT-Broker:<br><input name='mqtt' value='");
	sResponse2 += mqtt_server;
	sResponse2 += F("' type='text'><br></p>\r\n");

	sResponse2 += F("</form><br><br>\r\n");

	sResponse2 += F("<p><button onclick=\"window.location.href='/setup?CMD=SETMIN'\">Setup Minimum (Wasser)</button></p>");
	sResponse2 += F("<p><button onclick=\"window.location.href='/setup?CMD=SETMAX'\">Setup Maximum (Luft)</button></p>");
	sResponse2 += F("<br/>");
	sResponse2 += F("<p><button type='submit' form='form1' value='Submit'>Speichern</button></p>");
	sResponse2 += F("</body></html>");

	// Send the response to the client - delete strings after use to keep mem low
	pclient->print(MakeHTTPHeader(sResponse1.length() + sResponse2.length() ));
	pclient->print(sResponse1); 
	pclient->print(sResponse2);
}


void setup()
{
	// hardware setup
	pinMode(PinPower, OUTPUT);
	pinMode(PinSwitch, INPUT);

	digitalWrite(PinPower, 1);

	Serial.begin(57600);
	Serial.print("compiled: ");
	Serial.print(__DATE__);
	Serial.println(__TIME__);

	//read configuration from FS json
	loadParams();

	// The extra parameters to be configured (can be either global or just in the setup)
	// After connecting, parameter.getValue() will get you the configured value
	// id/name placeholder/prompt default length

	WiFiManagerParameter custom_mqtt_server("mqtt", "MQTT broker", mqtt_server, 30);
	WiFiManagerParameter custom_name_sensor("name", "Sensor name", name_sensor, 30);

	char clocal[8];
	sprintf(clocal, "%03X", ESP.getChipId());
	String sName = clocal;
	sName = "SOIL" + sName;
	WiFi.hostname(sName);

	Serial.print("HOST:=");
	Serial.println(WiFi.hostname());

	WiFiManager wifiManager;
	wifiManager.addParameter(&custom_mqtt_server);
	wifiManager.addParameter(&custom_name_sensor);

	wifiManager.setTimeout(120);

	if (!wifiManager.autoConnect("SENSOR"))
	{
		Serial.println("ERROR: failed to connect and hit timeout, deepsleep 10s");
		ESP.deepSleep(5 * 60 * 1000 * 1000); //microseconds
	}

	//if you get here you have connected to the WiFi
	Serial.println("-connected");

	//read updated parameters
	strcpy(mqtt_server, custom_mqtt_server.getValue());
	strcpy(name_sensor, custom_name_sensor.getValue());

	WiFi.setAutoReconnect(true);

	// first connection establish, lets initialize the handlers needed for proper reconnect
	Serial.println("-connecting WIFI event handler");

	Serial.print("IP:=");
	Serial.println(WiFi.localIP());
	Serial.print("SSID:=");
	Serial.println(WiFi.SSID());

	// Wifi server
	server.begin();
	Serial.println("Webserver - server started");

	// MQTT
	IPAddress mqttServerIP;
	WiFi.hostByName(mqtt_server, mqttServerIP);

	Serial.print("MQTT-broker:=");
	Serial.print(mqtt_server);
	Serial.print(" - ");
	Serial.println(mqttServerIP);

	mqttclient.setServer(mqtt_server, 1883);
	mqtt_reconnect();

	{
		// *** name ***
		char mqtt_path[60] = { '/0' };
		String host;
		host = WiFi.hostname();
		host.toLowerCase();

		char host2[20] = { '/0' };
		for (int i = 0; i < host.length(); i++)
		{
			host2[i] = host.charAt(i);
		}

		sprintf(mqtt_path, "%s/%s", host2, topic_name);
		mqttclient.publish(mqtt_path, name_sensor, false);
	}

	{
		// *** IP ***
		char mqtt_path[60] = { '/0' };
		String host;
		host = WiFi.hostname();
		host.toLowerCase();

		char host2[20] = { '/0' };
		for (int i = 0; i < host.length(); i++)
		{
			host2[i] = host.charAt(i);
		}

		sprintf(mqtt_path, "%s/%s", host2, topic_ip);
		mqttclient.publish(mqtt_path, String(WiFi.localIP().toString()).c_str(), false);
	}

}



bool saveParams()
{
	bool retVal = false;
	Serial.println("writing config file");

	File configFile = SPIFFS.open("/config.json", "w");
	if (!configFile)
	{
		Serial.println("-failed to create config file");
		retVal =  false;
	}
	else
	{
		DynamicJsonBuffer jsonBuffer;
		JsonObject& json = jsonBuffer.createObject();

		json["mqtt"] = mqtt_server;
		json["scan"] = lMinutes;
		json["name"] = name_sensor;
		json["smin"] = lSoilMin;
		json["smax"] = lSoilMax;

		if (json.printTo(configFile) == 0)
		{
			Serial.println("-failed to write config file");
			retVal= false;
		}
		else
		{
			retVal = true;
		}
		configFile.close();
	}

	return retVal;
}


bool loadParams()
{
	bool retVal = false;

	//read configuration from FS json
	Serial.println("mounting FS...");

	if (SPIFFS.begin())
	{
		Serial.println("-mounted file system");
		if (SPIFFS.exists("/config.json"))
		{
			//file exists, reading and loading
			Serial.println("reading config file");
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile)
			{
				Serial.println("-opened config file");

				size_t size = configFile.size();
				// Allocate a buffer to store contents of the file.
				std::unique_ptr<char[]> buf(new char[size]);

				configFile.readBytes(buf.get(), size);
				DynamicJsonBuffer jsonBuffer;
				JsonObject& json = jsonBuffer.parseObject(buf.get());

				if ( !json.success() )
				{
					Serial.println("-failed to deserialize file: ");
					retVal = false;
				}
				else
				{
					if (json.containsKey("mqtt"))
					{
						strcpy(mqtt_server, json["mqtt"]);
					}
					if (json.containsKey("name"))
					{
						strcpy(name_sensor, json["name"]);
					}
					if (json.containsKey("scan"))
					{
						unsigned int lMin = int(json["scan"]);
						if (lMin > 0)
						{
							lMinutes = lMin;
						}
					}
					if (json.containsKey("smin"))
					{
						lSoilMin = int(json["smin"]);
					}
					if (json.containsKey("smax"))
					{
						lSoilMax = int(json["smax"]);
					}

					retVal = true;
				}

				json.printTo(Serial);
				Serial.println(" ");
			}
			else
			{
				Serial.println("-failed to load json config");
				retVal = false;
			}
			configFile.close();
		}
		else
		{
			Serial.println("-no json config");
			retVal = true;
		}
	}
	else
	{
		Serial.println("-failed to mount FS");
		retVal = false;
	}
	
	return retVal;
}

// main routine
// wifi/mqtt are connected
// - setup button is pressed: show setup dialog until SAVE, start DeepSleep
// - setup button is pressed: read analog, mqtt, DeepSleep

void loop()
{

	int iSwitch = 0;
	iSwitch = digitalRead(PinSwitch);

	int inVal = 0;
	inVal = analogRead(A0);

	float fMoist = -999;
	if (lSoilMax != lSoilMin)
	{
		fMoist = 100 - 100 * (inVal - lSoilMin) / (lSoilMax - lSoilMin);  
	}

	if (!iSwitch)
	{
		digitalWrite(PinPower, 0);
	}

	Serial.print("LOG: switch=");
	Serial.print(iSwitch);
	Serial.print("   moist=");
	Serial.print(fMoist);
	Serial.print(" % (");
	Serial.print(inVal);
	Serial.println(")");
	
	if ( !WiFi.isConnected() )
	{
		Serial.println("ERROR: no wifi");
	}
	else
	{
		if (!mqttclient.connected())
		{
			Serial.print("MQTT reconnect, ");
			if ( !mqtt_reconnect() )
			{
				Serial.println("-fails");
			}
			else
			{
				Serial.println("-success");
			}
		}
	}

	if (mqttclient.connected())
	{
/*
		{
			// *** name ***
			char mqtt_path[60] = { '/0' };
			String host;
			host = WiFi.hostname();
			host.toLowerCase();

			char host2[20] = { '/0' };
			for (int i = 0; i < host.length(); i++)
			{
				host2[i] = host.charAt(i);
			}

			sprintf(mqtt_path, "%s/%s", host2, topic_name);
			mqttclient.publish(mqtt_path, name_sensor, false);
		}

		{
			// *** IP ***
			char mqtt_path[60] = { '/0' };
			String host;
			host = WiFi.hostname();
			host.toLowerCase();

			char host2[20] = { '/0' };
			for (int i = 0; i < host.length(); i++)
			{
				host2[i] = host.charAt(i);
			}

			sprintf(mqtt_path, "%s/%s", host2, topic_ip);
			mqttclient.publish(mqtt_path, String(WiFi.localIP().toString()).c_str(), false);
		}
*/
		{
			// *** moisture ***
			char mqtt_path[60] = { '/0' };
			String host;
			host = WiFi.hostname();
			host.toLowerCase();

			char host2[20] = { '/0' };
			for (int i = 0; i < host.length(); i++)
			{
				host2[i] = host.charAt(i);
			}

			sprintf(mqtt_path, "%s/%s", host2, topic_moisture);
			mqttclient.publish(mqtt_path, String(fMoist).c_str(), false);

			Serial.print(mqtt_path);
			Serial.print("=");
			Serial.println(String(fMoist).c_str());
		}

		if (!iSwitch)
		{
			espclient.flush();
			delay(100);
			mqttclient.disconnect();
		}

	}

	if (!iSwitch)
	{
		Serial.println("Going to sleep (3) - send MQTT");
		ESP.deepSleep(lMinutes * 60 * 1000 * 1000); //TODO, test 1sec
	}
	

	///////////////////////////////////
	// Check if a client has connected
	///////////////////////////////////
	WiFiClient wificlient = server.available();
	if (!wificlient)
	{
		Serial.println("WEB - no client");
		return;
	}

	// Wait until the client sends some data
	Serial.println("WEB - new client");
	unsigned long ultimeout = millis() + 250;
	while (!wificlient.available() && (millis()<ultimeout))
	{
		delay(1);
	}
	if (millis()>ultimeout)
	{
		Serial.println("WEB - client connection time-out!");
		return;
	}

	/////////////////////////////////////
	// Read the first line of the request
	/////////////////////////////////////
	String sRequest = wificlient.readStringUntil('\r');
	Serial.println(sRequest);
	wificlient.flush();

	// stop client, if request is empty
	if (sRequest == "")
	{
		Serial.println("WEB - empty request! stopping client");
		wificlient.stop();
		return;
	}

	// get path; end of path is either space or ?
	// Syntax is e.g. GET /?show=1234 HTTP/1.1
	String sPath     = "";
	String sParam    = "";
	String sCmd      = "";
	String sGetstart = "GET ";

	int iStart, iEndSpace, iEndQuest;
	iStart = sRequest.indexOf(sGetstart);


	if (iStart >= 0)
	{
		iStart += +sGetstart.length();
		iEndSpace = sRequest.indexOf(" ", iStart);
		iEndQuest = sRequest.indexOf("?", iStart);

		// are there parameters?
		if (iEndSpace>0)
		{
			if (iEndQuest>0)
			{
				// there are parameters
				sPath = sRequest.substring(iStart, iEndQuest);
				sParam = sRequest.substring(iEndQuest, iEndSpace);
			}
			else
			{
				// NO parameters
				sPath = sRequest.substring(iStart, iEndSpace);
			}
		}
	}

	Serial.print("WebPath=");
	Serial.println(sPath);
	Serial.print("SetParam=");
	Serial.println(sParam);

	bool bDoSave = false;

	if (sParam.length() > 0)
	{
		// sParam=?disp=WLAN-log&scan=11&mqtt=heizung.fritz.box&ntp=0.de.pool.ntp.org
		while (sParam.length()>0 && sParam.indexOf("=")>0 )
		{
			if (sParam.indexOf("&")==0 || sParam.indexOf("?")==0)
			{
				// remove leading & and ?
				sParam = sParam.substring(1);
			}

			int iEqu = sParam.indexOf("=");

			String sNam = sParam.substring(0, iEqu); // param
			String sVal = sParam.substring(iEqu+1);        // val

			if ( sParam.indexOf("&")>0)
			{
				sVal = sParam.substring(iEqu+1, sParam.indexOf("&"));
			}

			Serial.print("param ");
			Serial.print(sNam);
			Serial.print("=");
			Serial.println(sVal);
				
			if (sNam.equalsIgnoreCase("cmd") && sVal.equalsIgnoreCase("setmin"))
			{
				lSoilMin = analogRead(A0);
				bDoSave = true;
			}
			else if (sNam.equalsIgnoreCase("cmd") && sVal.equalsIgnoreCase("setmax"))
			{
				lSoilMax = analogRead(A0);
				bDoSave = true;
			}
			else if (sNam.equalsIgnoreCase("disp"))
			{
				strcpy(name_sensor, sVal.c_str());
				bDoSave = true;
			}
			else if (sNam.equalsIgnoreCase("mqtt") )
			{
				strcpy(mqtt_server, sVal.c_str());
				bDoSave = true;
			}
			else if (sNam.equalsIgnoreCase("scan") )
			{
				lMinutes = sVal.toInt();
				bDoSave = true;
			}
			else if (sNam.equalsIgnoreCase("soilmin") )
			{
				lSoilMin = sVal.toInt();
				//bDoSave = true;
			}
			else if (sNam.equalsIgnoreCase("soilmax") )
			{
				lSoilMax = sVal.toInt();
				//bDoSave = true;
			}

			sNam = "";
			sVal = "";

			if (sParam.indexOf("&") >= 0)
			{
				sParam = sParam.substring(sParam.indexOf("&"));
			}
			else
				sParam = "";

		}
		if (bDoSave)
		{
			saveParams();
		}
	}

	/////////////////////////////
	// format the html page for /
	/////////////////////////////
	handleSetup(&wificlient);

	// and stop the client
	wificlient.stop();
	Serial.println("Client disconnected");
}
