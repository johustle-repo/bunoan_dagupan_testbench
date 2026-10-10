#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <math.h>
#include "dashboard_assets.h"
#include "Telemetry.h"

// Simulation firmware: no GPIO inputs or physical testbench outputs are used.
const char* AP_SSID = "ECU-Testbench-C6";
const char* AP_PASSWORD = "ECUdemo2026";
WebServer server(80);
#include "LiveMonitor.h"
DNSServer dnsServer;
bool portalReady = false;
String currentScenario = "drive";
uint32_t scenarioStart = 0;
bool dashboardReady = false;
float limit(float value,float low,float high){return fmaxf(low,fminf(high,value));}
Telemetry generate(float t,const String& mode){
 Telemetry d;
 if(mode=="idle"){d.rpm=1200+sinf(t*2)*55;d.ect=60+sinf(t/20)*3;return d;}
 d.speed=mode=="cruise"?85+sinf(t/5)*2:mode=="fault"?55+sinf(t/3)*3:powf((sinf(t/9)+1)/2,1.5)*145;
 d.rear=d.speed;float load=limit(d.speed/160,0,1);
 d.rpm=1200+load*9500+sinf(t)*120;d.ect=62+load*22;d.iat=32+load*8;d.map=25+load*70;d.tps=.6+load*2.9;
 d.o2=.6+sinf(t*2)*.22;d.afr=14.7+sinf(t*2)*.6;d.battery=13.8+sinf(t/3)*.1;
 d.winker=((int)(t/8)%4==0)&&((int)(t*2)%2==0);
 if(mode=="fault"){d.ect=99;d.battery=10.8;d.ref2=3.2;d.check=true;d.abs=true;d.tcs=true;d.rear=d.speed+12;d.afr=17.3;}
 return d;
}
bool numberArg(const char* name,float& target,float minValue,float maxValue){
 if(!server.hasArg(name))return true;
 String input=server.arg(name);char* end=nullptr;const char* start=input.c_str();double value=strtod(start,&end);
 float parsed=(float)value;
 if(end==start||*end!='\0'||!isfinite(parsed)||parsed<minValue||parsed>maxValue)return false;
 target=parsed;return true;
}
bool boolArg(const char* name,bool& target){
 if(!server.hasArg(name))return true;String input=server.arg(name);
 if(input!="0"&&input!="1")return false;target=input=="1";return true;
}
void telemetry(){
 String mode=server.hasArg("scenario")?server.arg("scenario"):"drive";
 if(mode!="drive"&&mode!="idle"&&mode!="cruise"&&mode!="fault"&&mode!="manual"){server.send(400,"application/json","{\"error\":\"Invalid scenario\"}");return;}
 Telemetry d;
 if(mode=="manual"){
 bool valid=numberArg("speed",d.speed,0,250)&&numberArg("rpm",d.rpm,0,15000)&&numberArg("rear",d.rear,0,250)&&numberArg("ect",d.ect,20,100)&&numberArg("iat",d.iat,30,50)&&numberArg("map",d.map,20,101.3)&&numberArg("tps",d.tps,.6,3.5)&&numberArg("o2",d.o2,0,1.2)&&numberArg("afr",d.afr,10.28,17.65)&&numberArg("battery",d.battery,0,30)&&numberArg("ref1",d.ref1,0,30)&&numberArg("ref2",d.ref2,0,30)&&boolArg("key",d.key)&&boolArg("winker",d.winker)&&boolArg("check",d.check)&&boolArg("abs",d.abs)&&boolArg("tcs",d.tcs);
 if(!valid){server.send(400,"application/json","{\"error\":\"Invalid manual value\"}");return;}
 }
 if(mode!=currentScenario){currentScenario=mode;scenarioStart=millis();}
 if(mode!="manual")d=generate((uint32_t)(millis()-scenarioStart)/1000.0f,mode);
 char json[640];
 snprintf(json,sizeof(json),"{\"speed\":%.3f,\"rpm\":%.3f,\"rear\":%.3f,\"ect\":%.3f,\"iat\":%.3f,\"map\":%.3f,\"tps\":%.3f,\"o2\":%.3f,\"afr\":%.3f,\"battery\":%.3f,\"ref1\":%.3f,\"ref2\":%.3f,\"key\":%s,\"winker\":%s,\"check\":%s,\"abs\":%s,\"tcs\":%s}",d.speed,d.rpm,d.rear,d.ect,d.iat,d.map,d.tps,d.o2,d.afr,d.battery,d.ref1,d.ref2,d.key?"true":"false",d.winker?"true":"false",d.check?"true":"false",d.abs?"true":"false",d.tcs?"true":"false");
 server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",json);
}
void portalRedirect(){
 server.sendHeader("Cache-Control","no-store");
 server.sendHeader("Location","http://192.168.4.1/",true);
 server.send(302,"text/plain","Open the ECU dashboard at http://192.168.4.1/");
}
void setup(){
 Serial.begin(115200);WiFi.mode(WIFI_AP);
 if(!WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0))||!WiFi.softAP(AP_SSID,AP_PASSWORD,1,false,4)){Serial.println("Failed to start Wi-Fi access point");return;}
 portalReady=dnsServer.start(53,"*",WiFi.softAPIP());
 Serial.println("ECU dashboard simulation ready");Serial.print("Wi-Fi: ");Serial.println(AP_SSID);Serial.print("Dashboard: http://");Serial.println(WiFi.softAPIP());
 server.on("/",HTTP_GET,[]{server.send_P(200,"text/html; charset=utf-8",DASHBOARD_HTML);});
 server.on("/style.css",HTTP_GET,[]{server.send_P(200,"text/css; charset=utf-8",DASHBOARD_CSS);});
 server.on("/app.js",HTTP_GET,[]{server.send_P(200,"application/javascript; charset=utf-8",DASHBOARD_JS);});
 server.on("/api/telemetry",HTTP_GET,telemetry);
 server.on("/api/live",HTTP_GET,liveTelemetry);
 server.on("/api/monitor",HTTP_POST,liveControl);
 server.on("/api/reset",HTTP_POST,[]{scenarioStart=millis();currentScenario="drive";server.send(200,"application/json","{\"ok\":true}");});
 const char* probes[]={"/generate_204","/gen_204","/hotspot-detect.html","/library/test/success.html","/connecttest.txt","/ncsi.txt","/redirect","/canonical.html","/success.txt"};
 for(const char* path:probes)server.on(path,HTTP_GET,portalRedirect);
 server.onNotFound([]{if(server.uri().startsWith("/api/")){server.send(404,"application/json","{\"error\":\"Unknown API\"}");return;}portalRedirect();});scenarioStart=millis();server.begin();dashboardReady=true;
}
void loop(){
 server.handleClient();
 updateLive();
 if(Serial.available()&&Serial.read()=='s'){
 Serial.print(dashboardReady?"DASHBOARD_READY ssid=":"DASHBOARD_FAILED ssid=");Serial.print(AP_SSID);Serial.print(" ip=");Serial.print(WiFi.softAPIP());Serial.print(" clients=");Serial.print(WiFi.softAPgetStationNum());Serial.print(" portal=");Serial.println(portalReady?"ready":"failed");
 }
 delay(2);
}

