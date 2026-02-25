#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// moon.h — Moon phase calculation (no network required)
//
// Uses the known new moon of 2000-01-06 18:14 UTC as an epoch reference and
// the mean synodic period (29.530589 days) to compute the current lunar age.
// Accuracy: ±1 day — sufficient for display purposes.
// ─────────────────────────────────────────────────────────────────────────────
#include <time.h>
#include <math.h>

// Returns a short phase name for the given UTC Unix timestamp.
static const char* moon_phase_name(time_t t) {
    // Known new moon: 2000-01-06 18:14 UTC  (Unix epoch 946,685,640)
    const double KNOWN_NEW_MOON_SEC = 946685640.0;
    const double SYNODIC_MONTH_SEC  = 29.530588853 * 86400.0;

    double age = fmod((double)t - KNOWN_NEW_MOON_SEC, SYNODIC_MONTH_SEC)
                 / SYNODIC_MONTH_SEC;
    if (age < 0.0) age += 1.0;

    if (age < 0.025 || age >= 0.975) return "New Moon";
    if (age < 0.225)                  return "Waxing Crescent";
    if (age < 0.275)                  return "First Quarter";
    if (age < 0.475)                  return "Waxing Gibbous";
    if (age < 0.525)                  return "Full Moon";
    if (age < 0.725)                  return "Waning Gibbous";
    if (age < 0.775)                  return "Last Quarter";
    return "Waning Crescent";
}
