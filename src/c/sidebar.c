#include <pebble.h>
#include <ctype.h>
#include <math.h>
#include "settings.h"
#include "weather.h"
#include "languages.h"
#include "sidebar.h"
#include "sidebar_widgets.h"
#include "brutal_clock.h"

#define V_PADDING_DEFAULT 8
#define V_PADDING_COMPACT 4

GRect screen_rect;

// "private" functions
// layer update callbacks
#ifndef PBL_ROUND
  void updateRectSidebar(Layer *l, GContext* ctx);
#else
  void updateRoundSidebar(Layer *l, GContext* ctx);
#endif

Layer* sidebarLayer;

void Sidebar_init(Window* window) {
  // init the sidebar layer
  screen_rect = layer_get_bounds(window_get_root_layer(window));
  GRect bounds;

  #ifdef PBL_ROUND
    bounds = GRect(screen_rect.size.w - 40, 0, 40, screen_rect.size.h);
  #else
    if(!settings.sidebarOnLeft) {
      bounds = GRect(screen_rect.size.w - ACTION_BAR_WIDTH, 0, ACTION_BAR_WIDTH, screen_rect.size.h);
    } else {
      bounds = GRect(0, 0, ACTION_BAR_WIDTH, screen_rect.size.h);
    }
  #endif

  // init the widgets
  SidebarWidgets_init();

  sidebarLayer = layer_create(bounds);
  layer_add_child(window_get_root_layer(window), sidebarLayer);

  #ifdef PBL_ROUND
    layer_set_update_proc(sidebarLayer, updateRoundSidebar);
  #else
    layer_set_update_proc(sidebarLayer, updateRectSidebar);
  #endif
}

void Sidebar_deinit() {
  layer_destroy(sidebarLayer);

  SidebarWidgets_deinit();
}

void Sidebar_redraw() {
  #ifndef PBL_ROUND
    // reposition the sidebar if needed
    if(!settings.sidebarOnLeft) {
      layer_set_frame(sidebarLayer, GRect(screen_rect.size.w - ACTION_BAR_WIDTH, screen_rect.origin.y, ACTION_BAR_WIDTH, screen_rect.size.h));
    } else {
      layer_set_frame(sidebarLayer, GRect(0, screen_rect.origin.y, ACTION_BAR_WIDTH, screen_rect.size.h));
    }
  #endif

  // redraw the layer
  layer_mark_dirty(sidebarLayer);

  brutal_clock_update_settings();
}

void Sidebar_update_layout(GRect new_bounds) {
  screen_rect = new_bounds;
  Sidebar_redraw();
}

void Sidebar_updateTime(struct tm* timeInfo) {
  SidebarWidgets_updateTime(timeInfo);
}

bool isAutoBatteryShown() {
  if(!settings.disableAutobattery) {
    BatteryChargeState chargeState = battery_state_service_peek();

    if(dynamicSettings.enableAutoBatteryWidget) {
      if(chargeState.charge_percent <= 10 || chargeState.is_charging) {
        return true;
      }
    }
  }

  return false;
}


#ifdef PBL_ROUND

// returns the best candidate widget for replacement by the auto battery
// or the disconnection icon
int getReplacableWidget() {
  if(settings.widgets[0] == EMPTY) {
    return 0;
  } else if(settings.widgets[2] == EMPTY) {
    return 2;
  }

  if(settings.widgets[0] == WEATHER_CURRENT || settings.widgets[0] == WEATHER_FORECAST_TODAY) {
    return 0;
  } else if(settings.widgets[2] == WEATHER_CURRENT || settings.widgets[2] == WEATHER_FORECAST_TODAY) {
    return 2;
  }

  // if we don't have any of those things, just replace the left widget
  return 0;
}

#else

// returns the best candidate widget for replacement by the auto battery
// or the disconnection icon
int getReplacableWidget() {
  // if any widgets are empty, it's an obvious choice
  for(int i = 0; i < 3; i++) {
    if(settings.widgets[i] == EMPTY) {
      return i;
    }
  }

  // are there any bluetooth-enabled widgets? if so, they're the second-best
  // candidates
  for(int i = - 1; i < 3; i++) {
    if(settings.widgets[i] == WEATHER_CURRENT || settings.widgets[i] == WEATHER_FORECAST_TODAY) {
      return i;
    }
  }

  // if we don't have any of those things, just replace the middle widget
  return 1;
}

#endif

#ifdef PBL_ROUND

