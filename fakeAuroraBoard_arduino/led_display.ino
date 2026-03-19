// led_display.ino — LED rendering, animation, overlap blending, and startup sequence

// Convert a wavelength (400-700nm) to an RGB CRGB value using piecewise linear approximation
CRGB wavelengthToRGB(float wavelength) {
    float r = 0.0f, g = 0.0f, b = 0.0f;

    if (wavelength >= 400 && wavelength < 440) {
        r = -(wavelength - 440) / (440 - 400);
        b = 1.0f;
    } else if (wavelength >= 440 && wavelength < 490) {
        g = (wavelength - 440) / (490 - 440);
        b = 1.0f;
    } else if (wavelength >= 490 && wavelength < 510) {
        g = 1.0f;
        b = -(wavelength - 510) / (510 - 490);
    } else if (wavelength >= 510 && wavelength < 580) {
        r = (wavelength - 510) / (580 - 510);
        g = 1.0f;
    } else if (wavelength >= 580 && wavelength < 645) {
        r = 1.0f;
        g = -(wavelength - 645) / (645 - 580);
    } else if (wavelength >= 645 && wavelength <= 700) {
        r = 1.0f;
    }

    return CRGB(
        static_cast<uint8_t>(constrain(r, 0.0f, 1.0f) * 255),
        static_cast<uint8_t>(constrain(g, 0.0f, 1.0f) * 255),
        static_cast<uint8_t>(constrain(b, 0.0f, 1.0f) * 255)
    );
}

// Simple color blend helper for overlap animation (safe for uint8_t via int16_t cast)
CRGB blendColors(const CRGB& c1, const CRGB& c2, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    uint8_t r = static_cast<uint8_t>(c1.r + (int16_t)(c2.r - c1.r) * t);
    uint8_t g = static_cast<uint8_t>(c1.g + (int16_t)(c2.g - c1.g) * t);
    uint8_t b = static_cast<uint8_t>(c1.b + (int16_t)(c2.b - c1.b) * t);
    return CRGB(r, g, b);
}

// Recompute overlapping holds and route2-only holds between route1 and route2
void updateOverlapState() {
    overlappingHolds.clear();
    hasOverlap = false;

    if (route1Holds.empty() || route2Holds.empty()) {
        route2OnlyHolds.clear();
        return;
    }

    // Build a position lookup from route1 for O(n+m) instead of O(n*m)
    static bool route1PosPresent[NUM_LEDS];
    memset(route1PosPresent, 0, sizeof(route1PosPresent));

    for (const Hold& h1 : route1Holds) {
        if (h1.position < NUM_LEDS) {
            route1PosPresent[h1.position] = true;
        }
    }

    // Find overlapping holds and pre-compute route2-only holds
    route2OnlyHolds.clear();
    for (const Hold& h2 : route2Holds) {
        if (h2.position >= NUM_LEDS) continue;
        if (route1PosPresent[h2.position]) {
            // Overlapping — find route1's color for this position
            for (const Hold& h1 : route1Holds) {
                if (h1.position == h2.position) {
                    OverlapInfo info;
                    info.position = h1.position;
                    info.colorRoute1 = CRGB(h1.r, h1.g, h1.b);
                    info.colorRoute2 = CRGB(h2.r, h2.g, h2.b);
                    overlappingHolds.push_back(info);
                    break;
                }
            }
        } else {
            route2OnlyHolds.push_back(h2);
        }
    }

    hasOverlap = !overlappingHolds.empty();
    if (hasOverlap) {
        overlapAnimStartMillis = millis();
    }
}

