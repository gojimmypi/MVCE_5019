#include "GlobalDefine.h"
#include "htmlHelper.h"

//#if BOARD == ESP8266
//#  include <ESP8266HTTPClient.h>
//#elif BOARD == ESP32
//#  include <HTTPClient.h>
//#else
//#  error "Target hardware BOARD nont defined."
//#endif

// htmlHelper - library of utility functions to aid in parsing headers, query strings, etc.
// 
// most of the helpers can be re-used, however they were created primarily for the programmatic acceptance of terms and conditions
// found on some (Cisco) guest WiFi access points; see doAcceptTermsAndConditions() 
// 
// TODO - finish moving code to the htmlHelper class, pass WiFiClient by reference.
//
// 
// example of good header, ready to connect:
//
//  HTTP/1.1 200 OK
//  Cache-Control: private
//  Content-Length: 448
//  Content-Type: text/html; charset=utf-8
//  Server: Microsoft-IIS/8.0
//  X-AspNet-Version: 4.0.30319
//  X-Powered-By: ASP.NET
//  Set-Cookie: ARRAffinity=e6d4581efad2b0b03441830adabbc7c47de19c54485f1340458712af0124e9f2;Path=/;Domain=gojimmypi-dev-imageconvert2bmp.azurewebsites.net
//  Date: Sat, 03 Dec 2016 01:31:30 GMT
//
//
// example of header on visitor wifi, needing to accept "Terms and Conditions"
//
//  HTTP/1.1 200 OK
//  Location: http://1.1.1.1/fs/customwebauth/login.html?switch_url=http://1.1.1.1/login.html&ap_mac=00:11:22:33:44:55&client_mac=11:22:33:44:55:66&wlan=My%20Visitor%20WiFi&redirect=www.google.com/
//  Content-Type: text/html
//  Content-Length: 474
//
//
//  <HTML><HEAD><TITLE> Web Authentication Redirect</TITLE><META http-equiv="Cache-control" content="no-cache"><META http-equiv="Pragma" content="no-cache"><META http-equiv="Expires" content="-1"><META http-equiv="refresh" content="1; URL=http://1.1.1.1/fs/customwebauth/login.html?switch_url=http://1.1.1.1/login.htmlap_mac=00:11:22:33:44:55&client_mac=11:22:33:44:55:66&wlan=My%20Visitor%20WiFi&redirect=www.google.com/"></HEAD></HTML>


// Use WiFiClient class to create TCP connections
const int myDebugLevel = 3; // 3 is maximum verbose level

unsigned long timeout = millis();

const int ESP_MIN_HEAP = 4096; // we'll monitor operations to ensure we never run out of space (e.g. a massive web page)
int MAX_WEB_RESPONSE = (ESP.getFreeHeap() > 10000 ? ESP.getFreeHeap() - 10000 : 10000);

//const char* accessHost = "1.1.1.1"; // 1.1.1.1 is typically the address of a Cisco WiFi guest Access Point. TODO extract from http response
const char* accessHost = "192.0.2.1";

const char* wifiUserAgent = "User-Agent: Mozilla/5.0 (Windows NT 10.0; WOW64; Trident/7.0; ASTE; rv:11.0) like Gecko";
const char* internetHostCheck = "gojimmypi-dev-imageconvert2bmp.azurewebsites.net"; // some well known, reliable internet url (ideally small html payload)
const char* httpText = "http://";

bool isOutOfMemory;
String htmlString;
String PostData;          // = "buttonClicked=4&redirect_url=www.google.com&action=http://1.1.1.1/login.html";
String Request;           // = "POST /login.html HTTP/1.1\r\n";
String ResponseLocation;
String ResponseContentLocation;
String ResponseFirstLine;
String accessRedirect;    // a value like "/fs/customwebauth/login.html?switch_url=http://1.1.1.1/login.html%26ap_mac=00:11:22:33:44:55%26client_mac=cc:11:22:33:44:55%26wlan=Visitor%20WiFi%26redirect=www.google.com/"
uint ResponseContentLength = -1;
const String CrLf = "\n\r";


const int MAX_CONNECTION_TIMEOUT_MILLISECONDS = 8000;
WIFI_CLIENT_CLASS* myClient = NULL; // note this may be one of several different class types depending on architecture and security setting



int htmlHelper::Send() {
	return htmlSend(thisHost, thisPort, sendHeader); //const char* thisHost, int thisPort, String sendHeader
}

htmlHelper::htmlHelper(WIFI_CLIENT_CLASS* thisClient, const char* Host, int Port) {
	myClient = thisClient;
	thisHost = Host;
	thisPort = Port;
}

// helper with initial HTML header to send
htmlHelper::htmlHelper(WIFI_CLIENT_CLASS* thisClient, const char* Host, int Port, String Header) {
	htmlHelper(thisClient, Host, Port);
	sendHeader = Header;
}

// basic helper
htmlHelper::htmlHelper() {
	// un-initialized htmlHelper declaration
}

bool htmlExists(String targetURL) {
	return false;

	HTTPClient http;
	Serial.print("Calling htmlExists for: ");
	Serial.println(targetURL);
	http.begin(targetURL);

	int httpCode = http.GET();
	//if (!http.connected()) {
	//	Serial.println("HTTPClient not connected.");
	//	return;
	//}
	if (httpCode > 0) {
		// HTTP header has been send and Server response header has been handled
		// file found at server
		return true;
	}
	// http.end(); // TODO - sould we end here?
	return false;
}

