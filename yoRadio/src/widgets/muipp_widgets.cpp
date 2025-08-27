#if __has_include("Arduino_GFX.h")
#include "../gfx_engine.h"
#include "agfx.h"
#include "locale/l10n.h"
#include "../../core/log.h"

/**************************
      CLOCK WIDGET
 **************************/

bool ClockWidget::refresh_req() const {
  return std::time({}) != _last;
}

void ClockWidget::render(const MuiItem* parent, void* r){
  std::time_t time = std::time({});
  auto t = std::localtime(&time);
  _drawTime(t, static_cast<Arduino_GFX*>(r));
  _last = time;
  if (!dcfg.print_date) return;
  // check if date has changed
  struct tm cur_date = *t;
  t = std::localtime(&_last_date);
  _drawDate(&cur_date, static_cast<Arduino_GFX*>(r));
  // draw date only if year of year day has changed (can't be applied here)
//  if ( cfg.print_date && (t->tm_yday != cur_date.tm_yday) || (t->tm_year != cur_date.tm_year) ){
//    _drawDate(&cur_date, static_cast<Arduino_GFX*>(r));
//    _last_date = time;
//  }
  //_reconfig(&cur_date);
};

void ClockWidget::_drawTime(tm* t, Arduino_GFX* dsp){
  _clear_clk(dsp);
  char buff[std::size("hh:mm")];
  
  std::strftime(std::data(buff), std::size(buff), "%R", t);    // "%R" equivalent to "%H:%M", t->tm_sec % 2 ? "%R" : "%H %M" would blink semicolon
  LOGV(T_Clock, println, buff );
  // recalculate area for clock and save it to be cleared later
  if (cfg.font_hours)
    dsp->setFont(cfg.font_hours);
  else if (cfg.font_gfx_hours)
    dsp->setFont(cfg.font_gfx_hours);

  dsp->setTextSize(cfg.font_hours_size);
  dsp->getTextBounds(buff, cfg.x, cfg.y, &_time_block_x, &_time_block_y, &_time_block_w, &_time_block_h);

  if (t->tm_sec % 2){   // draw ':'
    if (cfg.font_hours)
      gfxDrawText(dsp, cfg.x, cfg.y, buff, cfg.color, cfg.font_gfx_hours, cfg.font_hours_size);
    else if (cfg.font_gfx_hours)
      gfxDrawText(dsp, cfg.x, cfg.y, buff, cfg.color, cfg.font_hours, cfg.font_hours_size);
  } else {
    // let's draw time in parts so that ':' is drawn with background color maintaining same area dimensions
    std::strftime(std::data(buff), std::size(buff), "%H", t);
    if (cfg.font_hours)
      gfxDrawText(dsp, cfg.x, cfg.y, buff, cfg.color, cfg.font_hours, cfg.font_hours_size);
    else if (cfg.font_gfx_hours)
      gfxDrawText(dsp, cfg.x, cfg.y, buff, cfg.color, cfg.font_gfx_hours, cfg.font_hours_size);

    // write semicolon
    if (cfg.font_hours)
      gfxDrawText(dsp, dsp->getCursorX(), dsp->getCursorY(), ":", cfg.bgcolor, cfg.font_hours, cfg.font_hours_size);
    else if (cfg.font_gfx_hours)
      gfxDrawText(dsp, dsp->getCursorX(), dsp->getCursorY(), ":", cfg.bgcolor, cfg.font_gfx_hours, cfg.font_hours_size);
    //    dsp->drawChar(dsp->getCursorX(), dsp->getCursorY(), 0x3a /* ':' */, RGB565_RED, RGB565_RED);
    // write minutes
    std::strftime(std::data(buff), std::size(buff), "%M", t);
    if (cfg.font_hours)
      gfxDrawText(dsp, dsp->getCursorX(), dsp->getCursorY(), buff, cfg.color, cfg.font_hours, cfg.font_hours_size);
    else if (cfg.font_gfx_hours)
      gfxDrawText(dsp, dsp->getCursorX(), dsp->getCursorY(), buff, cfg.color, cfg.font_gfx_hours, cfg.font_hours_size);
  }

  if (!cfg.print_seconds) return;

  // make seconds
  std::strftime(std::data(buff), std::size(buff), "%S", t);
  if (cfg.font_seconds)
    dsp->setFont(cfg.font_seconds);
  else if (cfg.font_gfx_seconds)
    dsp->setFont(cfg.font_gfx_seconds);

  dsp->setTextSize(cfg.font_seconds_size);
  // recalculate area for clock and save it to be cleared later
  dsp->getTextBounds(buff, dsp->getCursorX() + cfg.sec_offset_x, dsp->getCursorY() + cfg.sec_offset_y, &_seconds_block_x, &_seconds_block_y, &_seconds_block_w, &_seconds_block_h);
  // print seconds
  if (cfg.font_seconds)
    gfxDrawText(dsp, dsp->getCursorX() + cfg.sec_offset_x, dsp->getCursorY() + cfg.sec_offset_y, buff, cfg.color, cfg.font_seconds, cfg.font_seconds_size);
  else if (cfg.font_gfx_seconds)
    gfxDrawText(dsp, dsp->getCursorX() + cfg.sec_offset_x, dsp->getCursorY() + cfg.sec_offset_y, buff, cfg.color, cfg.font_gfx_seconds, cfg.font_seconds_size);
}

