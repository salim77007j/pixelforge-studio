#pragma once
#include <QString>

// Design-derived palette (sampled from reference mockup 03.png)
namespace Theme {
constexpr const char *WindowBg     = "#EFF1F4";  // window chrome
constexpr const char *PanelBg      = "#F7F8FA";  // dock/panel background
constexpr const char *CardBg       = "#FFFFFF";  // inner cards
constexpr const char *CanvasBg     = "#EAEBEF";  // workspace behind image
constexpr const char *Border       = "#E1E4E9";
constexpr const char *BorderSoft   = "#EAECF0";
constexpr const char *Text         = "#1F2937";
constexpr const char *TextSecondary= "#6B7280";
constexpr const char *Accent       = "#4A90E2";
constexpr const char *AccentDark   = "#3B7CC9";
constexpr const char *AccentChip   = "#E0EFFF";  // active tool chip
constexpr const char *Hover        = "#EDF0F4";
constexpr const char *Pressed      = "#E2E7ED";
constexpr const char *BottomBar    = "#E9EBEE";

QString styleSheet();
}