String htmlBasicHeaderText(String verb, const char* targetHost, String targetUrl) {
	return verb + " http://" + String(targetHost) + targetUrl + " HTTP/1.1\r\n" +
		"Host: " + String(targetHost) + "\r\n" +
		"Content-Encoding: identity" + "\r\n" +
		"Connection: keep-alive\r\n\r\n"; // TODO, should this be keep-alive ?
}


//**************************************************************************************************************
// return the html query string value in urlString that was specified in keyString
//    exmple:  queryStringValue("?a=1&b=42!","b") should return a value of "42!"
//**************************************************************************************************************
String queryStringValue(String urlString, String keyString) {
	String thisUrl = urlString;
	thisUrl.replace("?", "&");
	thisUrl.replace("/n", "&");

	String tagStart = "&" + keyString + "=";
	int startPosition = urlString.indexOf(tagStart) + tagStart.length();
	int endPosition = urlString.indexOf("&", startPosition + 1);
	if ((startPosition > 0) && (endPosition > 0) && (endPosition > startPosition)) {
		return urlString.substring(startPosition, endPosition);
	}
	else {
		return "";
	}

}

//**************************************************************************************************************
// myMacAddressString return this device's mac address in lowercase hex:  ab:12:cd:34:ef:56
//**************************************************************************************************************
String myMacAddressString() {
	String res;
	char myMac[18] = { 0 };
	for (int i = 0; i < 6; i++) {
		sprintf(&myMac[i * 3], "%02X:", WiFi.macAddress()[i]); // format each number as 2 digit hex, followed by colon.  "12:"
	}
	res = (String)myMac;
	res.toLowerCase();
	return res.substring(0, res.length() - 1); // minus 1 in order to not return the trailing ":"
}

//**************************************************************************************************************
// htmlTagValue - return innerHTML text between <start>and</start>  (in this case, the word: and )
//**************************************************************************************************************
String htmlTagValue(String  thisHTML, String thisTag) {
	String tagStart = "<" + thisTag + ">";
	String tagEnd = "</" + thisTag + ">";
	int startPosition = thisHTML.indexOf(tagStart) + tagStart.length();      // add the length of the tag, as we are not interested in having it in result
																			 //Serial.print("START = ");
																			 //Serial.println(startPosition);
	int endPosition = thisHTML.indexOf(tagEnd);
	String res = thisHTML.substring(startPosition, endPosition);
	res.trim();
	Serial.println("thisTag = [" + res + "]");
	return res;
}


//**************************************************************************************************************
// getHeaderValue - return header value of keyword
//
// html header values are formatted:
//   keyword: value
//
// e.g. Content-Length: 38826
//**************************************************************************************************************
String getHeaderValue(String keyWord, String str) {
	if ((str.length() < 1) || (keyWord.length() < 1)) {
		return "";
	}
	String keyText = keyWord;
	String thisString = str;
	String resultStr = "";
	if (thisString.startsWith("\n")) { thisString = thisString.substring(1); } // we [read until \r] so the \n may come before or after the \r
	if (thisString.startsWith("\r")) { thisString = thisString.substring(1); }
	thisString.trim();

	keyText.trim();
	if (keyText.endsWith(":")) { keyText += " "; } // we need to end with a colon AND a space, so add the space if we have only colon
	if (!keyText.endsWith(": ")) { keyText += ": "; } // add both colopn and space if not provided

													  //Serial.print("Looking at: [" + thisString + "] for keyword: [" + keyText + "]");
	int keyLength = keyText.length();
	if (thisString.startsWith(keyText)) {
		//Serial.print(" Found!");
		resultStr = thisString.substring(keyLength);
	}
	return resultStr;
}

// uint OutValue
void getHeaderValue(String keyWord, String str, uint& OutValue) {
	String resultStr = "";
	int result;
	resultStr = getHeaderValue(keyWord, str);
	if (resultStr != "") {
		//Serial.print(" Found!");
		result = resultStr.toInt();
		OutValue = result;
	}
}

// String OutValue
void getHeaderValue(String keyWord, String str, String& OutValue) {
	String resultStr = "";
	resultStr = getHeaderValue(keyWord, str);
	if (resultStr != "") {
		OutValue = resultStr;
	}
}

