#include "netserver.h"

#include "config.h"
#include "player.h"
#include "telnet.h"
#include "display.h"
#include "options.h"
#include "network.h"
#include "mqtt.h"
#include "controls.h"
#include <Update.h>
#include <ESPmDNS.h>
#include "ArduinoJson.h"
#ifdef USE_SD
#include "sdmanager.h"
#endif
#ifndef MIN_MALLOC
#define MIN_MALLOC 24112
#endif
#ifndef NSQ_SEND_DELAY
  #define NSQ_SEND_DELAY       (TickType_t)100  //portMAX_DELAY?
#endif

//#define CORS_DEBUG

NetServer netserver;

AsyncWebServer webserver(80);
AsyncWebSocket websocket("/ws");
AsyncUDP udp;

String processor(const String& var);
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleUploadWeb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleHTTPArgs(AsyncWebServerRequest * request);
// send playlist from LittleFS or from SD
void send_playlist(AsyncWebServerRequest * request);
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

bool  shouldReboot  = false;
#ifdef MQTT_ROOT_TOPIC
Ticker mqttplaylistticker;
bool  mqttplaylistblock = false;
void mqttplaylistSend() {
  mqttplaylistblock = true;
  mqttplaylistticker.detach();
  mqttPublishPlaylist();
  mqttplaylistblock = false;
}
#endif

void updateError(String& s) {
  s.reserve(200);
  s = "Update failed with code:";
  s += Update.getError();
  s += ", err:";
  s += Update.errorString();
}

bool NetServer::begin(bool quiet) {
  if(network.status==SDREADY) return true;
  if(!quiet) Serial.print("##[BOOT]#\tnetserver.begin\t");
  importRequest = IMDONE;
  irRecordEnable = false;
  if (!nsQueue)
    nsQueue = xQueueCreate( 20, sizeof( nsRequestParams_t ) );

  if(config.emptyFS){
    webserver.on("/", HTTP_GET, [](AsyncWebServerRequest * request) { request->send(200, asyncsrv::T_text_html, emptyfs_html, processor); });
    webserver.on("/", HTTP_POST, [](AsyncWebServerRequest *request) { 
      if(!request->arg("ssid").isEmpty() && !request->arg("pass").isEmpty()){
        char buf[BUFLEN];
        memset(buf, 0, BUFLEN);
        snprintf(buf, BUFLEN, "%s\t%s", request->arg("ssid").c_str(), request->arg("pass").c_str());
        request->redirect("/");
        config.saveWifiFromNextion(buf);
        return;
      }
      request->redirect("/");
      ESP.restart(); 
    }, handleUploadWeb);
  } else {
    // server index
    webserver.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ if (r->args()) return handleHTTPArgs(r); r->redirect( network.status == CONNECTED ? "/index.html" : "/settings.html");});
    webserver.on("/", HTTP_POST, handleHTTPArgs);
    webserver.on("/webboard", HTTP_GET, [](AsyncWebServerRequest * request) { request->send(200, asyncsrv::T_text_html, emptyfs_html, processor); });
    webserver.on("/webboard", HTTP_POST, [](AsyncWebServerRequest *request) { request->redirect("/"); }, handleUploadWeb);
  }
  
  // playlist serve
  webserver.on(PLAYLIST_PATH, HTTP_GET, send_playlist);
  
  webserver.on("/upload", HTTP_POST, beginUpload, handleUpload);
  webserver.on("/update", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(LittleFS, "/www/update.html", asyncsrv::T_text_html, false, processor); } );
  webserver.on("/update", HTTP_POST, beginUpdate, handleUpdate);
  webserver.serveStatic("/settings", LittleFS, "/www/settings.html", asyncsrv::T_no_cache);
  webserver.serveStatic("/ir", LittleFS, "/www/ir.html", asyncsrv::T_no_cache);

  // server file content from filesystem
  webserver.serveStatic("/", LittleFS, "/www/")
    .setCacheControl(asyncsrv::T_no_cache);     // revalidate based on etag/IMS headers

  webserver.serveStatic("/data", LittleFS, "/data/")
    .setCacheControl(asyncsrv::T_no_cache);     // revalidate based on etag/IMS headers


#ifdef CORS_DEBUG
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), F("*"));
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Headers"), F("content-type"));
#endif
  webserver.begin();
  if(strlen(config.store.mdnsname)>0)
    MDNS.begin(config.store.mdnsname);
  websocket.onEvent(onWsEvent);
  webserver.addHandler(&websocket);

  //echo -n "helle?" | socat - udp-datagram:255.255.255.255:44490,broadcast
  if (udp.listen(44490)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      if (strcmp((char*)packet.data(), "helle?") == 0)
        packet.println(WiFi.localIP());
    });
  }
  if(!quiet) Serial.println("done");
  return true;
}

void NetServer::beginUpdate(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response;
  shouldReboot = !Update.hasError();
  if (shouldReboot)
    response = request->beginResponse(200, asyncsrv::T_text_plain, "OK");
  else {
    String s;
    updateError(s);
    response = request->beginResponse(200, asyncsrv::T_text_plain, s.c_str());
  }

  response->addHeader(asyncsrv::T_Connection, asyncsrv::T_close);
  request->send(response);
}

