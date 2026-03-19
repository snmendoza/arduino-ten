// route_history.ino — Per-lane route history buffer (FIFO, fixed-depth)
//
// Each lane maintains a fixed-size history of previous routes.
// historyPush() saves the current route before it's overwritten by a new one.
// historyNavigate() lets the user scroll through history via IR remote.
// historyGetDisplayRoute() returns whichever route should be displayed
// (live or a history snapshot).

// Convert a vector<Hold> to a fixed-size RouteSnapshot
RouteSnapshot snapshotFromVector(const std::vector<Hold>& route) {
    RouteSnapshot snap;
    snap.count = (route.size() > MAX_HOLDS_PER_ROUTE)
                 ? MAX_HOLDS_PER_ROUTE
                 : static_cast<uint8_t>(route.size());
    for (uint8_t i = 0; i < snap.count; i++) {
        snap.holds[i] = route[i];
    }
    return snap;
}

// Convert a RouteSnapshot back to a vector<Hold>
void vectorFromSnapshot(const RouteSnapshot& snap, std::vector<Hold>& out) {
    out.clear();
    out.reserve(snap.count);
    for (uint8_t i = 0; i < snap.count; i++) {
        out.push_back(snap.holds[i]);
    }
}

// Push the current route into history for a lane (call BEFORE overwriting the live route).
// Shifts everything down; oldest entry is discarded if full.
void historyPush(uint8_t lane, const std::vector<Hold>& route) {
    if (lane >= NUM_LANES || route.empty()) return;

    LaneHistory& hist = laneHistory[lane];

    // Shift existing entries down (0→1, 1→2, ...)
    uint8_t moveCount = (hist.depth < MAX_HISTORY_DEPTH)
                        ? hist.depth
                        : MAX_HISTORY_DEPTH - 1;
    for (int8_t i = moveCount; i > 0; i--) {
        hist.slots[i] = hist.slots[i - 1];
    }

    // Insert current route at slot 0 (most recent history)
    hist.slots[0] = snapshotFromVector(route);

    if (hist.depth < MAX_HISTORY_DEPTH) {
        hist.depth++;
    }

    // Reset browsing to live view
    hist.browseIndex = -1;

    // Mark flash as needing a save
    flashDirty = true;
}

// Navigate history for a lane.
// direction: -1 = older (left arrow), +1 = newer (right arrow)
// Returns true if the display route changed.
bool historyNavigate(uint8_t lane, int8_t direction) {
    if (lane >= NUM_LANES) return false;

    LaneHistory& hist = laneHistory[lane];
    int8_t newIndex = hist.browseIndex;

    if (direction < 0) {
        // Go older
        if (newIndex < 0) {
            newIndex = 0;  // live → most recent history
        } else if (newIndex < hist.depth - 1) {
            newIndex++;
        } else {
            return false;  // already at oldest, stop
        }
    } else {
        // Go newer
        if (newIndex > 0) {
            newIndex--;
        } else if (newIndex == 0) {
            newIndex = -1;  // back to live
        } else {
            return false;  // already at live
        }
    }

    if (newIndex == hist.browseIndex) return false;
    hist.browseIndex = newIndex;
    return true;
}

// Get the route that should be displayed for a lane.
// If browsing history, returns that snapshot; otherwise returns the live route.
// Caller must NOT hold a reference across a push — the vector is rebuilt each call
// when browsing history. Returns the live route reference when not browsing.
static std::vector<Hold> historyTempRoute;  // reusable buffer to avoid repeated allocation

const std::vector<Hold>& historyGetDisplayRoute(uint8_t lane) {
    if (lane >= NUM_LANES) {
        historyTempRoute.clear();
        return historyTempRoute;
    }

    LaneHistory& hist = laneHistory[lane];

    if (hist.browseIndex < 0) {
        // Live mode — return the actual live route
        return (lane == 0) ? route1Holds : route2Holds;
    }

    // Browsing history — unpack the snapshot
    if (hist.browseIndex < hist.depth) {
        vectorFromSnapshot(hist.slots[hist.browseIndex], historyTempRoute);
    } else {
        historyTempRoute.clear();
    }
    return historyTempRoute;
}

// Reset browsing back to live view (e.g. when a new route arrives)
void historyResetBrowsing(uint8_t lane) {
    if (lane < NUM_LANES) {
        laneHistory[lane].browseIndex = -1;
    }
}