//**************************************************************************************************************
//  htmlSend - send thisHeader to targetHost:targetPort
//
//  Note we instantiate our own WiFiClient (or WiFiClientSecure) here.
//
//  return codes:   0 success
//                  1 success, but with redirect
//                  2 failed to connect
//                  3 client timeout (see MAX_CONNECTION_TIMEOUT_MILLISECONDS)
//                  4 out of memory (rare, but perhaps already running low on memory or unusually large header)
//                  5 content too large to load (would be out of memory if content attempted to load)
//                  6 myClient is NULL
//                  7 WiFi not connected
//**************************************************************************************************************
int htmlSend(const char* thisHost, int thisPort, String sendHeader) {
	Serial.println(">>>> htmlSend");
    HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
    HEAP_DEBUG_PRINT("Memory free heap: ");
    HEAP_DEBUG_PRINTLN("Memory free heap: ");
    Serial.println(">>>> SET_HEAP_MESSAGE");
    SET_HEAP_MESSAGE("htmlSend: ");
    HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
    HEAP_DEBUG_PRINT("Memory free heap: ");
    HEAP_DEBUG_PRINTLN("Memory free heap: ");

    Serial.println(">>>> htmlSend end heap test");
    int countReadResponseAttempts = 5; // this a is a somewhat arbitrary number, mainly to handle large HTML payloads
    String thisResponse; thisResponse = "";
    String thisResponseHeader; thisResponseHeader = "";

    Serial.printf("Connection status: %d\n", WiFi.status()); // TODO abort if no WiFi

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Error: htmlSend called when not connected to WiFi!");
        return 7;
    }

    if (myClient == NULL) {
		Serial.println("Error: myClient not initialized!");
		return 6;
	}

	Serial.print("client status = ");
	Serial.println(myClient->status());
	Serial.print("client available = ");
	Serial.println(myClient->available());
	Serial.print("client getLastSSLError = ");
	Serial.println(myClient->getLastSSLError());
	Serial.print("client getWriteError = ");
	Serial.println(myClient->getWriteError());
	Serial.print("client getWriteError = ");

    yield();
    Serial.println("Connecting to port 80");
	myClient->setInsecure();
	if (!myClient->connect(thisHost, 80)) {
        Serial.println("SUCCESS! Connecting to port 80");
    }
    else {
        Serial.println("FAILED! Connecting to port 80");
    }

    yield();
	if (thisPort != 80) {
		Serial.println("Target is not port 80; stopAll...");
		myClient->stopAll();
		delay(20);
	}
    HTTP_DEBUG_PRINTLN(DEBUG_SEPARATOR);
    HTTP_DEBUG_PRINT("Connecting to ");
    HTTP_DEBUG_PRINT(thisHost); // e.g. www.google.com, no http, no path, just dns name
    HTTP_DEBUG_PRINT("; port ");
    HTTP_DEBUG_PRINTLN(thisPort);
    HTTP_DEBUG_PRINTLN(DEBUG_SEPARATOR);
    
	myClient->setInsecure();
	if (!myClient->connect(thisHost, thisPort)) {
		Serial.print(thisHost); Serial.print(":"); Serial.print(thisPort);
		Serial.println(" htmlSend connection failed, flushing and trying htmlSendPlainText...");
		myClient->flush();
		myClient->stopAll();
		return htmlSendPlainText(thisHost, sendHeader);
	}
   
#ifdef HTTP_DEBUG
	Serial.println("Sending HTML: ");
	Serial.println(DEBUG_SEPARATOR);
	Serial.println(sendHeader);
	Serial.println(DEBUG_SEPARATOR);
#endif


	isOutOfMemory = (ESP.getFreeHeap() < ESP_MIN_HEAP); // we never want to read a header so big that it exceeds available memory
	if (isOutOfMemory) {
		Serial.println("Starting out of memory!");
	}
	else {
		HEAP_DEBUG_PRINT("Memory free heap: ");	HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
	}

	// BEGIN TIME SENSITIVE SECTION (edit with care, don't waste CPU, yield to OS!)
	myClient->flush(); // discard any incoming data
	yield();
	myClient->print(sendHeader); //blocks until either data is sent and ACKed, or timeout occurs (currently hard-coded to 5 seconds). 
	timeout = millis();
	while (myClient->available() == 0) {
		yield(); // give the OS a little breathing room 
		if ((millis() - timeout) > MAX_CONNECTION_TIMEOUT_MILLISECONDS) {
			Serial.println(">>> Client Timeout !");
			myClient->flush();
			myClient->stop();
			return 3;
		}
		delay(10); // TODO does delay offer any benefit over yield() ?
	}

	bool endOfHeader = false; // we'll keep track of where the html header ends, and where the payload begins
	int thisLineCount = 0;
	String line = "";
	ResponseFirstLine = "";
	while (countReadResponseAttempts > 0) {
		// Read all the lines of the reply from server and print them to Serial
		while (myClient->available()) {
			delay(1);
			// we'll always incrementally read header lines, but we may not read content if it is too large.
			if ((!endOfHeader) || (ResponseContentLength < (ESP.getFreeHeap() - ESP_MIN_HEAP))) {
				line = myClient->readStringUntil('\r'); // note that the char AFTER this is often a \n - but the documentation says to read until \r
			}
			else {
				Serial.println("Content too large! Flushing client to discard Rx data...");
				myClient->flush();
				myClient->stop();
				return 5; // abort, we cannot load this content
			}
			if ((line != "") && (ResponseFirstLine == "")) {
				ResponseFirstLine = line;
			}

			if (line.startsWith("Location: ")) {
				ResponseLocation = line.substring(10);
			}
			if (line.startsWith("\nLocation: ")) { // we [read until \r] so the \n may come before or after the \r
				ResponseLocation = line.substring(11);
			}

			yield(); // give the OS a little breathing room when loading large documents
			if (ESP.getFreeHeap() > ESP_MIN_HEAP) {
				Serial.print(".");
				if (!endOfHeader) {
					// we only look for these values in the header, never the content!
					getHeaderValue("Content-Location", line, ResponseContentLocation);
					getHeaderValue("Content-Length", line, ResponseContentLength);
					thisResponseHeader += line; // we always capture the full header, as it has interesting fields (e.g. length of content which might exceed our memory capacity!)
				}
				if (endOfHeader) { thisResponse += line; } // always capture the response once we reach the end of the header

				if (line.length() <= 1) { // the first blank line found signifies the separation between header and content
					endOfHeader = true;
				}
			}
			else {
				Serial.print("!");
				thisResponse += CrLf + "Out of memory! " + CrLf;
				Serial.print("Out of memory! Heap=");
				HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
				isOutOfMemory = true;
				myClient->flush();
				return 4;
			}

			thisLineCount++;
		}
		//Serial.print("(");
		//Serial.print(countReadResponseAttempts);
		//Serial.print(")");
		countReadResponseAttempts--; // TODO - do we stil need to look for more data like this?
									 // Serial.println("Next attempt!");
		delay(100); // give the OS a little breathing room when loading large documents
	}
	// END TIME SENSITIVE SECTION 