void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    int target = (request->getParam("updatetarget", true)->value() == "spiffs") ? U_SPIFFS : U_FLASH;
    Serial.printf("Update Start: %s\n", filename.c_str());
    player.sendCommand({PR_STOP, 0});
    display.putRequest(NEWMODE, UPDATING);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, target)) {
      Update.printError(Serial);
      String s;
      updateError(s);
      request->send(200, asyncsrv::T_text_html, s.c_str() );
    }
  }
  if (!Update.hasError()) {
    if (Update.write(data, len) != len) {
      Update.printError(Serial);
      String s;
      updateError(s);
      request->send(200, asyncsrv::T_text_html, s.c_str() );
    }
  }
  if (final) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %uB\n", index + len);
    } else {
      Update.printError(Serial);
      String s;
      updateError(s);
      request->send(200, asyncsrv::T_text_html, s.c_str() );
    }
  }
}

void NetServer::beginUpload(AsyncWebServerRequest *request) {
  if (request->hasParam("plfile", true, true)) {
    netserver.importRequest = IMPL;
    request->send(200);
  } else if (request->hasParam("wifile", true, true)) {
    netserver.importRequest = IMWIFI;
    request->send(200);
  } else {
    request->send(404);
  }
}

size_t NetServer::chunkedHtmlPageCallback(uint8_t* buffer, size_t maxLen, size_t index){
  File requiredfile;
  bool sdpl = strcmp(netserver.chunkedPathBuffer, PLAYLIST_SD_PATH) == 0;
  if(sdpl){
    requiredfile = config.SDPLFS()->open(netserver.chunkedPathBuffer, "r");
  }else{
    requiredfile = LittleFS.open(netserver.chunkedPathBuffer, "r");
  }
  if (!requiredfile) return 0;
  size_t filesize = requiredfile.size();
  size_t needread = filesize - index;
  if (!needread) {
    requiredfile.close();
    return 0;
  }
  size_t canread = (needread > maxLen) ? maxLen : needread;
  DBGVB("[%s] seek to %d in %s and read %d bytes with maxLen=%d", __func__, index, netserver.chunkedPathBuffer, canread, maxLen);
  requiredfile.seek(index, SeekSet);
  //vTaskDelay(1);
  requiredfile.read(buffer, canread);
  index += canread;
  if (requiredfile) requiredfile.close();
  return canread;
}

void NetServer::chunkedHtmlPage(const String& contentType, AsyncWebServerRequest *request, const char * path, bool doproc) {
  memset(chunkedPathBuffer, 0, sizeof(chunkedPathBuffer));
  strlcpy(chunkedPathBuffer, path, sizeof(chunkedPathBuffer)-1);
  AsyncWebServerResponse *response;
  if(doproc)
    response = request->beginChunkedResponse(contentType, chunkedHtmlPageCallback, processor);
  else
    response = request->beginChunkedResponse(contentType, chunkedHtmlPageCallback);
  request->send(response);
}

#ifndef DSP_NOT_FLIPPED
  #define DSP_CAN_FLIPPED true
#else
  #define DSP_CAN_FLIPPED false
#endif
#if !defined(HIDE_WEATHER) && (!defined(DUMMYDISPLAY) && !defined(USE_NEXTION))
  #define SHOW_WEATHER  true
#else
  #define SHOW_WEATHER  false
#endif

#ifndef NS_QUEUE_TICKS
  #define NS_QUEUE_TICKS 0
#endif

const char *getFormat(BitrateFormat _format) {
  switch (_format) {
    case BF_MP3:  return "MP3";
    case BF_AAC:  return "AAC";
    case BF_FLAC: return "FLC";
    case BF_OGG:  return "OGG";
    case BF_WAV:  return "WAV";
    default:      return "bitrate";
  }
}

