// Copyright (c) 2026 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef BSS_INFO_H_
#define BSS_INFO_H_

#include <glib.h>

/**
 * BSSinformation for the service
 *
 */
typedef struct bssinfo
{
	gchar bssid[18]; /* "20:AA:4B:DB:C5:A8" */
	gint signal;
	gint frequency;
} bssinfo_t;

/*
 * The access points behind a WiFi service, as getNetworks reports them in
 * bssInfo (com.webos.service.location's WiFi positioning reads them there).
 *
 * LG's connman lists them all in a "BSS" property. Upstream connman with
 * LuneOS's patch 0006 publishes only the one the service would use: "BSSID"
 * and "Frequency", next to the usual "Strength". This makes that one into a
 * one-entry list. connman works Strength out as 120 + the signal in dBm,
 * capped at 100, so the signal is Strength - 120 (a capped 100 reads as
 * -20 dBm, the strongest it can tell apart).
 *
 * Returns NULL when there is no usable BSSID: NULL, empty, or not of the
 * "xx:xx:xx:xx:xx:xx" form.
 */
GArray *bss_info_from_access_point(const gchar *bssid, guint frequency, guchar strength);

#endif /* BSS_INFO_H_ */