#ifdef HTTP_DEBUG
#endif
	Serial.println("");
	Serial.print("First Response Line = ");
	Serial.println(ResponseFirstLine);
	Serial.print("Found Response Content Length = ");
	Serial.println(ResponseContentLength);
	Serial.print("Found Response Content Location = ");
	Serial.println(ResponseContentLocation);
	//
	// take ResponseLocation that looks like http://1.1.1.1/fs/customwebauth/login.html?switch_url=http://1.1.1.1/login.html&ap_mac=00:11:22:33:44:55&client_mac=cc:11:22:33:44:55&wlan=Visitor%20WiFi&redirect=www.w3.org/
	// then strip the http & host name, and urlencode for the accessRedirect
	//
	Serial.println("accessRedirect debug");
	if (ResponseLocation.length() > 138) { // make sure we have a minimum length REsponse Location string
		Serial.println("ResponseLocation = " + ResponseLocation);
		accessRedirect = ResponseLocation;  // should contain full URL:  http://1.1.1.1/path?switchurl=... but we only want path?switchurl=...
		Serial.println("accessRedirect = " + ResponseLocation);
		accessRedirect.remove(0, 16);       // remove the first 14 characters that should be: http://1.1.1.1 TODO handle when different (longer?)
		accessRedirect.replace("&", "%26"); // urlencode the ampersands
		Serial.println("accessRedirect = " + ResponseLocation);
	}
	else {
		// if there's no Location: value in header, we are likely not needing a redirect on the visitor WiFi
		//Serial.println("Warning accessRedirect too short.");
	}


#ifdef HTTP_DEBUG
	Serial.println("");
	Serial.println("Response Header:");
	Serial.println(DEBUG_SEPARATOR);
	Serial.println(thisResponseHeader);
	Serial.println(DEBUG_SEPARATOR);
	Serial.println("");
	if (myDebugLevel >= 2) { // only show the response content for debug level 2 or greater
		Serial.println("Response Payload Content:");
		Serial.println(DEBUG_SEPARATOR);
		// Serial.println(thisResponse);
		Serial.println(DEBUG_SEPARATOR);
		Serial.println("");
	}
	//Serial.println("Page Title=" + htmlTagValue(thisResponse, "TITLE") );
	Serial.println("");
	Serial.print("Read done! Additional Read Responses:");
	Serial.println(countReadResponseAttempts);
	Serial.print("*** End of response. ");
	Serial.print(thisLineCount);
	Serial.println(" lines. ");
	Serial.println("");
	//Serial.print("thisResponse.indexOf(Web Authentication Redirect)");
	//Serial.println(thisResponse.indexOf("Web Authentication Redirect"));
	//Serial.println("");
	Serial.print("thisResponseHeader.indexOf(Location: http://192.0.2.1/)");
	Serial.println(thisResponseHeader.indexOf("Location: http://192.0.2.1/"));
	Serial.println("");
	Serial.print("htmlTagValue(thisResponse, TITLE) = ");
	Serial.println(htmlTagValue(thisResponse,"TITLE"));
	Serial.println("");


	//Serial.print("my MAC = ");
	//Serial.println(myMacAddressString());

	//Serial.println("Changing MAC...");
	//uint8_t  mac[] = { 0x77, 0x01, 0x02, 0x03, 0x04, 0x05 };
	//wifi_set_macaddr(STATION_IF, &mac[0]);
	//Serial.println("MAC Change Done!");

	//Serial.print("my MAC = ");
	//Serial.println(myMacAddressString());
#endif

	if (
		(thisResponseHeader.indexOf("Location: http://192.0.2.1/") > 0) // anytime the 1.1.1.1 address is in the header, we are doing a redirect
		||
		(htmlTagValue(thisResponse, "TITLE") == "Web Authentication Redirect") // anytime this text is found title tag, we are doing a redirect
		)
	{
		Serial.println("REDIRECT!!");
		return 1; // success, but with redirect
	}
	else {
		return 0; // success! Internet access ready.
	}

//#ifdef ARDUINO_ARCH_ESP8266
//	client.stopAll(); // flush client (only ESP8266 seems to have implemented stopAll)
//#endif
//
//#ifdef ARDUINO_ARCH_ESP32
//	client.stop(); // flush client (the ESP32 does not seem to have implemented stopAll)
//#endif

}
int htmlSend(WIFI_CLIENT_CLASS* thisClient, const char* thisHost, int thisPort) {
	myClient = thisClient;
	return htmlSend(thisHost, thisPort,"");
}
int htmlSend(WIFI_CLIENT_CLASS* thisClient, const char* thisHost, int thisPort, String sendHeader) {
	myClient = thisClient;
	return htmlSend(thisHost, thisPort, sendHeader);
}