void NetServer::processQueue(){
  if(nsQueue==NULL) return;
  nsRequestParams_t request;
  if(xQueueReceive(nsQueue, &request, NS_QUEUE_TICKS)){
    uint8_t clientId = request.clientId;
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    switch (request.type) {
      case PLAYLIST:        getPlaylist(clientId); break;
      case PLAYLISTSAVED:   {
        #ifdef USE_SD
        if(config.getMode()==PM_SDCARD) {
        //  config.indexSDPlaylist();
          config.initSDPlaylist();
        }
        #endif
        if(config.getMode()==PM_WEB){
          config.indexPlaylist(); 
          config.initPlaylist(); 
        }
        getPlaylist(clientId); break;
      }
      case GETACTIVE: {
          bool dbgact = false, nxtn=false;
          JsonArray act = obj["act"].to<JsonArray>();
          act.add("group_wifi");
          if (network.status == CONNECTED) {
            act.add("group_system");
            if (BRIGHTNESS_PIN != 255 || DSP_CAN_FLIPPED || DSP_MODEL == DSP_NOKIA5110 || dbgact)
              act.add("group_display");
          #ifdef USE_NEXTION
            act.add("group_nextion");
            if (!SHOW_WEATHER || dbgact)
              act.add("group_weather");
            nxtn=true;
          #endif
          #if defined(LCD_I2C) || defined(DSP_OLED)
            act.add("group_oled");
          #endif
          #ifndef HIDE_VU
            act.add("group_vu");
          #endif
            if (BRIGHTNESS_PIN != 255 || nxtn || dbgact)
              act.add("group_brightness");
            if (DSP_CAN_FLIPPED || dbgact)
              act.add("group_tft");
            if (TS_MODEL != TS_MODEL_UNDEFINED || dbgact)
              act.add("group_touch");
            if (DSP_MODEL == DSP_NOKIA5110)
              act.add("group_nokia");

            act.add("group_timezone");
            if (SHOW_WEATHER || dbgact)
              act.add("group_weather");

            act.add("group_controls");
            if (ENC_BTNL != 255 || ENC2_BTNL != 255 || dbgact)
              act.add("group_encoder");
            if (IR_PIN != 255 || dbgact)
              act.add("group_ir");
          }
          break;
      }

      case GETMODE:
        obj["pmode"] = network.status == CONNECTED ? "player" : "ap";
        break;

      case GETINDEX:      {
        requestOnChange(STATION, clientId); 
        requestOnChange(TITLE, clientId); 
        requestOnChange(VOLUME, clientId); 
        requestOnChange(EQUALIZER, clientId); 
        requestOnChange(BALANCE, clientId); 
        requestOnChange(BITRATE, clientId); 
        requestOnChange(MODE, clientId); 
        requestOnChange(SDINIT, clientId);
        requestOnChange(GETPLAYERMODE, clientId); 
        if (config.getMode()==PM_SDCARD) { requestOnChange(SDPOS, clientId); requestOnChange(SDLEN, clientId); requestOnChange(SDSNUFFLE, clientId); } 
        return; 
        break;
      }

      case GETSYSTEM:
        obj["sst"] = config.store.smartstart != 2;
        obj["aif"] = config.store.audioinfo;
        obj["vu"] = config.store.vumeter;
        obj["softr"] = config.store.softapdelay;
        obj["vut"] = config.vuThreshold;
        obj["mdns"] = config.store.mdnsname;
        break;

      case GETSCREEN:
        obj["flip"] = config.store.flipscreen;
        obj["inv"] =  config.store.invertdisplay;
        obj["nump"] =  config.store.numplaylist;
        obj["tsf"] =  config.store.fliptouch;
        obj["tsd"] =  config.store.dbgtouch;
        obj["dspon"] =  config.store.dspon;
        obj["br"] =  config.store.brightness;
        obj["con"] =  config.store.contrast;
        obj["scre"] =  config.store.screensaverEnabled;
        obj["scrt"] =  config.store.screensaverTimeout;
        obj["scrb"] =  config.store.screensaverBlank;
        obj["scrpe"] =  config.store.screensaverPlayingEnabled;
        obj["scrpt"] =  config.store.screensaverPlayingTimeout;
        obj["scrpb"] =  config.store.screensaverPlayingBlank;
        break;

      case GETTIMEZONE:
        obj["tzh"] =  config.store.tzHour;
        obj["tzm"] =  config.store.tzMin;
        obj["sntp1"] =  config.store.sntp1;
        obj["sntp2"] =  config.store.sntp2;
        break;

      case GETWEATHER:
        obj["wen"] = config.store.showweather;
        obj["wlat"] = config.store.weatherlat;
        obj["wlon"] = config.store.weatherlon;
        obj["wkey"] = config.store.weatherkey;
        break;

      case GETCONTROLS:
        obj["vols"] =  config.store.volsteps;
        obj["enca"] =  config.store.encacc;
        obj["irtl"] =  config.store.irtlp;
        obj["skipup"] =  config.store.skipPlaylistUpDown;
        break;
      case DSPON:
        obj["dspontrue"] = true;
        break;

      case STATION:
        requestOnChange(STATIONNAME, clientId); requestOnChange(ITEM, clientId);
        break;

      case STATIONNAME:
        obj["nameset"] = config.station.name;
        break;
      
      case ITEM:
        obj["current"] = config.lastStation();
        break;

      case TITLE:
        obj["meta"] = config.station.title;
        telnet.printf("##CLI.META#: %s\n> ", config.station.title);
        break;

      case VOLUME:
        obj["vol"] = config.store.volume;
        telnet.printf("##CLI.VOL#: %d\n", config.store.volume);
        break;

      case NRSSI:
        obj["rssi"] = rssi;
        break;

      case SDPOS:
        obj["sdpos"] =  player.getFilePos();
        obj["sdend"] =  player.getFileSize();
        obj["sdtpos"] =  player.getAudioCurrentTime();
        obj["sdtend"] =  player.getAudioFileDuration(); 
        break;

      case SDLEN:
        obj["sdmin"] =  player.sd_min;
        obj["sdmax"] =  player.sd_max;
        break;

      case SDSNUFFLE:
        obj["snuffle"] =  config.store.sdsnuffle;
        break;

      case BITRATE:
        obj["bitrate"] = config.station.bitrate;
        obj["format"] =  static_cast<int32_t>(config.configFmt);
        break;

      case MODE:
        obj["mode"] = player.status() == PLAYING ? "playing" : "stopped";
        break;

      case EQUALIZER:
        obj["bass"] = config.store.bass;
        obj["middle"] = config.store.middle;
        obj["trebble"] = config.store.trebble;
        break;

      case BALANCE:
        obj["balance"] = config.store.balance;        
        break;

      case SDINIT:
        obj["sdinit"] = SDC_CS!=255;        
        break;

      case GETPLAYERMODE:
        obj["playermode"] = config.getMode()==PM_SDCARD?"modesd":"modeweb";        
        break;

    #ifdef USE_SD
      case CHANGEMODE:
        config.changeMode(newConfigMode);
        return;
    #endif

      default:          break;
    }
    if (!doc.isNull()){
      size_t length = measureJson(obj);
      auto buffer = websocket.makeBuffer(length);
      if (buffer){
        serializeJson(obj, (char*)buffer->get(), length);
      }
      if (clientId == 0)
        { websocket.textAll(buffer); }
      else
        { websocket.text(clientId, buffer); }

      #ifdef MQTT_ROOT_TOPIC
        if (clientId == 0 && (request.type == STATION || request.type == ITEM || request.type == TITLE || request.type == MODE)) mqttPublishStatus();
        if (clientId == 0 && request.type == VOLUME) mqttPublishVolume();
      #endif
    }
  }
}

