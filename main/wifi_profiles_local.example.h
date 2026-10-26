/*
 * Default Wi-Fi profiles. Copy this file to wifi_profiles_local.h (git-ignored, so passwords
 * stay out of the public repo) and fill it in. The defaults are used until a profile is edited
 * on the device (Settings > Wi-Fi > Edit), which saves it in NVS. A build without
 * wifi_profiles_local.h uses this file: the profiles start empty.
 */
#pragma once

#define WIFI_WORK_SSID      ""
#define WIFI_WORK_PASSWORD  ""

#define WIFI_HOME2_SSID     ""
#define WIFI_HOME2_PASSWORD ""

#define WIFI_HOME3_SSID     ""
#define WIFI_HOME3_PASSWORD ""

#define WIFI_HOTSPOT_SSID     ""
#define WIFI_HOTSPOT_PASSWORD ""
