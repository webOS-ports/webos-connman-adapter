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

#include <glib.h>

#include "bss_info.h"

static void test_one_access_point(void)
{
	GArray *bss = bss_info_from_access_point("a8:0b:fb:71:5d:d0", 2462, 57);
	g_assert_nonnull(bss);
	g_assert_cmpuint(bss->len, ==, 1);
	bssinfo_t *info = &g_array_index(bss, bssinfo_t, 0);
	g_assert_cmpstr(info->bssid, ==, "a8:0b:fb:71:5d:d0");
	g_assert_cmpint(info->frequency, ==, 2462);
	g_assert_cmpint(info->signal, ==, -63);
	g_array_free(bss, TRUE);
}

static void test_signal_from_strength(void)
{
	/* connman: Strength = 120 + dBm, capped at 100. */
	GArray *bss = bss_info_from_access_point("34:97:F6:01:B9:F8", 5180, 100);
	g_assert_cmpint(g_array_index(bss, bssinfo_t, 0).signal, ==, -20);
	g_array_free(bss, TRUE);

	bss = bss_info_from_access_point("34:97:f6:01:b9:f8", 2412, 0);
	g_assert_cmpint(g_array_index(bss, bssinfo_t, 0).signal, ==, -120);
	g_assert_cmpstr(g_array_index(bss, bssinfo_t, 0).bssid, ==, "34:97:f6:01:b9:f8");
	g_array_free(bss, TRUE);
}

static void test_refuses_what_is_no_bssid(void)
{
	g_assert_null(bss_info_from_access_point(NULL, 2412, 50));
	g_assert_null(bss_info_from_access_point("", 2412, 50));
	g_assert_null(bss_info_from_access_point("a8:0b:fb:71:5d", 2412, 50));
	g_assert_null(bss_info_from_access_point("a8:0b:fb:71:5d:d0:11", 2412, 50));
	g_assert_null(bss_info_from_access_point("a8-0b-fb-71-5d-d0", 2412, 50));
	g_assert_null(bss_info_from_access_point("a8:0b:fb:71:5d:dg", 2412, 50));
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/bss-info/one-access-point", test_one_access_point);
	g_test_add_func("/bss-info/signal-from-strength", test_signal_from_strength);
	g_test_add_func("/bss-info/refuses-what-is-no-bssid", test_refuses_what_is_no_bssid);
	return g_test_run();
}