void NetServer::loop() {
  if(network.status==SDREADY) return;
  if (shouldReboot) {
    Serial.println("Rebooting...");
    delay(100);
    ESP.restart();
  }
  websocket.cleanupClients();
  switch (importRequest) {
    case IMPL:    importPlaylist();  importRequest = IMDONE; break;
    case IMWIFI:  config.saveWifi(); importRequest = IMDONE; break;
    default:      break;
  }
  //if (rssi < 255) requestOnChange(NRSSI, 0);
  processQueue();
}

#if IR_PIN!=255
void NetServer::irToWs(const char* protocol, uint64_t irvalue) {
  char buf[BUFLEN] = { 0 };
  sprintf (buf, "{\"ircode\": %llu, \"protocol\": \"%s\"}", irvalue, protocol);
  websocket.textAll(buf);
}
void NetServer::irValsToWs() {
  if (!irRecordEnable) return;
  char buf[BUFLEN] = { 0 };
  sprintf (buf, "{\"irvals\": [%llu, %llu, %llu]}", config.ircodes.irVals[config.irindex][0], config.ircodes.irVals[config.irindex][1], config.ircodes.irVals[config.irindex][2]);
  websocket.textAll(buf);
}
#endif

void NetServer::onWsMessage(void *arg, uint8_t *data, size_t len, uint8_t clientId) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    char cmd[65], val[65];
    if (config.parseWsCommand((const char*)data, cmd, val, 65)) {
      if (strcmp(cmd, "getmode") == 0     ) { requestOnChange(GETMODE, clientId);     return; }
      if (strcmp(cmd, "getindex") == 0    ) { requestOnChange(GETINDEX, clientId);    return; }
      if (strcmp(cmd, "getsystem") == 0   ) { requestOnChange(GETSYSTEM, clientId);   return; }
      if (strcmp(cmd, "getscreen") == 0   ) { requestOnChange(GETSCREEN, clientId);   return; }
      if (strcmp(cmd, "gettimezone") == 0 ) { requestOnChange(GETTIMEZONE, clientId); return; }
      if (strcmp(cmd, "getcontrols") == 0 ) { requestOnChange(GETCONTROLS, clientId); return; }
      if (strcmp(cmd, "getweather") == 0  ) { requestOnChange(GETWEATHER, clientId);  return; }
      if (strcmp(cmd, "getactive") == 0   ) { requestOnChange(GETACTIVE, clientId);   return; }
      if (strcmp(cmd, "newmode") == 0     ) { newConfigMode = atoi(val); requestOnChange(CHANGEMODE, 0); return; }
      if (strcmp(cmd, "smartstart") == 0) {
        uint8_t valb = atoi(val);
        uint8_t ss = valb == 1 ? 1 : 2;
        if (!player.isRunning() && ss == 1) ss = 0;
        config.setSmartStart(ss);
        return;
      }
      if (strcmp(cmd, "audioinfo") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.audioinfo, valb);
        display.putRequest(AUDIOINFO);
        return;
      }
      if (strcmp(cmd, "vumeter") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.vumeter, valb);
        display.putRequest(SHOWVUMETER);
        return;
      }
      if (strcmp(cmd, "softap") == 0) {
        uint8_t valb = atoi(val);
        config.saveValue(&config.store.softapdelay, valb);
        return;
      }
      if (strcmp(cmd, "mdnsname") == 0) {
        config.saveValue(config.store.mdnsname, val, MDNS_LENGTH);
        return;
      }
      if (strcmp(cmd, "rebootmdns") == 0) {
        char buf[MDNS_LENGTH*2];
        if(strlen(config.store.mdnsname)>0)
          snprintf(buf, MDNS_LENGTH*2, "{\"redirect\": \"http://%s.local\"}", config.store.mdnsname);
        else
          snprintf(buf, MDNS_LENGTH*2, "{\"redirect\": \"http://%s/\"}", WiFi.localIP().toString().c_str());
        websocket.text(clientId, buf);
        delay(500);
        ESP.restart();
        return;
      }
      if (strcmp(cmd, "invertdisplay") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.invertdisplay, valb);
        display.invert();
        return;
      }
      if (strcmp(cmd, "numplaylist") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.numplaylist, valb);
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
        return;
      }
      if (strcmp(cmd, "fliptouch") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.fliptouch, valb);
        flipTS();
        return;
      }
      if (strcmp(cmd, "dbgtouch") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.dbgtouch, valb);
        return;
      }
      if (strcmp(cmd, "flipscreen") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.flipscreen, valb);
        display.flip();
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
        return;
      }
      if (strcmp(cmd, "brightness") == 0) {
        uint8_t valb = atoi(val);
        if (!config.store.dspon) requestOnChange(DSPON, 0);
        config.store.brightness = valb;
        config.setBrightness(true);
        return;
      }
      if (strcmp(cmd, "screenon") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.setDspOn(valb);
        return;
      }
      if (strcmp(cmd, "contrast") == 0) {
        uint8_t valb = atoi(val);
        config.saveValue(&config.store.contrast, valb);
        display.setContrast();
        return;
      }
      if (strcmp(cmd, "screensaverenabled") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverEnabled, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensavertimeout") == 0) {
        uint16_t valb = atoi(val);
        valb = constrain(valb,5,65520);
        config.saveValue(&config.store.screensaverTimeout, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverblank") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverBlank, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverplayingenabled") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverPlayingEnabled, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverplayingtimeout") == 0) {
        uint16_t valb = atoi(val);
        valb = constrain(valb,1,1080);
        config.saveValue(&config.store.screensaverPlayingTimeout, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverplayingblank") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverPlayingBlank, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "tzh") == 0) {
        int8_t vali = atoi(val);
        config.saveValue(&config.store.tzHour, vali);
        return;
      }
      if (strcmp(cmd, "tzm") == 0) {
        int8_t vali = atoi(val);
        config.saveValue(&config.store.tzMin, vali);
        return;
      }
      if (strcmp(cmd, "sntp2") == 0) {
        config.saveValue(config.store.sntp2, val, 35);
        return;
      }
      if (strcmp(cmd, "sntp1") == 0) {
        config.saveValue(config.store.sntp1, val, 35);
        bool tzdone = false;
        if (strlen(config.store.sntp1) > 0 && strlen(config.store.sntp2) > 0) {
          configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1, config.store.sntp2);
          tzdone = true;
        } else if (strlen(config.store.sntp1) > 0) {
          configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1);
          tzdone = true;
        }
        if (tzdone) {
          network.forceTimeSync = true;
        }
        return;
      }
      if (strcmp(cmd, "volsteps") == 0) {
        uint8_t valb = atoi(val);
        config.saveValue(&config.store.volsteps, valb);
        return;
      }
      if (strcmp(cmd, "encacceleration") == 0) {
        uint16_t valb = atoi(val);
        setEncAcceleration(valb);
        config.saveValue(&config.store.encacc, valb);
        return;
      }
      if (strcmp(cmd, "irtlp") == 0) {
        uint8_t valb = atoi(val);
        setIRTolerance(valb);
        return;
      }
      if (strcmp(cmd, "oneclickswitching") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.skipPlaylistUpDown, valb);
        return;
      }
      if (strcmp(cmd, "showweather") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.showweather, valb);
        network.trueWeather=false;
        network.forceWeather = true;
        display.putRequest(SHOWWEATHER);
        return;
      }
      if (strcmp(cmd, "lat") == 0) {
        config.saveValue(config.store.weatherlat, val, 10, false);
        return;
      }
      if (strcmp(cmd, "lon") == 0) {
        config.saveValue(config.store.weatherlon, val, 10, false);
        return;
      }
      if (strcmp(cmd, "key") == 0) {
        config.saveValue(config.store.weatherkey, val, WEATHERKEY_LENGTH);
        network.trueWeather=false;
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
        return;
      }
      /*  RESETS  */
      if (strcmp(cmd, "reset") == 0) {
        if (strcmp(val, "system") == 0) {
          config.saveValue(&config.store.smartstart, (uint8_t)2, false);
          config.saveValue(&config.store.audioinfo, false, false);
          config.saveValue(&config.store.vumeter, false, false);
          config.saveValue(&config.store.softapdelay, (uint8_t)0, false);
          snprintf(config.store.mdnsname, MDNS_LENGTH, "yoradio-%x", config.getChipId());
          config.saveValue(config.store.mdnsname, config.store.mdnsname, MDNS_LENGTH, true, true);
          display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
          requestOnChange(GETSYSTEM, clientId);
          return;
        }
        if (strcmp(val, "screen") == 0) {
          config.saveValue(&config.store.flipscreen, false, false);
          display.flip();
          config.saveValue(&config.store.invertdisplay, false, false);
          display.invert();
          config.saveValue(&config.store.dspon, true, false);
          config.store.brightness = 100;
          config.setBrightness(false);
          config.saveValue(&config.store.contrast, (uint8_t)55, false);
          display.setContrast();
          config.saveValue(&config.store.numplaylist, false);
          config.saveValue(&config.store.screensaverEnabled, false);
          config.saveValue(&config.store.screensaverTimeout, (uint16_t)20);
          config.saveValue(&config.store.screensaverBlank, false);
          config.saveValue(&config.store.screensaverPlayingEnabled, false);
          config.saveValue(&config.store.screensaverPlayingTimeout, (uint16_t)5);
          config.saveValue(&config.store.screensaverPlayingBlank, false);
          display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
          requestOnChange(GETSCREEN, clientId);
          return;
        }
        if (strcmp(val, "timezone") == 0) {
          config.saveValue(&config.store.tzHour, (int8_t)3, false);
          config.saveValue(&config.store.tzMin, (int8_t)0, false);
          config.saveValue(config.store.sntp1, "pool.ntp.org", 35, false);
          config.saveValue(config.store.sntp2, "ru.pool.ntp.org", 35);
          configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1, config.store.sntp2);
          network.forceTimeSync = true;
          requestOnChange(GETTIMEZONE, clientId);
          return;
        }
        if (strcmp(val, "weather") == 0) {
          config.saveValue(&config.store.showweather, false, false);
          config.saveValue(config.store.weatherlat, "55.7512", 10, false);
          config.saveValue(config.store.weatherlon, "37.6184", 10, false);
          config.saveValue(config.store.weatherkey, "", WEATHERKEY_LENGTH);
          network.trueWeather=false;
          display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
          requestOnChange(GETWEATHER, clientId);
          return;
        }
        if (strcmp(val, "controls") == 0) {
          config.saveValue(&config.store.volsteps, (uint8_t)1, false);
          config.saveValue(&config.store.fliptouch, false, false);
          config.saveValue(&config.store.dbgtouch, false, false);
          config.saveValue(&config.store.skipPlaylistUpDown, false);
          
          setEncAcceleration(200);
          setIRTolerance(40);
          requestOnChange(GETCONTROLS, clientId);
          return;
        }
      } /*  EOF RESETS  */
      if (strcmp(cmd, "volume") == 0) {
        uint8_t v = atoi(val);
        player.setVol(v);
      }
      if (strcmp(cmd, "sdpos") == 0) {
        //return;
        if (config.getMode()==PM_SDCARD){
          config.sdResumePos = 0;
          if(!player.isRunning()){
            player.setResumeFilePos(atoi(val)-player.sd_min);
            player.sendCommand({PR_PLAY, config.store.lastSdStation});
          }else{
            player.setFilePos(atoi(val)-player.sd_min);
          }
        }
        return;
      }
      if (strcmp(cmd, "snuffle") == 0) {
        config.setSnuffle(strcmp(val, "true") == 0);
        return;
      }
      if (strcmp(cmd, "balance") == 0) {
        int8_t valb = atoi(val);
        player.setBalance(valb);
        config.setBalance(valb);
        netserver.requestOnChange(BALANCE, 0);
        return;
      }
      if (strcmp(cmd, "treble") == 0) {
        int8_t valb = atoi(val);
        player.setTone(config.store.bass, config.store.middle, valb);
        config.setTone(config.store.bass, config.store.middle, valb);
        netserver.requestOnChange(EQUALIZER, 0);
        return;
      }
      if (strcmp(cmd, "middle") == 0) {
        int8_t valb = atoi(val);
        player.setTone(config.store.bass, valb, config.store.trebble);
        config.setTone(config.store.bass, valb, config.store.trebble);
        netserver.requestOnChange(EQUALIZER, 0);
        return;
      }
      if (strcmp(cmd, "bass") == 0) {
        int8_t valb = atoi(val);
        player.setTone(valb, config.store.middle, config.store.trebble);
        config.setTone(valb, config.store.middle, config.store.trebble);
        netserver.requestOnChange(EQUALIZER, 0);
        return;
      }
      if (strcmp(cmd, "submitplaylist") == 0) {
        return;
      }
      if (strcmp(cmd, "submitplaylistdone") == 0) {
#ifdef MQTT_ROOT_TOPIC
        //mqttPublishPlaylist();
        mqttplaylistticker.attach(5, mqttplaylistSend);
#endif
        if (player.isRunning()) {
          player.sendCommand({PR_PLAY, -config.lastStation()});
        }
        return;
      }
