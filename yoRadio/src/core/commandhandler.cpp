#include "commandhandler.h"
#include "player.h"
#include "../displays/dspcore.h"
#include "netserver.h"
#include "config.h"
#include "controls.h"
#include "options.h"
#include "evtloop.h"
#include "log.h"

CommandHandler cmd;

bool CommandHandler::exec(const char *command, const char *value, uint8_t cid) {
  LOGD("CMD: ", printf, "%s:%s\n", command, value);
#ifdef USE_SD
  if (strEquals(command, "mode"))     { config.changeMode(atoi(value)); return true; }
#endif
  if (strEquals(command, "reset") && cid==0)    { config.reset(); return true; }
  if (strEquals(command, "dspon"))     { config.setDspOn(atoi(value)!=0); return true; }
  if (strEquals(command, "dim"))       { int d=atoi(value); config.store.brightness = (uint8_t)(d < 0 ? 0 : (d > 100 ? 100 : d)); config.setBrightness(true); return true; }
  if (strEquals(command, "clearspiffs")){ config.spiffsCleanup(); config.saveValue(&config.store.play_mode, static_cast<uint8_t>(PM_WEB)); return true; }
  /*********************************************/
  /****************** WEBSOCKET ****************/
  /*********************************************/
  if (strEquals(command, "getindex"))  { netserver.requestOnChange(GETINDEX, cid); return true; }
  
  if (strEquals(command, "getsystem"))  { netserver.requestOnChange(GETSYSTEM, cid); return true; }
  if (strEquals(command, "getscreen"))  { netserver.requestOnChange(GETSCREEN, cid); return true; }
  if (strEquals(command, "gettimezone")){ netserver.requestOnChange(GETTIMEZONE, cid); return true; }
  if (strEquals(command, "getcontrols")){ netserver.requestOnChange(GETCONTROLS, cid); return true; }
  if (strEquals(command, "getweather")) { netserver.requestOnChange(GETWEATHER, cid); return true; }
  if (strEquals(command, "getactive"))  { netserver.requestOnChange(GETACTIVE, cid); return true; }
  if (strEquals(command, "newmode"))    { config.newConfigMode = atoi(value); netserver.requestOnChange(CHANGEMODE, cid); return true; }
  
  if (strEquals(command, "invertdisplay")){ config.saveValue(&config.store.invertdisplay, static_cast<bool>(atoi(value))); display->invert(); return true; }
  if (strEquals(command, "numplaylist")){
    config.saveValue(&config.store.numplaylist, static_cast<bool>(atoi(value)));
    int32_t d = CLEAR;
    EVT_POST_DATA(YO_CMD_EVENTS, e2int(evt::yo_event_t::displayNewMode), &d, sizeof(d));
    d = PLAYER;
    EVT_POST_DATA(YO_CMD_EVENTS, e2int(evt::yo_event_t::displayNewMode), &d, sizeof(d));
    return true;
  }
  if (strEquals(command, "fliptouch"))    { config.saveValue(&config.store.fliptouch, static_cast<bool>(atoi(value))); flipTS(); return true; }
  if (strEquals(command, "dbgtouch"))     { config.saveValue(&config.store.dbgtouch, static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "flipscreen")){
    config.saveValue(&config.store.flipscreen, static_cast<bool>(atoi(value)));
    // todo: no event for display flip, why?
    display->flip();
    int32_t d = CLEAR;
    EVT_POST_DATA(YO_CMD_EVENTS, e2int(evt::yo_event_t::displayNewMode), &d, sizeof(d));
    d = PLAYER;
    EVT_POST_DATA(YO_CMD_EVENTS, e2int(evt::yo_event_t::displayNewMode), &d, sizeof(d));
    return true;
  }
  if (strEquals(command, "brightness"))   { if (!config.store.dspon) netserver.requestOnChange(DSPON, 0); config.store.brightness = static_cast<uint8_t>(atoi(value)); config.setBrightness(true); return true; }
  if (strEquals(command, "screenon"))     { config.setDspOn(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "contrast"))     { config.saveValue(&config.store.contrast, static_cast<uint8_t>(atoi(value))); display->setContrast(); return true; }
  if (strEquals(command, "screensaverenabled")){ config.enableScreensaver(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "screensavertimeout")){ config.setScreensaverTimeout(static_cast<uint16_t>(atoi(value))); return true; }
  if (strEquals(command, "screensaverblank"))  { config.setScreensaverBlank(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "screensaverplayingenabled")){ config.setScreensaverPlayingEnabled(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "screensaverplayingtimeout")){ config.setScreensaverPlayingTimeout(static_cast<uint16_t>(atoi(value))); return true; }
  if (strEquals(command, "screensaverplayingblank"))  { config.setScreensaverPlayingBlank(static_cast<bool>(atoi(value))); return true; }
  
  if (strEquals(command, "volsteps"))         { config.saveValue(&config.store.volsteps, static_cast<uint8_t>(atoi(value))); return true; }
  if (strEquals(command, "encacc"))  { setEncAcceleration(static_cast<uint16_t>(atoi(value))); return true; }
  if (strEquals(command, "irtlp"))            { setIRTolerance(static_cast<uint8_t>(atoi(value))); return true; }
  if (strEquals(command, "oneclickswitching")){ config.saveValue(&config.store.skipPlaylistUpDown, static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "showweather"))      { config.setShowweather(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "lat"))              { config.saveValue(config.store.weatherlat, value, 10, false); return true; }
  if (strEquals(command, "lon"))              { config.saveValue(config.store.weatherlon, value, 10, false); return true; }
  if (strEquals(command, "key"))              { config.setWeatherKey(value); return true; }
  //<-----TODO
  if (strEquals(command, "volume"))  { player->setVolume(static_cast<uint8_t>(atoi(value))); return true; }
  if (strEquals(command, "sdpos"))   { config.setSDpos(static_cast<uint32_t>(atoi(value))); return true; }
  if (strEquals(command, "snuffle")) { config.setSnuffle(strcmp(value, "true") == 0); return true; }
  if (strEquals(command, "reboot"))  { ESP.restart(); return true; }
  if (strEquals(command, "format"))  { LittleFS.format(); ESP.restart(); return true; }
  if (strEquals(command, "submitplaylist"))  { return true; }
  
#if IR_PIN!=255
  if (strEquals(command, "irbtn"))  { config.setIrBtn(atoi(value)); return true; }
  if (strEquals(command, "chkid"))  { config.irchck = static_cast<uint8_t>(atoi(value)); return true; }
  if (strEquals(command, "irclr"))  { config.ircodes.irVals[config.irindex][static_cast<uint8_t>(atoi(value))] = 0; return true; }
#endif
  if (strEquals(command, "reset"))  { config.resetSystem(value, cid); return true; }
  
  if (strEquals(command, "smartstart")){ uint8_t ss = atoi(value) == 1 ? 1 : 2; if (!player->isRunning() && ss == 1) ss = 0; config.setSmartStart(ss); return true; }

  if (strEquals(command, "vumeter")){
    config.saveValue(&config.store.vumeter, static_cast<bool>(atoi(value)));
    EVT_POST(YO_CMD_EVENTS, e2int(evt::yo_event_t::displayShowVUMeter));
    return true;
  }

  if (strEquals(command, "softap"))    { config.saveValue(&config.store.softapdelay, static_cast<uint8_t>(atoi(value))); return true; }
  if (strEquals(command, "mdnsname"))  { config.saveValue(config.store.mdnsname, value, MDNS_LENGTH); return true; }
  if (strEquals(command, "rebootmdns")){
    char buf[MDNS_LENGTH*2];
    if(strlen(config.store.mdnsname)>0) snprintf(buf, MDNS_LENGTH*2, "{\"redirect\": \"http://%s.local\"}", config.store.mdnsname);
    else snprintf(buf, MDNS_LENGTH*2, "{\"redirect\": \"http://%s/\"}", WiFi.localIP().toString().c_str());
    //websocket.text(cid, buf); delay(500); ESP.restart();
    return true;
  }
  
  return false;
}