void ClockWidget::_drawDate(tm* t, Arduino_GFX* dsp){
  dsp->fillRect(_date_block_x, _date_block_y, _date_block_w, _date_block_h, dcfg.bgcolor);
  char buff[100];
  // date format "1 января / понедельник"
  snprintf(buff, std::size(buff), "%d %s / %s", t->tm_mday, mnths[t->tm_mon], dcfg.dow_short ? dowf[t->tm_wday] : dowf[t->tm_wday]);
  //LOGD(T_Clock, println, buff);
  // recalculate area for clock and save it to be cleared later
  dsp->setFont(dcfg.font);
  dsp->setTextSize(dcfg.font_date_size);
  dsp->getTextBounds(buff, dcfg.x, dcfg.y, &_date_block_x, &_date_block_y, &_date_block_w, &_date_block_h);

  // draw date
  gfxDrawText(dsp, dcfg.x, dcfg.y, buff, dcfg.color, FONT_DATE, dcfg.font_date_size);
}

void ClockWidget::_clear_clk(Arduino_GFX* dsp){
  // Очищаем область под текущими часами
  dsp->fillRect(_time_block_x, _time_block_y, _time_block_w, _time_block_h, cfg.bgcolor);  // RGB565_DARKGREY); //
  if (cfg.print_seconds)
    dsp->fillRect(_seconds_block_x, _seconds_block_y, _seconds_block_w, _seconds_block_h, cfg.bgcolor); // RGB565_DARKGREEN); //config.theme.background);
}

/*
void ClockWidget::_reconfig(tm* t){
  // todo: this is a wrong place for this code, should be in display/dspcore
  #if LIGHT_SENSOR!=255
  if(config.store.dspon) {
    config.store.brightness = AUTOBACKLIGHT(analogRead(LIGHT_SENSOR));
    config.setBrightness();
  }
#endif
  if (config.isScreensaver && t->tm_sec == 0){
    #ifdef GXCLOCKFONT
      uint16_t ft=static_cast<uint16_t>(random(TFT_FRAMEWDT, (dsp->height()-dsp->plItemHeight-TFT_FRAMEWDT*2-clockConf.textsize)));
    #else
      uint16_t ft=static_cast<uint16_t>(random(TFT_FRAMEWDT+clockConf.textsize, (dsp->height()-dsp->plItemHeight-TFT_FRAMEWDT*2)));
    #endif
    moveTo({_config.left, ft, 0});
  }
}
*/

MuiItem_Bitrate_Widget::MuiItem_Bitrate_Widget(muiItemId id,
  int16_t x, int16_t y,
  uint16_t w, uint16_t h,
  AGFX_text_t tcfg)
    : MuiItem_Uncontrollable(id), _x(x), _y(y), _w(w), _h(h),  _tcfg(tcfg) {

  // bitrate state change event picker
  esp_event_handler_instance_register_with(evt::get_hndlr(), YO_CHG_STATE_EVENTS, e2int(evt::yo_event_t::playerAudioInfo),
    [](void* self, esp_event_base_t base, int32_t id, void* data){ static_cast<MuiItem_Bitrate_Widget*>(self)->setInfo(static_cast<audio_info_t*>(data)); },
    this, &_hdlr_chg_evt
  );

};

MuiItem_Bitrate_Widget::~MuiItem_Bitrate_Widget(){
  esp_event_handler_instance_unregister_with(evt::get_hndlr(), YO_CMD_EVENTS, e2int(evt::yo_event_t::playerAudioInfo), _hdlr_chg_evt);
}

void MuiItem_Bitrate_Widget::render(const MuiItem* parent, void* r){
  Arduino_GFX* g = static_cast<Arduino_GFX*>(r);

  g->fillRect(_x, _y, _w, _h, _tcfg.bgcolor);
#ifdef BITRATE_WDGT_RADIUS
  // draw rounded rect
  g->drawRoundRect(_x, _y, _w, _h, BITRATE_WDGT_RADIUS, _tcfg.color);
  g->fillRoundRect(_x, _y + _h/2, _w, _h/2, BITRATE_WDGT_RADIUS, _tcfg.color);
#else
  g->drawRect(_x, _y, _w, _h, _tcfg.color);
  g->fillRect(_x, _y + _h/2, _w, _h/2, _tcfg.color);
#endif
  g->setFont(_tcfg.font);
  g->setTextSize(_tcfg.font_size);
  g->setTextColor(_tcfg.color, _tcfg.bgcolor);
  char buff[16];
  snprintf(buff, 16, bitrateFmt, _info.bitRate);
  // text block
  int16_t  xx{0}, yy{0};
  uint16_t ww{0}, hh{0};
  g->getTextBounds(buff, _x, _y + _h, &xx, &yy, &ww, &hh);
  g->setCursor(_x + _w/2 - ww/2, (_y + _h/4) + hh/2);
  g->printf(bitrateFmt, _info.bitRate);

  std::string_view a(_info.codecName ? _info.codecName : "n/a");
  g->getTextBounds(a.data(), _x, _y, &xx, &yy, &ww, &hh);
  
  // align text by h/v center 
  g->setCursor(_x + _w/2 - ww/2, _y + _h - _h/4 + hh/2);
  g->setTextColor(_tcfg.bgcolor);
  g->print(a.data());
  _pending = false;
}

#endif  // #if __has_include("Arduino_GFX.h")
