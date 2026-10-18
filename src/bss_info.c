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

#include <string.h>

#include "bss_info.h"

static gboolean is_bssid(const gchar *bssid)
{
	gsize i;

	if (!bssid || strlen(bssid) != 17)
		return FALSE;

	for (i = 0; i < 17; i++)
	{
		if (i % 3 == 2)
		{
			if (bssid[i] != ':')
				return FALSE;
		}
		else if (!g_ascii_isxdigit(bssid[i]))
		{
			return FALSE;
		}
	}

	return TRUE;
}

GArray *bss_info_from_access_point(const gchar *bssid, guint frequency, guchar strength)
{
	bssinfo_t info;
	GArray *array;

	if (!is_bssid(bssid))
		return NULL;

	memset(&info, 0, sizeof(info));
	g_strlcpy(info.bssid, bssid, sizeof(info.bssid));
	info.signal = (gint)strength - 120;
	info.frequency = (gint)frequency;

	array = g_array_sized_new(FALSE, FALSE, sizeof(bssinfo_t), 1);
	g_array_append_val(array, info);
	return array;
}
