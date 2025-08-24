#pragma once
#include "320x480_agfx_defines.h"
#include "../widgets/muipp_widgets.hpp"

// this file contains static structs for widget's configs related to display layout

#ifndef TFT_WIDTH
#define TFT_WIDTH 320
#endif

// Clock
static const clock_time_cfg_t clock_cfg{
  0, 100,   // clock print position
  &FONT_CLOCK_H, &FONT_CLOCK_S,
  FONT_DEFAULT_COLOR, 0,    // color, bgcolor;
  1, 1,       // font_hours_size, font_seconds_size;
  0, -15,     // offset for seconds print after minite's position
  true, true  // print_seconds, print_date;
};

// Date
static const clock_date_cfg_t date_cfg{
  0, 150,                 // date print position
  FONT_DATE,
  FONT_DEFAULT_COLOR, 0,  //  uint16_t color, bgcolor;
  2,                      // int font_date_size;
  true, false, false      //print_date, dow_short, month_short;
};

// ************************
// Device status text widet
#define TITLE_STATUS_POSITION_X DSP_WIDTH/2
#define TITLE_STATUS_POSITION_Y 10

static constexpr AGFX_text_t title_status_cfg {
  RGB565_WHITE, 0,        // color, bgcolor;
  FONT_DEFAULT_U8G2,
  1,                      // font size
  muipp::text_align_t::center, muipp::text_align_t::baseline, //  muipp::text_align_t halign{muipp::text_align_t::left}, valign{muipp::text_align_t::baseline};
  true                    // transp_bg
};

// ************************
// Scroller 1 - Station
#define SCROLLER_STATION_POSITION_X 0
#define SCROLLER_STATION_POSITION_Y 15
#define SCROLLER_STATION_POSITION_W DSP_WIDTH
#define SCROLLER_STATION_POSITION_H 20
#define SCROLLER_STATION_SPEED      40    // pix per second

static constexpr AGFX_text_t scroller_station_cfg {
  RGB565_OLIVE, 0,        // color, bgcolor;
  FONT_MEDIUM_U8G2,
  1,                      // font size
  muipp::text_align_t::left, muipp::text_align_t::baseline, //  muipp::text_align_t halign{muipp::text_align_t::left}, valign{muipp::text_align_t::baseline};
  false                             // transp_bg
};

// ************************
// Scroller 2 - Track Title
#define SCROLLER_TRACK_POSITION_X 0
#define SCROLLER_TRACK_POSITION_Y 40
#define SCROLLER_TRACK_POSITION_W TFT_WIDTH
#define SCROLLER_TRACK_POSITION_H 20
#define SCROLLER_TRACK_SPEED      40    // pix per second

static constexpr AGFX_text_t scroller_track_cfg {
  RGB565_CYAN, 0,        // color, bgcolor;
  FONT_MEDIUM_U8G2,
  1,                      // font size
  muipp::text_align_t::left, muipp::text_align_t::baseline, //  muipp::text_align_t halign{muipp::text_align_t::left}, valign{muipp::text_align_t::baseline};
  false                             // transp_bg
};

// *************************
// Bitrate / format
#define BITRATE_WDGT_POSITION_X 0
#define BITRATE_WDGT_POSITION_Y 160
#define BITRATE_WDGT_W 60
#define BITRATE_WDGT_H 50
#define BITRATE_WDGT_RADIUS 10     // leave undefined for rectangular box
static const char        bitrateFmt[]    = "%uk";
static constexpr AGFX_text_t bitrate_wdgt_cfg {
  RGB565_CYAN, 0,         // color, bgcolor;
  FONT_DEFAULT_U8G2,
  2,                      // font size
  muipp::text_align_t::left, muipp::text_align_t::baseline, //  muipp::text_align_t halign{muipp::text_align_t::left}, valign{muipp::text_align_t::baseline};
  false                             // transp_bg
};



/* STRINGS  */
static const char         numtxtFmt[]    = "%d";
static const char           rssiFmt[]    = "WiFi %d";
static const char          iptxtFmt[]    = "%s";
static const char         voltxtFmt[]    = "\023\025%d";