// Combines both route data and renders to LEDs.
// Overlapping holds animate between route1 and route2 colors.
void updateBoardState() {
    // both off, just clear
    if (!route1On && !route2On) {
        FastLED.clear();
        FastLED.show();
        return;
    }

    // if only one route is on, send it to the board
    if (route1On && !route2On) {
        setBoardLEDs(route1Holds);
    }
    else if (route2On && !route1On) {
        setBoardLEDs(route2Holds);
    }
    // if both routes are on, calculate union of both routes
    else if (route1On && route2On) {
        // If there is no overlap, fall back to existing combined behavior
        if (!hasOverlap) {
            std::vector<Hold> boardState;
            boardState.insert(boardState.end(), route1Holds.begin(), route1Holds.end());
            boardState.insert(boardState.end(), route2Holds.begin(), route2Holds.end());
            setBoardLEDs(boardState);
        } else {
            // Overlap present: animate overlapping holds with hold-fade pattern
            // Pattern: Hold color1 (750ms) -> Fade to color2 (350ms) -> Hold color2 (750ms) -> Fade to color1 (350ms)
            const unsigned long periodMs = 2200UL;
            const unsigned long hold1Ms = 750UL;
            const unsigned long fade1Ms = 350UL;
            const unsigned long hold2Ms = 750UL;
            const unsigned long fade2Ms = 350UL;

            unsigned long now = millis();
            unsigned long elapsed = (now - overlapAnimStartMillis) % periodMs;
            float t;

            if (elapsed < hold1Ms) {
                t = 0.0f;
            } else if (elapsed < hold1Ms + fade1Ms) {
                unsigned long fadeElapsed = elapsed - hold1Ms;
                t = static_cast<float>(fadeElapsed) / static_cast<float>(fade1Ms);
            } else if (elapsed < hold1Ms + fade1Ms + hold2Ms) {
                t = 1.0f;
            } else {
                unsigned long fadeElapsed = elapsed - (hold1Ms + fade1Ms + hold2Ms);
                t = 1.0f - (static_cast<float>(fadeElapsed) / static_cast<float>(fade2Ms));
            }

            FastLED.clear();

            // Draw all route1 holds (base colors)
            for (const Hold& h1 : route1Holds) {
                if (h1.position < NUM_LEDS) {
                    leds[h1.position] = CRGB(h1.r, h1.g, h1.b);
                }
            }

            // Draw route2-only holds (pre-computed, no per-frame lookup needed)
            for (const Hold& h2 : route2OnlyHolds) {
                if (h2.position < NUM_LEDS) {
                    leds[h2.position] = CRGB(h2.r, h2.g, h2.b);
                }
            }

            // Blend overlapping holds between route1 and route2 colors
            for (const OverlapInfo& info : overlappingHolds) {
                if (info.position < NUM_LEDS) {
                    leds[info.position] = blendColors(info.colorRoute1, info.colorRoute2, t);
                }
            }

            FastLED.show();
        }
    }
}

// Render a single set of holds to the LED strip
void setBoardLEDs(const std::vector<Hold>& boardState) {
    FastLED.clear();
    for (const Hold& hold : boardState) {
        if (hold.position < NUM_LEDS) {
            leds[hold.position] = CRGB(hold.r, hold.g, hold.b);
        }
    }
    FastLED.show();
}

// Mirror a hold position horizontally on the 17-column board
int mirrorPosition(int pos) {
    int remainder = pos % 29;
    int col = (pos - remainder) / 29;
    int new_col = 16 - col;
    if (remainder > 14) {
        new_col -= 1;
    }
    int new_position = new_col * 29 + remainder;
    return new_position;
}

void mirrorCurrentLane() {
    if (currentLane == 0) {
        for (size_t i = 0; i < route1Holds.size(); i++) {
            route1Holds[i].position = mirrorPosition(route1Holds[i].position);
        }
    } else {
        for (size_t i = 0; i < route2Holds.size(); i++) {
            route2Holds[i].position = mirrorPosition(route2Holds[i].position);
        }
    }
    updateOverlapState();
    updateBoardState();
}

// Startup animation: spectrum sweep (violet -> red -> violet) ending on purple
void startupLEDs() {
    if (NUM_LEDS <= 0) return;

    const int totalSteps = 50;
    const int delayMs = 5;
    const float startWl = 400.0f;
    const float endWl = 700.0f;
    const float range = endWl - startWl;

    // Forward sweep: violet -> red
    for (int step = 0; step < totalSteps; step++) {
        float wl = startWl + (step * range / totalSteps);
        fill_solid(leds, NUM_LEDS, wavelengthToRGB(wl));
        FastLED.show();
        delay(delayMs);
    }

    // Reverse sweep: red -> violet
    for (int step = totalSteps - 1; step >= 0; step--) {
        float wl = startWl + (step * range / totalSteps);
        fill_solid(leds, NUM_LEDS, wavelengthToRGB(wl));
        FastLED.show();
        delay(delayMs);
    }

    // Hold purple
    fill_solid(leds, NUM_LEDS, CRGB(128, 0, 128));
    FastLED.show();
}
