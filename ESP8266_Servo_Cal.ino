
#define VERSION "0.0q"

#define SERVO

/* the following can be set from the config.json file */
String wifiSSID = "yourSSID";		// ssid
String wifiPSK = "yourPSK";			// psk
String nodeID;						// node
bool autoconnect = true;			// autoconnect

#define nameOf(x)		#x			// return x as a string
#define nameOfName(x)	nameOf(x)

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ipaddress.h>
#include <ESP8266WiFiGratuitous.h>

#include <fs.h>
#include <ArduinoJson.h>

#if defined(PWM)
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#else
#include <Servo.h>
#define SERVO_PIN       	D7		// control pin for servo
#endif

extern "C" 
{
#include <user_interface.h>
}

size_t rootStringLength;			// size of last generated root html

const int LED_PIN = LED_BUILTIN;
byte led;
// Web Server for OTA
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
File fsUploadFile;

#if defined(PWM)
#define PWM_ADDR        	0x40 	// address of PWM
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(PWM_ADDR);
#else
#define SERVO
Servo servo;
#endif

#define SERVOMIN  	500 		// This is the 'minimum' pulse length in usecs
#define SERVOMAX  	2500 		// This is the 'maximum' pulse length count in usecs
#define SERVO_FREQ 	50 			// Analog servos run at ~50 Hz updates

uint16_t pulsemin = SERVOMIN;
uint16_t pulsemax = SERVOMAX;
uint16_t pulseleft;
uint16_t pulseright;
uint16_t current;
uint16_t previous = 0;

void servoWriteMicroseconds(uint16_t val)
{
    current = val;
    if (previous == val)
        return;
    previous = val;
#if defined(PWM)
    pwm.writeMicroseconds(val);
#else
    servo.writeMicroseconds(val);
#endif
}

bool loadConfiguration(String configTitle);

String generateSetForm(String name, String action, int min, int max, int val)
{
    String s; 
    String valuename = action.charAt(0) + String("value");
    String sv;

    sv = "min='" + String(min) + "' "
        "max='" + String(max) + "' "
        "value='" + String(val) + "' ";

    s = name + "<br>\n"
        "<form method='GET' action='" + action + "' enctype='multipart/form-data'>\n"
        "<input type='range' id='x" + valuename + "' name='x " + valuename + "' " +
        sv +
        "oninput='this.nextElementSibling.value = this.value'>\n"
        "<input type='number' id='" + valuename + "' name='" + valuename + "' " +
        sv +
        "oninput='this.previousElementSibling.value = this.value'>\n" +
        "<input type='submit' value='Set'>\n" +
        "</form>\n" +
        "<br>";

    return(s);
}

String generateFileForm(String name, String action)
{
	String s;
	String capaction = action;
	capaction.setCharAt(0,toupper(capaction.charAt(0)));
	s = name + "<br><form method='POST' action='" + action + "' enctype='multipart/form-data'>\n"
		"<input type='file' name='" + action + "'>\n" +
		"<input type='submit' value='" + capaction + "'>\n" +
		"</form><br>";
	return(s);
}

