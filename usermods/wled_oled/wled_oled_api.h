#pragma once
// ============================================================
// API partagée entre wled_oled et wled_buttons
// Permet à wled_buttons de déclencher la vue "état LED" sur l'écran
// ============================================================

// Défini dans wled_oled.cpp
// Quand millis() < g_showLedStatusUntil, wled_oled affiche la vue LED
extern uint32_t g_showLedStatusUntil;
