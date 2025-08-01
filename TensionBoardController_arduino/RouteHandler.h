#ifndef ROUTE_HANDLER_H
#define ROUTE_HANDLER_H

#include <vector>
#include "ColorSchemes.h"

void parseRouteData(const uint8_t* data, size_t length, std::vector<Hold>& holds);
void updateRouteDisplay(const std::vector<Hold>& holds);

#endif // ROUTE_HANDLER_H