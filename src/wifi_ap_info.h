/* @@@LICENSE
*
* Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

/**
 * @file wifi_ap_info.h
 *
 * @brief Details of the access point the device is associated with, for the
 * "apInfo" object of com.webos.service.wifi/getstatus.
 *
 * ConnMan does not carry these. Its Strength is a normalised 0..100 and it
 * reports nothing about the channel the AP operates on, so the values here are
 * read from wpa_supplicant, which is where ConnMan itself gets its scan
 * results from.
 */

#ifndef _WIFI_AP_INFO_H_
#define _WIFI_AP_INFO_H_

#include <glib.h>
#include <stdbool.h>

typedef struct wifi_ap_info
{
	/** Lower case colon separated MAC of the associated AP. */
	gchar *bssid;
	/** Operating frequency in MHz, 0 when unknown. */
	guint16 frequency;
	/** Channel number derived from the frequency, 0 when unknown. */
	guint16 channel;
	/**
	 * Operating channel width in MHz: 20, 40, 80 or 160. 8080 encodes the
	 * 80+80 case. 0 when it could not be determined.
	 */
	guint16 width;
	/** Signal in dBm, as opposed to ConnMan's normalised percentage. */
	gint16 signal_level;
	/** True when at least one field above was filled in. */
	bool valid;
} wifi_ap_info_t;

/**
 * @brief Fill in the details of the AP the given interface is associated with.
 *
 * @param ifname Interface to ask about, e.g. "wlan0".
 * @param info Filled in on success; caller frees with wifi_ap_info_free().
 *
 * @return true when wpa_supplicant answered and the device is associated.
 */
bool wifi_ap_info_get(const gchar *ifname, wifi_ap_info_t *info);

/**
 * @brief Release anything wifi_ap_info_get() allocated.
 */
void wifi_ap_info_free(wifi_ap_info_t *info);

#endif /* _WIFI_AP_INFO_H_ */
