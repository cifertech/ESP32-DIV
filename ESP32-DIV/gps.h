#pragma once

namespace GpsSatelliteScanner {

void session();

} 

namespace GpsWardriver {

void session();

void stopBackgroundIfRunning();

bool statusBarGpsIconActive();

} 