int htmlSendPlainText(const char* thisHost, String sendHeader) {
	HTTP_DEBUG_PRINTLN("");
	HTTP_DEBUG_PRINTLN(">>>> Start htmlSendPlainText");
	HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
	int thisPort = 80;

	WiFiClient OtherClient; // note regardless of default client, we create another plain text, non-secure client here
	HEAP_DEBUG_PRINT("Memory free heap after OtherClient: ");	HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);

    Serial.println(">>>> htmlSendPlainText");

	int countReadResponseAttempts = 5; // this a is a somewhat arbitrary number, mainly to handle large HTML payloads
	String thisResponse; thisResponse = "";
	String thisResponseHeader; thisResponseHeader = "";

#ifdef HTTP_DEBUG
	Serial.println(DEBUG_SEPARATOR);
	Serial.print("Connecting to ");
	Serial.print(thisHost); // e.g. www.google.com, no http, no path, just dns name
	Serial.print("; port ");
	Serial.println(thisPort);
	Serial.println(DEBUG_SEPARATOR);
#endif


	if (!OtherClient.connect(thisHost, thisPort)) {
#ifdef HTTP_DEBUG
		Serial.println("htmlSend OtherClient connection failed");
#endif
		return 2;
	}

#ifdef HTTP_DEBUG
	Serial.println("Sending OtherClient HTML: ");
	Serial.println(DEBUG_SEPARATOR);
	Serial.println(sendHeader);
	Serial.println(DEBUG_SEPARATOR);
#endif


	isOutOfMemory = (ESP.getFreeHeap() < ESP_MIN_HEAP); // we never want to read a header so big that it exceeds available memory
	if (isOutOfMemory) {
		Serial.println("Starting out of memory!");
	}
	else {
		HEAP_DEBUG_PRINT("Memory free heap: ");	HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
	}

	// BEGIN TIME SENSITIVE SECTION (edit with care, don't waste CPU, yield to OS!)
	OtherClient.flush(); // discard any incoming data
	yield();
	OtherClient.print(sendHeader); //blocks until either data is sent and ACKed, or timeout occurs (currently hard-coded to 5 seconds). 
	timeout = millis();
	while (OtherClient.available() == 0) {
		yield(); // give the OS a little breathing room 
		if ((millis() - timeout) > MAX_CONNECTION_TIMEOUT_MILLISECONDS) {
			Serial.println(">>> PlainText Client Timeout !");
			OtherClient.flush();
			OtherClient.stop();
			return 3;
		}
		delay(10); // TODO does delay offer any benefit over yield() ?
	}

	bool endOfHeader = false; // we'll keep track of where the html header ends, and where the payload begins
	int thisLineCount = 0;
	String line = "";
	ResponseFirstLine = "";
	while (countReadResponseAttempts > 0) {
		// Read all the lines of the reply from server and print them to Serial
		while (OtherClient.available()) {
			delay(1);
			// we'll always incrementally read header lines, but we may not read content if it is too large.
			if ((!endOfHeader) || (ResponseContentLength < (ESP.getFreeHeap() - ESP_MIN_HEAP))) {
				line = OtherClient.readStringUntil('\r'); // note that the char AFTER this is often a \n - but the documentation says to read until \r
			}
			else {
				Serial.println("Content too large! Flushing client to discard Rx data...");
				OtherClient.flush();
				OtherClient.stop();
				return 5; // abort, we cannot load this content
			}
			if ((line != "") && (ResponseFirstLine == "")) {
				ResponseFirstLine = line;
			}

			if (line.startsWith("Location: ")) {
				ResponseLocation = line.substring(10);
			}
			if (line.startsWith("\nLocation: ")) { // we [read until \r] so the \n may come before or after the \r
				ResponseLocation = line.substring(11);
			}

			yield(); // give the OS a little breathing room when loading large documents
			if (ESP.getFreeHeap() > ESP_MIN_HEAP) {
				Serial.print(".");
				if (!endOfHeader) {
					// we only look for these values in the header, never the content!
					getHeaderValue("Content-Location", line, ResponseContentLocation);
					getHeaderValue("Content-Length", line, ResponseContentLength);
					thisResponseHeader += line; // we always capture the full header, as it has interesting fields (e.g. length of content which might exceed our memory capacity!)
				}
				if (endOfHeader) { thisResponse += line; } // always capture the response once we reach the end of the header

				if (line.length() <= 1) { // the first blank line found signifies the separation between header and content
					endOfHeader = true;
				}
			}
			else {
				Serial.print("!");
				thisResponse += CrLf + "Out of memory! " + CrLf;
				Serial.print("Out of memory! Heap=");
				HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
				isOutOfMemory = true;
				OtherClient.flush();
				return 4;
			}

			thisLineCount++;
		}
		//Serial.print("(");
		//Serial.print(countReadResponseAttempts);
		//Serial.print(")");
		countReadResponseAttempts--; // TODO - do we stil need to look for more data like this?
									 // Serial.println("Next attempt!");
		delay(100); // give the OS a little breathing room when loading large documents
	}
	// END TIME SENSITIVE SECTION 