#if IR_PIN!=255
      if (strcmp(cmd, "irbtn") == 0) {
        config.irindex = atoi(val);
        irRecordEnable = (config.irindex >= 0);
        config.irchck = 0;
        irValsToWs();
        if (config.irindex < 0) config.saveIR();
      }
      if (strcmp(cmd, "chkid") == 0) {
        config.irchck = atoi(val);
      }
      if (strcmp(cmd, "irclr") == 0) {
        uint8_t cl = atoi(val);
        config.ircodes.irVals[config.irindex][cl] = 0;
      }
#endif
    }
  }
}

void NetServer::getPlaylist(uint8_t clientId) {
  char buf[160] = {0};
  sprintf(buf, "{\"file\": \"http://%s%s\"}", WiFi.localIP().toString().c_str(), PLAYLIST_PATH);
  if (clientId == 0) { websocket.textAll(buf); } else { websocket.text(clientId, buf); }
}

int NetServer::_readPlaylistLine(File &file, char * line, size_t size){
  int bytesRead = file.readBytesUntil('\n', line, size);
  if(bytesRead>0){
    line[bytesRead] = 0;
    if(line[bytesRead-1]=='\r') line[bytesRead-1]=0;
  }
  return bytesRead;
}

bool NetServer::importPlaylist() {
  if(config.getMode()==PM_SDCARD) return false;
  File tempfile = LittleFS.open(TMP_PATH, "r");
  if (!tempfile) {
    return false;
  }
  char sName[BUFLEN], sUrl[BUFLEN], linePl[BUFLEN*3];;
  int sOvol;
  _readPlaylistLine(tempfile, linePl, sizeof(linePl)-1);
  if (config.parseCSV(linePl, sName, sUrl, sOvol)) {
    tempfile.close();
    LittleFS.rename(TMP_PATH, PLAYLIST_PATH);
    requestOnChange(PLAYLISTSAVED, 0);
    return true;
  }
  if (config.parseJSON(linePl, sName, sUrl, sOvol)) {
    File playlistfile = LittleFS.open(PLAYLIST_PATH, "w");
    snprintf(linePl, sizeof(linePl)-1, "%s\t%s\t%d", sName, sUrl, 0);
    playlistfile.println(linePl);
    while (tempfile.available()) {
      _readPlaylistLine(tempfile, linePl, sizeof(linePl)-1);
      if (config.parseJSON(linePl, sName, sUrl, sOvol)) {
        snprintf(linePl, sizeof(linePl)-1, "%s\t%s\t%d", sName, sUrl, 0);
        playlistfile.println(linePl);
      }
    }
    playlistfile.flush();
    playlistfile.close();
    tempfile.close();
    LittleFS.remove(TMP_PATH);
    requestOnChange(PLAYLISTSAVED, 0);
    return true;
  }
  tempfile.close();
  LittleFS.remove(TMP_PATH);
  return false;
}

