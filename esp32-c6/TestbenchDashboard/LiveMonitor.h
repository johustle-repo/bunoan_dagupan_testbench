#pragma once
// Input-only monitor. Electrical conditioning must be verified separately.
const uint8_t analogPins[5]={0,1,2,3,4}; // IAT ECT O2 MAP TPS
bool monitorEnabled=false,liveValid=false;
volatile uint32_t pulseCount=0;
portMUX_TYPE pulseMux=portMUX_INITIALIZER_UNLOCKED;
uint32_t pulseWindow=0,lastAnalogSample=0,liveSample=0;
float liveHz=0;
uint32_t liveMv[5]={};
bool liveClipped[5]={};
void ARDUINO_ISR_ATTR countRpmPulse(){portENTER_CRITICAL_ISR(&pulseMux);pulseCount++;portEXIT_CRITICAL_ISR(&pulseMux);}
void setMonitor(bool enabled){
 if(enabled==monitorEnabled)return;
 if(enabled){
  analogReadResolution(12);
  for(uint8_t pin:analogPins){pinMode(pin,INPUT);analogSetPinAttenuation(pin,ADC_11db);}
  pinMode(5,INPUT);pinMode(6,INPUT);pinMode(7,INPUT);
  portENTER_CRITICAL(&pulseMux);pulseCount=0;portEXIT_CRITICAL(&pulseMux);
  liveHz=0;liveValid=false;pulseWindow=millis();lastAnalogSample=0;
  attachInterrupt(digitalPinToInterrupt(5),countRpmPulse,RISING);
 }else{detachInterrupt(digitalPinToInterrupt(5));liveValid=false;}
 monitorEnabled=enabled;
}
void updateLive(){
 if(!monitorEnabled)return;
 uint32_t now=millis();
 if((uint32_t)(now-lastAnalogSample)>=100){
  lastAnalogSample=now;
  for(int i=0;i<5;i++){uint32_t sum=0;bool clipped=false;for(int j=0;j<8;j++){sum+=analogReadMilliVolts(analogPins[i]);if(analogRead(analogPins[i])>=4090)clipped=true;}liveMv[i]=sum/8;liveClipped[i]=clipped;}
  liveSample=now;
 }
 uint32_t elapsed=now-pulseWindow;
 if(elapsed>=1000){portENTER_CRITICAL(&pulseMux);uint32_t count=pulseCount;pulseCount=0;portEXIT_CRITICAL(&pulseMux);liveHz=count*1000.0f/elapsed;pulseWindow=now;liveValid=true;}
}
void liveControl(){
 if(!server.hasArg("enabled")||(server.arg("enabled")!="0"&&server.arg("enabled")!="1")){server.send(400,"application/json","{\"error\":\"enabled must be 0 or 1\"}");return;}
 setMonitor(server.arg("enabled")=="1");server.send(200,"application/json",monitorEnabled?"{\"enabled\":true}":"{\"enabled\":false}");
}
void liveTelemetry(){
 server.sendHeader("Cache-Control","no-store");
 if(!monitorEnabled||!liveValid){server.send(503,"application/json","{\"error\":\"Monitor disabled or warming up\"}");return;}
 char json[600];
 snprintf(json,sizeof(json),"{\"source\":\"measured_adc\",\"sample_ms\":%lu,\"age_ms\":%lu,\"iat_mv\":%lu,\"ect_mv\":%lu,\"o2_mv\":%lu,\"map_mv\":%lu,\"tps_mv\":%lu,\"clipped\":[%s,%s,%s,%s,%s],\"rpm_hz\":%.3f,\"switch9\":%d,\"switch10\":%d}",(unsigned long)liveSample,(unsigned long)(millis()-liveSample),(unsigned long)liveMv[0],(unsigned long)liveMv[1],(unsigned long)liveMv[2],(unsigned long)liveMv[3],(unsigned long)liveMv[4],liveClipped[0]?"true":"false",liveClipped[1]?"true":"false",liveClipped[2]?"true":"false",liveClipped[3]?"true":"false",liveClipped[4]?"true":"false",liveHz,digitalRead(6),digitalRead(7));
 server.send(200,"application/json",json);
}