void handleRoot()
{
    String s;
    const String br = "<br>";   
    int deg;
    
    IPAddress ipaddr = WiFi.localIP();

	if (rootStringLength)
	{
		// reserve string size based on prior observation
		s.reserve(rootStringLength*1.25);
	}
    
    s = R"(<!DOCTYPE html>
        <html lang='en'>
        <head>
        <style>
        body {
            background-color: lightblue;
        }

        h1 {
            text-align: center;
            font-size: 22px;
        }

        p {
            font-family: verdana;
            font-size: 20px;
        }

        form {
            font-family: verdana;
            font-size: 20px;
        }

        </style>
        </head>
        <body> <h1>)";
    s += "ESP8266 Servo Calibrator "
        "Version: "
        VERSION
        "</h1>"
        "IP Address: " + ipaddr.toString();
    s += "<br>"
        "Hostname: " + WiFi.hostname();
    s += "<br>"
        "mDNS name: " + nodeID;
    s += "<br><br>";  

    s += "Minimum pulse width: " + String(pulsemin) + " usecs";
    s += br;
    s += "Maximum pulse width: " + String(pulsemax) + " usecs";
    s += br;  
    s += "Current pulse width: " + String(current) + " usecs   (";
    deg = map(current, pulsemin, pulsemax, 0, 180);
    s += String(deg) + "\xb0)";

    s += "<br><br>\n";

    s += generateSetForm("Left", "left", pulsemin, pulsemax, pulseleft);
    s += generateSetForm("Right", "right", pulsemin, pulsemax, pulseright);
 
    s += R"(System Restart<br>
        <form method='GET' action='restart' enctype='multipart/form-data'>
            <input type='submit' value='Restart'>
        </form>
        <br>)";

	s += generateFileForm("Firmware Update","update");
	s += generateFileForm("File Upload","upload");

  	s += R"(  
        </body>
        </html>)";
        
    httpServer.send(200, "text/html", s);

    rootStringLength = s.length();
    Serial.println("Root string length=" + String(rootStringLength));
}    

void handleLeft()
{
    current = pulseleft = httpServer.arg("lvalue").toInt();
    handleRoot();
}

void handleRight()
{
    current = pulseright = httpServer.arg("rvalue").toInt();
    handleRoot();
}

void handleFileUpload()
{
    Serial.println("In handleFileUpload");
    if (httpServer.uri() != "/upload") 
        return;
    HTTPUpload& 
        upload = httpServer.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
        String filename = upload.filename;
        if (!filename.startsWith("/")) 
            filename = "/" + filename;
        Serial.print("handleFileUpload Name: "); 
        Serial.println(filename);
        updateDisplay(3,"File uploading");
        fsUploadFile = SPIFFS.open(filename, "w");
        filename = String();
    } 
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (fsUploadFile)
        	fsUploadFile.write(upload.buf, upload.currentSize);
    } 
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (fsUploadFile)
            fsUploadFile.close();
        Serial.print("handleFileUpload Size: "); 
        Serial.println(upload.totalSize);
        updateDisplay(3,"File uploaded");
    }
}

void handleRestart()
{
    String s;
    Serial.println("Restarting");
    updateDisplay(1,"Restarting");
    s = "<!DOCTYPE HTML>\r\n<html>\r\n<body>";
    s += "<META http-equiv='refresh' content='15;URL=/'>Restarting...";
    httpServer.send(200, "text/html", s);
    delay(500);
    ESP.restart();
}
void setup() 
{
    Serial.begin(115200);
    Serial.println("\nESP8266 Servo Calibrator " VERSION);
    
    nodeID = "esp" + String(system_get_chip_id(),HEX);
    Serial.println("Node: " + nodeID);
    
#if defined(PWM)
    Serial.println("SCL: " + String(SCL));
    Serial.println("SDA: " + String(SDA));
#else
	Serial.println("Servo Pin " nameOfName(SERVO_PIN));
#endif
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

#if defined(PWM)
    Wire.begin(SCL,SDA);
    Wire.beginTransmission(PWM_ADDR);
    byte werr = Wire.endTransmission();
    if (werr == 0)
        Serial.println("PWM found");
    else
        Serial.println("PWM not found");

    pwm.begin();   
 
    pwm.setOscillatorFrequency(27000000);
    pwm.setPWMFreq(SERVO_FREQ);  
#endif
    
    delay(10);

    SPIFFSConfig cfg;
    cfg.setAutoFormat(false);
    SPIFFS.setConfig(cfg);  
    if (SPIFFS.begin())
    {
        FSInfo fs_info;
        SPIFFS.info(fs_info);
        Serial.println("SPIFFS size = " + String(fs_info.totalBytes));
        loadConfiguration("/config.json");
    }
    else
    {
        Serial.println("Unable to mount SPIFFS");
    }

    // wait until config file read before initialising servo
#if defined(SERVO)
    servo.attach(SERVO_PIN,pulsemin,pulsemax);
#endif

    // exercise the servo
    servoWriteMicroseconds(current = pulseright = pulsemax);
    delay(1000);
    current = pulseleft = pulsemin;

	// connect to WiFi and MDS
    connectWiFi();
    setupMDNS();

    // set up HTTP server
    httpUpdater.setup(&httpServer);
    httpServer.begin();

    // general form
    httpServer.on("/", HTTP_GET, handleRoot);
    // left
    httpServer.on("/left", HTTP_GET, handleLeft);
    // right
    httpServer.on("/right", HTTP_GET, handleRight);
    // note that update capability is done through httpUpdater with /update
    // setup upload capability
    httpServer.on("/upload", HTTP_POST, [](){ httpServer.send(200, "text/plain", "file uploaded"); }, handleFileUpload);
    // setup reset
    httpServer.on("/restart", HTTP_GET, handleRestart);
}