#ifdef HTTP_DEBUG
#endif
	Serial.println("");
	Serial.print("First Response Line = ");
	Serial.println(ResponseFirstLine);
	Serial.print("Found Response Content Length = ");
	Serial.println(ResponseContentLength);
	Serial.print("Found Response Content Location = ");
	Serial.println(ResponseContentLocation);
	//
	// take ResponseLocation that looks like http://1.1.1.1/fs/customwebauth/login.html?switch_url=http://1.1.1.1/login.html&ap_mac=00:11:22:33:44:55&client_mac=cc:11:22:33:44:55&wlan=Visitor%20WiFi&redirect=www.w3.org/
	// then strip the http & host name, and urlencode for the accessRedirect
	//
	Serial.println("accessRedirect debug");
	if (ResponseLocation.length() > 138) { // make sure we have a minimum length REsponse Location string
		Serial.println("ResponseLocation = " + ResponseLocation);
		accessRedirect = ResponseLocation;  // should contain full URL:  http://1.1.1.1/path?switchurl=... but we only want path?switchurl=...
		Serial.println("accessRedirect = " + accessRedirect);
		//accessRedirect.remove(0, 14);       // remove the first 14 characters that should be: http://1.1.1.1 TODO handle when different (longer?)
		accessRedirect.remove(0, 16);       // remove the first 14 characters that should be: http://192.0.2.1 TODO handle when different (longer?)
		accessRedirect.replace("&", "%26"); // urlencode the ampersands
		Serial.println("accessRedirect = " + accessRedirect);
		//                                   http://192.0.2.1/fs/customwebauth/login.html?switch_url=http://192.0.2.1/login.html&ap_mac=00:3a:7d:13:aa:80&client_mac=18:fe:34:d7:7c:3b&wlan=Visitor%20WiFi&redirect=www.w3.org/
	}
	else {
		// if there's no Location: value in header, we are likely not needing a redirect on the visitor WiFi
		//Serial.println("Warning accessRedirect too short.");
	}


#ifdef HTTP_DEBUG
	Serial.println("");
	Serial.println("Response Header:");
	Serial.println(DEBUG_SEPARATOR);
	Serial.println(thisResponseHeader);
	Serial.println(DEBUG_SEPARATOR);
	Serial.println("");
	if (myDebugLevel >= 2) { // only show the response content for debug level 2 or greater
		Serial.println("Response Payload Content:");
		Serial.println(DEBUG_SEPARATOR);
		Serial.println(thisResponse);
		Serial.println(DEBUG_SEPARATOR);
		Serial.println("");
	}
	//Serial.println("Page Title=" + htmlTagValue(thisResponse, "TITLE") );
	Serial.println("");
	Serial.print("Read done! Additional Read Responses:");
	Serial.println(countReadResponseAttempts);
	Serial.print("*** End of response. ");
	Serial.print(thisLineCount);
	Serial.println(" lines. ");
	Serial.println("");
	//Serial.print("thisResponse.indexOf(Web Authentication Redirect)");
	//Serial.println(thisResponse.indexOf("Web Authentication Redirect"));
	//Serial.println("");
	Serial.print("thisResponseHeader.indexOf(Location: http://192.0.2.1/)");
	Serial.println(thisResponseHeader.indexOf("Location: http://192.0.2.1/"));
	Serial.println("");
	Serial.print("htmlTagValue(thisResponse, TITLE) = ");
	Serial.println(htmlTagValue(thisResponse, "TITLE"));
	Serial.println("");


	//Serial.print("my MAC = ");
	//Serial.println(myMacAddressString());

	//Serial.println("Changing MAC...");
	//uint8_t  mac[] = { 0x77, 0x01, 0x02, 0x03, 0x04, 0x05 };
	//wifi_set_macaddr(STATION_IF, &mac[0]);
	//Serial.println("MAC Change Done!");

	//Serial.print("my MAC = ");
	//Serial.println(myMacAddressString());
#endif

	if (
		(thisResponseHeader.indexOf("Location: http://192.0.2.1/") > 0) // anytime the 1.1.1.1 address is in the header, we are doing a redirect
		||
		(htmlTagValue(thisResponse, "TITLE") == "Web Authentication Redirect") // anytime this text is found title tag, we are doing a redirect
		)
	{
		Serial.println("REDIRECT!!");
		return 1; // success, but with redirect
	}
	else {
		Serial.println("CONNECTED!!");
		return 0; // success! Internet access ready.
	}

	//#ifdef ARDUINO_ARCH_ESP8266
	//	client.stopAll(); // flush client (only ESP8266 seems to have implemented stopAll)
	//#endif
	//
	//#ifdef ARDUINO_ARCH_ESP32
	//	client.stop(); // flush client (the ESP32 does not seem to have implemented stopAll)
	//#endif

}



void htmlSetClient(WIFI_CLIENT_CLASS* thisClient) {
	myClient = thisClient;
}

