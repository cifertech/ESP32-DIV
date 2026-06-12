#pragma once

/** Neo-6M / GNSS: GSV sky plot, GGA/RMC fix + time, GSA DOP + satellites in solution. */
namespace GpsSatelliteScanner {

void session();

} // namespace GpsSatelliteScanner

/** GPS + WiFi scan logger to SD (CSV). Shares UART/NMEA helpers with satellite scanner in gps_satellite.cpp. */
namespace GpsWardriver {

void session();

/** Stops background wardriving (UART/SD) before other GPS features use the same UART. */
void stopBackgroundIfRunning();

/** True while wardriver UI session is active or background wardrive task is running (status bar GPS icon). */
bool statusBarGpsIconActive();

void clearSessionRetry();

bool consumeSessionRetry();

} // namespace GpsWardriver