void NetServer::requestOnChange(requestType_e request, uint8_t clientId) {
  if(nsQueue==NULL) return;
  nsRequestParams_t nsrequest;
  nsrequest.type = request;
  nsrequest.clientId = clientId;
  xQueueSend(nsQueue, &nsrequest, NSQ_SEND_DELAY);
}

void NetServer::resetQueue(){
  if(nsQueue!=NULL) xQueueReset(nsQueue);
}

// HTML %Templates% processor
String processor(const String& var) {
  if (var == "ACTION") return (network.status == CONNECTED && !config.emptyFS) ? String("webboard") : String();
  if (var == "UPLOADWIFI") return (network.status == CONNECTED) ? String(" hidden") : String();
  if (var == "VERSION") return YOVERSION;
  return String();
}

int freeSpace;
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    if(filename!="tempwifi.csv"){
      if(LittleFS.exists(PLAYLIST_PATH)) LittleFS.remove(PLAYLIST_PATH);
      if(LittleFS.exists(INDEX_PATH)) LittleFS.remove(INDEX_PATH);
      if(LittleFS.exists(PLAYLIST_SD_PATH)) LittleFS.remove(PLAYLIST_SD_PATH);
      if(LittleFS.exists(INDEX_SD_PATH)) LittleFS.remove(INDEX_SD_PATH);
    }
    freeSpace = (float)LittleFS.totalBytes()/100*68-LittleFS.usedBytes();
    request->_tempFile = LittleFS.open(TMP_PATH , "w", true);
  }
  if (len) {
    if(freeSpace>index+len){
      request->_tempFile.write(data, len);
    }
  }
  if (final) {
    request->_tempFile.close();
  }
}