void loop()
{
    static long next;
    if (millis() > next)
    {
        digitalWrite(LED_PIN,led = !led);
        next = millis()+500;
    }

    // mDNS
    MDNS.update();
    
    // check for web activity
    httpServer.handleClient();

    // update servo if necessary
    servoWriteMicroseconds(current);
}

bool loadConfiguration(String configTitle)
{
    StaticJsonDocument<512> jsonDoc;
    char configBuffer[512];
    File configFile;  
    
    Serial.println(configTitle);        
    configFile = SPIFFS.open(configTitle.c_str(),"r");
    if (!configFile)
    {
        Serial.println("No config file found");
        return(true);
    }
    else
    {
        int rslt = configFile.readBytes(configBuffer,sizeof(configBuffer));
        if (rslt == sizeof(configBuffer))
        {
            Serial.println("Warning--config file too big");
        }
        else
        {
            DeserializationError error = deserializeJson(jsonDoc, configBuffer);
            if (error)
            {
                Serial.println("Error parsing json");
                configFile.close();
                return(true);
            }
            else
            {
            	// extract configuration values
                const char *val;
                if (val = jsonDoc["ssid"])
                    wifiSSID = String(val);
                if (val = jsonDoc["psk"])
                    wifiPSK = String(val);                
                if (val = jsonDoc["autoconnect"])
                    autoconnect = String(val).toInt();
                if (val = jsonDoc["node"])
                    nodeID = String(val);
                if (val = jsonDoc["min"])
                    pulsemin = String(val).toInt(); 
                if (val = jsonDoc["max"])
                    pulsemax = String(val).toInt();
            }
        }      
        configFile.close();
    }     
    return(false);  
}


void connectWiFi()
{     

    updateDisplay(1,"Connecting");

    if (WiFi.getAutoConnect())
    {
        WiFi.begin();
        Serial.println("Reconnecting to: " + WiFi.SSID());
    }
    else
    {
        Serial.println("Connecting to: " + wifiSSID);
        WiFi.begin(wifiSSID.c_str(),wifiPSK.c_str());
    }

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi status = " + String(WiFi.status()));
        // Blink the LED
        digitalWrite(LED_PIN, led = !led);
        delay(1000);
    }
    digitalWrite(LED_PIN, led = HIGH);
    
    Serial.println("WiFi connected");  
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // this forces an ARP to be sent
    experimental::ESP8266WiFiGratuitous::stationKeepAliveSetIntervalMs(2000);

    // automatically reconnect if dropped
    WiFi.setAutoReconnect(true);
        
    // Set WiFi to automatically connect if so configured
    WiFi.setAutoConnect(autoconnect);

    updateDisplay(1,"Connected");
}

void setupMDNS()
{
    // Call MDNS.begin(<domain>) to set up mDNS to point to
    // "<domain>.local"
    if (!MDNS.begin(nodeID.c_str())) 
    {
        Serial.println("Error setting up MDNS responder!");
        while(1)
        { 
            delay(1000);
        }
    }
    MDNS.addService("tcp","http",80);
    Serial.println("mDNS responder started");
}

void updateDisplay(int line,String s)
{
    /*
    display.setColor(BLACK);
    display.fillRect(0,line*11,display.getWidth(),11);
    display.setColor(WHITE);
    display.drawString(0,line*11,s);
    display.display();
    */
}