void updateRoundSidebar(Layer *l, GContext* ctx) {
  GRect layerBounds = layer_get_bounds(l);

  // Build a 360px diameter circle whose visible arc fills
  // the right edge of the screen (PebbleOS Timeline peek
  // aesthetic). layer_get_bounds returns layer-local
  // coordinates so layerBounds.origin is (0,0).
  int diameter = layerBounds.size.h * 2;
  int circle_x = layerBounds.origin.x
                 - diameter
                 + layerBounds.size.w;
  int circle_y = layerBounds.size.h / -2;
  GRect bgBounds = GRect(circle_x - layerBounds.origin.x,
                         circle_y, diameter, diameter);

  SidebarWidgets_updateFonts();

  graphics_context_set_fill_color(ctx, settings.sidebarColor);
  graphics_fill_radial(ctx, bgBounds,
                       GOvalScaleModeFillCircle,
                       100, DEG_TO_TRIGANGLE(0),
                       TRIG_MAX_ANGLE);

  graphics_context_set_text_color(ctx,
                                  settings.sidebarTextColor);

  bool showDisconnectIcon =
        settings.activateDisconnectIcon
        && !bluetooth_connection_service_peek();
  bool showAutoBattery = isAutoBatteryShown();

  SidebarWidgetType topType    = settings.widgets[0];
  SidebarWidgetType bottomType = settings.widgets[2];

  if (showAutoBattery || showDisconnectIcon) {
    int replaceIdx = getReplacableWidget();
    SidebarWidgetType replacement =
        showAutoBattery ? BATTERY_METER : BLUETOOTH_DISCONNECT;
    if (replaceIdx == 0) topType    = replacement;
    else                 bottomType = replacement;
  }

  SidebarWidget topWidget    = getSidebarWidgetByType(topType);
  SidebarWidget bottomWidget =
        getSidebarWidgetByType(bottomType);

  // Nudge widgets slightly left so they stay legible inside
  // the curved fill.
  SidebarWidgets_xOffset = 5;

  int v_padding = 10;
  int topPos    = v_padding;
  int bottomPos = layerBounds.size.h
                  - v_padding
                  - bottomWidget.getHeight();

  topWidget.draw(ctx, topPos);
  bottomWidget.draw(ctx, bottomPos);
}

#else

void updateRectSidebar(Layer *l, GContext* ctx) {
  GRect bounds = layer_get_unobstructed_bounds(l);

  // this ends up being zero on every rectangular platform besides emery
  SidebarWidgets_xOffset = (ACTION_BAR_WIDTH - 30) / 2;

  SidebarWidgets_updateFonts();

  graphics_context_set_fill_color(ctx, settings.sidebarColor);
  graphics_fill_rect(ctx, layer_get_bounds(l), 0, GCornerNone);

  graphics_context_set_text_color(ctx, settings.sidebarTextColor);

  bool showDisconnectIcon = false;
  bool showAutoBattery = isAutoBatteryShown();

  // if the pebble is disconnected and activated, show the disconnect icon
  if(settings.activateDisconnectIcon) {
    showDisconnectIcon = !bluetooth_connection_service_peek();
  }

  SidebarWidget displayWidgets[3];

  displayWidgets[0] = getSidebarWidgetByType(settings.widgets[0]);
  displayWidgets[1] = getSidebarWidgetByType(settings.widgets[1]);
  displayWidgets[2] = getSidebarWidgetByType(settings.widgets[2]);

  // do we need to replace a widget?
  // if so, determine which widget should be replaced
  if(showAutoBattery || showDisconnectIcon) {
    int widget_to_replace = getReplacableWidget();

    if(showAutoBattery) {
      displayWidgets[widget_to_replace] = getSidebarWidgetByType(BATTERY_METER);
    } else if(showDisconnectIcon) {
      displayWidgets[widget_to_replace] = getSidebarWidgetByType(BLUETOOTH_DISCONNECT);
    }
  }

  // if the widgets are too tall, enable "compact mode"
  int compact_mode_threshold = bounds.size.h - V_PADDING_DEFAULT * 2 - 3;
  int v_padding = V_PADDING_DEFAULT;

  SidebarWidgets_useCompactMode = false; // ensure that we compare the non-compacted heights
  int totalHeight = displayWidgets[0].getHeight() + displayWidgets[1].getHeight() + displayWidgets[2].getHeight();
  SidebarWidgets_useCompactMode = (totalHeight > compact_mode_threshold);
  // printf("Total Height: %i, Threshold: %i", totalHeight, compact_mode_threshold);

  // now that they have been compacted, check if they fit a second time,
  // if they still don't fit, our only choice is MURDER (of the middle widget)
  totalHeight = displayWidgets[0].getHeight() + displayWidgets[1].getHeight() + displayWidgets[2].getHeight();
  bool hide_middle_widget = (totalHeight > compact_mode_threshold);
  // printf("Compact Mode Enabled. Total Height: %i, Threshold: %i", totalHeight, compact_mode_threshold);

  // still doesn't fit? try compacting the vertical padding
  totalHeight = displayWidgets[0].getHeight() + displayWidgets[2].getHeight();
  if(totalHeight > compact_mode_threshold) {
    v_padding = V_PADDING_COMPACT;
  }

  // calculate the three widget positions
  int topWidgetPos = v_padding;
  int lowerWidgetPos = bounds.size.h - v_padding - displayWidgets[2].getHeight();

  // vertically center the middle widget using MATH
  int middleWidgetPos = ((lowerWidgetPos - displayWidgets[1].getHeight()) + (topWidgetPos + displayWidgets[0].getHeight())) / 2;

  // draw the widgets
  displayWidgets[0].draw(ctx, topWidgetPos);
  if(!hide_middle_widget) {
    displayWidgets[1].draw(ctx, middleWidgetPos);
  }
  displayWidgets[2].draw(ctx, lowerWidgetPos);
}

#endif