void handleUploadWeb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  DBGVB("File: %s, size:%u bytes, index: %u, final: %s\n", filename.c_str(), len, index, final?"true":"false");
  if (!index) {
    String spath = "/www/";
    if(filename=="playlist.csv" || filename=="wifi.csv") spath = "/data/";
    request->_tempFile = LittleFS.open(spath + filename , "w");
  }
  if (len) {
    request->_tempFile.write(data, len);
  }
  if (final) {
    request->_tempFile.close();
    if(filename=="playlist.csv") config.indexPlaylist();
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT: if (config.store.audioinfo) Serial.printf("[WEBSOCKET] client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str()); break;
    case WS_EVT_DISCONNECT: if (config.store.audioinfo) Serial.printf("[WEBSOCKET] client #%u disconnected\n", client->id()); break;
    case WS_EVT_DATA: netserver.onWsMessage(arg, data, len, client->id()); break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void send_playlist(AsyncWebServerRequest * request){
#ifdef MQTT_ROOT_TOPIC    // very ugly check
  // if MQTT something is in progress, ask client to return later
  if (mqttplaylistblock){
    AsyncWebServerResponse *response = request->beginResponse(429);
    response->addHeader(asyncsrv::T_retry_after, 1);
    return request->send(response);
  }
#endif
  if (config.getMode()==PM_SDCARD)    // redirect to SDCARD's path
    return request->redirect(PLAYLIST_SD_PATH);

  // last resort - send playlist from LittleFS
  request->send(LittleFS, request->url().c_str());
}

void handleHTTPArgs(AsyncWebServerRequest * request) {
  if (network.status != CONNECTED) {
    return request->send(503, asyncsrv::T_text_plain, "Network is not available");
  }

    bool commandFound=false;
    if (request->hasArg("start")) { player.sendCommand({PR_PLAY, config.lastStation()}); commandFound=true; }
    if (request->hasArg("stop")) { player.sendCommand({PR_STOP, 0}); commandFound=true; }
    if (request->hasArg("toggle")) { player.toggle(); commandFound=true; }
    if (request->hasArg("prev")) { player.prev(); commandFound=true; }
    if (request->hasArg("next")) { player.next(); commandFound=true; }
    if (request->hasArg("volm")) { player.stepVol(false); commandFound=true; }
    if (request->hasArg("volp")) { player.stepVol(true); commandFound=true; }
    #ifdef USE_SD
    if (request->hasArg("mode")) {
      const AsyncWebParameter* p = request->getParam("mode");
      int mm = atoi(p->value().c_str());
      if(mm>2) mm=0;
      if(mm==2)
        config.changeMode();
      else
        config.changeMode(mm);
      commandFound=true;
    }
    #endif
    if (request->hasArg("reset")) { request->redirect("/"); request->send(200); config.reset(); return; }
    if (request->hasArg("trebble") && request->hasArg("middle") && request->hasArg("bass")) {
      const AsyncWebParameter* pt = request->getParam("trebble", request->method() == HTTP_POST);
      const AsyncWebParameter* pm = request->getParam("middle", request->method() == HTTP_POST);
      const AsyncWebParameter* pb = request->getParam("bass", request->method() == HTTP_POST);
      int t = atoi(pt->value().c_str());
      int m = atoi(pm->value().c_str());
      int b = atoi(pb->value().c_str());
      player.setTone(b, m, t);
      config.setTone(b, m, t);
      netserver.requestOnChange(EQUALIZER, 0);
      commandFound=true;
    }
    if (request->hasArg("ballance")) {
      const AsyncWebParameter* p = request->getParam("ballance", request->method() == HTTP_POST);
      int b = atoi(p->value().c_str());
      player.setBalance(b);
      config.setBalance(b);
      netserver.requestOnChange(BALANCE, 0);
      commandFound=true;
    }
    if (request->hasArg("playstation") || request->hasArg("play")) {
      const AsyncWebParameter* p = request->getParam(request->hasArg("playstation") ? "playstation" : "play", request->method() == HTTP_POST);
      int id = atoi(p->value().c_str());
      if (id < 1) id = 1;
      if (id > config.store.countStation) id = config.store.countStation;
      //config.sdResumePos = 0;
      player.sendCommand({PR_PLAY, id});
      commandFound=true;
      DBGVB("[%s] play=%d", __func__, id);
    }
    if (request->hasArg("vol")) {
      const AsyncWebParameter* p = request->getParam("vol", request->method() == HTTP_POST);
      int v = atoi(p->value().c_str());
      if (v < 0) v = 0;
      if (v > 254) v = 254;
      config.store.volume = v;
      player.setVol(v);
      commandFound=true;
      DBGVB("[%s] vol=%d", __func__, v);
    }
    if (request->hasArg("dspon")) {
      const AsyncWebParameter* p = request->getParam("dspon", request->method() == HTTP_POST);
      int d = atoi(p->value().c_str());
      config.setDspOn(d!=0);
      commandFound=true;
    }
    if (request->hasArg("dim")) {
      const AsyncWebParameter* p = request->getParam("dim", request->method() == HTTP_POST);
      int d = atoi(p->value().c_str());
      if (d < 0) d = 0;
      if (d > 100) d = 100;
      config.store.brightness = (uint8_t)d;
      config.setBrightness(true);
      commandFound=true;
    }
    if (request->hasArg("sleep")) {
      const AsyncWebParameter* sfor = request->getParam("sleep", request->method() == HTTP_POST);
      int sford = atoi(sfor->value().c_str());
      int safterd = 0;
      if(request->hasArg("after")){
        const AsyncWebParameter* safter = request->getParam("after", request->method() == HTTP_POST);
        safterd = atoi(safter->value().c_str());
      }
      if(sford > 0 && safterd >= 0){
        request->send(200);
        config.sleepForAfter(sford, safterd);
        commandFound=true;
      }
    }
    if (request->hasArg("clearspiffs")) {
      if(config.spiffsCleanup()){
        config.saveValue(&config.store.play_mode, static_cast<uint8_t>(PM_WEB));
        request->redirect("/");
        ESP.restart();
      }else{
        request->send(200);
      }
      return;
    }

    if (commandFound){
      return request->send(200);
    } else
      return request->send(404, asyncsrv::T_text_plain, "Command unknown");
}