int doAcceptTermsAndConditions() {
	//**************************************************************************************************************
	// when we make the original request, if we've not pressed the "I accept the terms" button yet, 
	// we'll get a page reponse like this when visiting a web page:
	//
	// HTTP/1.1 200 OK
	// Location: http://1.1.1.1/fs/customwebauth/login.html?switch_url=http://1.1.1.1/login.html&ap_mac=00:11:22:33:44:55&client_mac=11:22:33:44:55:66&wlan=My%20Visitor%20WiFi&redirect=www.google.com/
	// Content-Type: text/html
	// Content-Length: 440
	//
	// <HTML><HEAD><TITLE> Web Authentication Redirect</TITLE>
	// <META http-equiv="Cache-control" content="no-cache">
	// <META http-equiv="Pragma" content="no-cache">
	// <META http-equiv="Expires" content="-1">
	// <META http-equiv="refresh" content="1; URL=http://1.1.1.1/fs/customwebauth/login.html?switch_url=http://1.1.1.1/login.htmlap_mac=00:11:22:33:44:55&client_mac=11:22:33:44:55:66&wlan=My%20Visitor%20WiFi&redirect=www.google.com/">
	// </HEAD></HTML>
	// 

	// <HTML><HEAD><TITLE> Web Authentication Redirect< / TITLE>
	// <META http-equiv="Cache-control" content = "no-cache">
	// <META http-equiv="Pragma" content = "no-cache">
	// <META http-equiv="Expires" content = "-1">
	// <META http-equiv="refresh" content = "1; URL=http://192.0.2.1/fs/customwebauth/login.html?switch_url=http://192.0.2.1/login.html&ap_mac=00:11:22:33:44:55&client_mac=11:22:33:44:55:66&wlan=My%20Visitor%20WiFi&redirect=www.w3.org/">< / HEAD>< / HTML>
	// assumes the access point is called My Visitor Wifi, and we are trying to connect to google.com
	//
	// in particular, note the META refresh redirector.  TODO: programmatically extract the META redirect path
	//**************************************************************************************************************


	//**************************************************************************************************************
	// fetch the custom.js code; this is the interesting stuff that handles the web page button pressing
	// (this is what we need to emulate here, with no button and no button presser!)
	//
	// the script e3ssentially does this at load time:
	//
	//   document.forms[0].action = args.switch_url;
	//
	// then when the user presses the accept button, it does this:
	//
	//   redirectUrl = redirectUrl.substring(0,255);
	//   document.forms[0].redirect_url.value = redirectUrl;
	//   document.forms[0].buttonClicked.value = 4;
	//   document.forms[0].submit();
	//
	// 
	//**************************************************************************************************************

	//**************************************************************************************************************
	// fetch the custom.js
	// (this section is for reference only, and not needed to actually connect; there might be "interesting" data)
	//**************************************************************************************************************
	Serial.println("\r\n");
	Serial.println("Requesting custom.js: \r\n");
	//**************************************************************************************************************
	htmlString = "";
	htmlString += String("GET ") + "/fs/customwebauth/custom.js" + " HTTP/1.1\r\n";
	htmlString += "Host: " + String(accessHost) + "\r\n";
	htmlString += "Accept-Language: en-US\r\n";
	htmlString += String(WIFI_USER_AGENT) + "\r\n"; // "User-Agent: Mozilla/5.0 (Windows NT 10.0; WOW64; Trident/7.0; ASTE; rv:11.0) like Gecko\r\n";
	htmlString += "Connection: Keep-Alive\r\n\r\n";

	htmlSend(accessHost, 80, htmlString);


	//**************************************************************************************************************
	// we are pretending to be a browser, but we're not; so we need to manually redirect to the WiFi Access 
	// redirect, found in the META tag, above. (note we use accessRedirect, extracted from  ResponseLocation)
	//**************************************************************************************************************
	Serial.println("\r\n");
	Serial.println("Requesting URL redirect: \r\n");
	//**************************************************************************************************************
	htmlString = "";

	// GET /fs/customwebauth/login.html?switch_url=http://1.1.1.1/login.html%26ap_mac=00:11:22:33:44:55%26client_mac=cc:11:22:33:44:55%26wlan=Visitor%20WiFi%26redirect=www.google.com/ HTTP/1.1
	// htmlString += String("GET ") + String(wifiAccessRedirect) + " HTTP/1.1\r\n"; // TODO get the redirect from the prior http response
	htmlString += String("GET ") + accessRedirect + " HTTP/1.1\r\n";

	htmlString += "Host: " + String(accessHost) + "\r\n";
	htmlString += "Connection: Keep-Alive\r\n";
	htmlString += "\r\n"; // blank line before content

	htmlSend(accessHost, 80, htmlString);

	delay(5000); // here we are spending 5 seconds reading the terms and conditions.
	//
	//**************************************************************************************************************

	//**************************************************************************************************************
	// The post back (note it is actually a GET, with query-string parameters; thanks fiddler app for this!)
	//**************************************************************************************************************
	// 
	PostData = "buttonClicked=4&redirect_url=" + String(internetHostCheck);
	Request = ""; // init request

				  // note in the JavaScript to submit form, there's a line:  document.forms[0].buttonClicked.value = 4;
	Request += "GET http://" + String(accessHost) + "/login.html?buttonClicked=4&redirect_url=" + String(internetHostCheck) + " HTTP/1.1\r\n";
	Request += "Accept: text/html, application/xhtml+xml, */*\r\n";


	// Request += "Referer: http://" + String(accessHost) + "/fs/customwebauth/login.html?switch_url=http://1.1.1.1/login.html&ap_mac=00:11:22:33:44:55&client_mac=cc:11:22:33:44:55&wlan=Visitor%20WiFi&redirect=" + String(internetHostCheck) + "\r\n";
	Request += "Referer: " + ResponseLocation + "\r\n";

#ifdef HTTP_DEBUG
#endif
	Serial.println("Header so far:\n\r:");
	Serial.println(Request);
	Serial.println("\n\r\n\r\n\r");
	Serial.println("accessRedirect:");
	Serial.println(accessRedirect);
	Serial.println("\n\r\n\r\n\r");
	Serial.println("\n\r\n\r\n\r");


	Request += "Accept-Language: en-US\r\n";
	Request += "User-Agent: Mozilla/5.0 (Windows NT 10.0; WOW64; Trident/7.0; ASTE; rv:11.0) like Gecko\r\n";
	Request += "DNT: 1\r\n";
	Request += "If-Modified-Since: Mon 31 Oct 2016 23:28:25 GMT\r\n";
	Request += "Content-Length: ";
	Request += PostData.length(); // we need to say how long our posted data is
	Request += "\n";              // linefeed after content length; not a blank line!
	Request += "Host: " + String(accessHost) + "\r\n";
	Request += "Connection: Keep-Alive\r\n";
	Request += "\r\n"; // this is the blank line separating the header from the postback content
	Request += PostData;

	//Serial.print("Debug Halt! Waiting");
	//while (1) {
	//	Serial.print(".");
	//	delay(90000000);
	//}

	// send the response, accepting the terms and conditions. the result should be we are granted access!
	//htmlSend(accessHost, 80, Request);


	Serial.println("\r\n");

	//Request += "Connection: close\r\n";

	htmlSend(accessHost, 80, Request);

	//Serial.println("\r\n");



	//**************************************************************************************************************
	// we should now have full WiFi connectivity, and able to browse anywhere the access point allows.
	//
	// for reference, a Windows-based fiddler session shows a header like this:
	//
	//  GET http://google.com/ HTTP/1.1
	//  Host: google.com
	//  Connection: keep-alive
	//  Upgrade-Insecure-Requests: 1
	//  User-Agent: Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/54.0.2840.71 Safari/537.36
	//  X-Client-Data: CIS...etc
	//  Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8
	//  Accept-Encoding: gzip, deflate, sdch
	//  Accept-Language: en-US,en;q=0.8
	//  Cookie: OGPC=506...etc
	//
	//  "A server MUST NOT send transfer-codings to an HTTP/1.0 client. "  See http://www.w3.org/Protocols/rfc2616/rfc2616-sec3.html
	//  (however note that even though we may request 1.0, a HTTP/1.1 response comes back. TODO: why?)
	//  why would we do this? An attempt to avoid Chunked Transfer Encoding. TODO: how to fetch Chunked Transfer Encoded content
	//**************************************************************************************************************
	return 0;
}

