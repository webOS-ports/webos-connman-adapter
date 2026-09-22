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
#include <string.h>

#include "utils.h"

static void test_strip_prefix(void)
{
	char *result;

	g_assert(strip_prefix(NULL, "pre") == NULL);
	g_assert(strip_prefix("string", NULL) == NULL);
	g_assert(strip_prefix("st", "string") == NULL);
	g_assert(strip_prefix("other", "pre") == NULL);

	result = strip_prefix("/net/connman/service/wifi_abc", "/net/connman/service/");
	g_assert(result != NULL);
	g_assert(strcmp(result, "wifi_abc") == 0);
	g_free(result);

	result = strip_prefix("prefix", "prefix");
	g_assert(result != NULL);
	g_assert(strcmp(result, "") == 0);
	g_free(result);
}

static void test_is_valid_wifi_passphrase(void)
{
	/* invalid arguments */
	g_assert(is_valid_wifi_passphrase(NULL, "psk") == false);
	g_assert(is_valid_wifi_passphrase("password", NULL) == false);

	/* psk: 8..63 chars, or 64 hex chars */
	g_assert(is_valid_wifi_passphrase("short", "psk") == false);
	g_assert(is_valid_wifi_passphrase("12345678", "psk") == true);
	g_assert(is_valid_wifi_passphrase(
	             "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
	             "psk") == false); /* 64 chars, not hex */
	g_assert(is_valid_wifi_passphrase(
	             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
	             "psk") == true); /* 64 hex chars */

	/* wep: 5 or 13 chars, or 10/26 hex chars */
	g_assert(is_valid_wifi_passphrase("abcde", "wep") == true);
	g_assert(is_valid_wifi_passphrase("abcdef", "wep") == false);
	g_assert(is_valid_wifi_passphrase("abcdefghijklm", "wep") == true);
	g_assert(is_valid_wifi_passphrase("0123456789", "wep") == true);
	g_assert(is_valid_wifi_passphrase("012345678z", "wep") == false);

	/* unknown security types are accepted as-is */
	g_assert(is_valid_wifi_passphrase("x", "ieee8021x") == true);
}

static void test_is_vlan(void)
{
	g_assert(is_vlan(NULL) == false);
	g_assert(is_vlan("eth0") == false);
	g_assert(is_vlan("eth0.100") == true);
	g_assert(is_vlan(".") == true);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/utils/strip_prefix", test_strip_prefix);
	g_test_add_func("/utils/is_valid_wifi_passphrase",
	                test_is_valid_wifi_passphrase);
	g_test_add_func("/utils/is_vlan", test_is_vlan);

	return g_test_run();
}