//**************************************************************************************************************
//  confirmedInternetConnectivity - return 0 if we are able to connect to host and retrieve data
//
//  note the added complexity of poetentially being redirected to the WiFi Visitor Access, needing
//  to programatically accept the terms and conditions.
//**************************************************************************************************************
int confirmedInternetConnectivity(const char* host) {
	// This will send the request to the server
	setHeapMsg ("start confirmedInternetConnectivity: ");
	HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
	String htmlString;
//	const char* internetHostCheck = "gojimmypi-dev-imageconvert2bmp.azurewebsites.net"; // some well known, reliable internet url (ideally small html payload)

																						//htmlString = String("GET http://") + String(internetHostCheck) + url + " HTTP/1.1\r\n" +
																						// "Host: " + String(internetHostCheck) + "\r\n" +
																						// "Content-Encoding: identity" + "\r\n" +
																						// "Connection: Keep-Alive\r\n\r\n";
																						//Serial.println("htmlString:");
																						//Serial.println(htmlString);

	htmlString = htmlBasicHeaderText("GET", host, "/");
#ifdef HTTP_DEBUG
	Serial.println("");
	Serial.println(DEBUG_SEPARATOR);
	Serial.println("confirmedInternetConnectivity() - htmlString:");
	Serial.println(DEBUG_SEPARATOR);
	Serial.println(htmlString);
	Serial.println(DEBUG_SEPARATOR);
#endif


#ifdef USE_TLS_SSL
	HEAP_DEBUG_PRINT("1. "); HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
	int connectionStatus = htmlSend(host, 443, htmlString);
	HEAP_DEBUG_PRINT("2. "); HEAP_DEBUG_PRINTLN(DEFAULT_DEBUG_MESSAGE);
#else
	int connectionStatus = htmlSend(host, 80, htmlString);
#endif
	if (connectionStatus == 0) {
        Serial.println("\r\n Connected to internet! \r\n");
    }
	else if (connectionStatus == 1) {
		Serial.println("Accepting Terms and Conditions...");

		//	  Serial.print("ResponseLocation=");
		//	  Serial.println(ResponseLocation);

		// Serial.println();

		//	  Serial.print("ap_mac queryStringValue=");
		//	  Serial.print(queryStringValue(ResponseLocation, "ap_mac"));

		doAcceptTermsAndConditions();
		connectionStatus = htmlSend(host, 80, htmlString);
		if (connectionStatus != 0) {
			Serial.println("Error: Unable to accept terms and conditions!");
			return 1;
		}
	}
	else {
		Serial.println("Error connecting."); Serial.print("Status = "); Serial.println(connectionStatus);
		return 1;
	}
	Serial.println("confirmedInternetConnectivity - success!");
	return 0;
};
