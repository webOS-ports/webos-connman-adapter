// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wifi_hwaddr.h"

static char *dir;
static int live_calls;
static int live_result;

static int fake_live(const char *ifname, char *out, unsigned int out_len)
{
	(void) ifname;
	live_calls++;
	if (live_result != 0)
		return live_result;
	g_strlcpy(out, "AA:BB:CC:DD:EE:FF", out_len);
	return 0;
}

static void write_record(const char *content, mode_t mode)
{
	char *path = g_build_filename(dir, "wifi-hwaddr-wlan0", NULL);
	int fd;

	g_unlink(path);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	g_assert(fd >= 0);
	g_assert(write(fd, content, strlen(content)) == (ssize_t) strlen(content));
	close(fd);
	g_assert(chmod(path, mode) == 0);
	g_free(path);
}

static int get(char *out, size_t len)
{
	return wifi_hwaddr_get(dir, "wlan0", geteuid(), out, len, fake_live);
}

static void setup(void)
{
	char *path = g_build_filename(dir, "wifi-hwaddr-wlan0", NULL);

	g_unlink(path);
	g_free(path);
	g_assert(chmod(dir, 0755) == 0);
	live_calls = 0;
	live_result = 0;
}

static void test_record_used(void)
{
	char out[32];

	setup();
	write_record("02:1a:2b:3c:4d:5e 1\n", 0644);
	g_assert_cmpint(get(out, sizeof(out)), ==, WIFI_HWADDR_FROM_RECORD);
	g_assert_cmpstr(out, ==, "02:1A:2B:3C:4D:5E");
	g_assert_cmpint(live_calls, ==, 0);
}

static void test_no_record_uses_live(void)
{
	char out[32];

	setup();
	g_assert_cmpint(get(out, sizeof(out)), ==, WIFI_HWADDR_FROM_LIVE);
	g_assert_cmpstr(out, ==, "AA:BB:CC:DD:EE:FF");
	g_assert_cmpint(live_calls, ==, 1);
}

static void test_bad_records_use_live(void)
{
	static const char *const bad[] = {
		"02:1A:2B:3C:4D:5E 1\n", "02:11:22:33:44:55 2\n",
		"01:11:22:33:44:55 1\n", "00:00:00:00:00:00 1\n",
		"02:11:22:33:44:55 1", "02:11:22:33:44:55 1\n\n",
	};
	char out[32];
	size_t i;

	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		setup();
		write_record(bad[i], 0644);
		g_assert_cmpint(get(out, sizeof(out)), ==, WIFI_HWADDR_FROM_LIVE);
	}

	setup();
	write_record("02:11:22:33:44:55 1\n", 0666);
	g_assert_cmpint(get(out, sizeof(out)), ==, WIFI_HWADDR_FROM_LIVE);

	setup();
	write_record("02:11:22:33:44:55 1\n", 0644);
	g_assert(chmod(dir, 0777) == 0);
	g_assert_cmpint(get(out, sizeof(out)), ==, WIFI_HWADDR_FROM_LIVE);
	g_assert(chmod(dir, 0755) == 0);
}

static void test_symlink_not_followed(void)
{
	char *target = g_build_filename(dir, "target", NULL);
	char *path = g_build_filename(dir, "wifi-hwaddr-wlan0", NULL);
	char out[32];
	int fd;

	setup();
	fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	g_assert(fd >= 0);
	g_assert(write(fd, "02:11:22:33:44:55 1\n", 20) == 20);
	close(fd);
	g_assert(symlink(target, path) == 0);
	g_assert_cmpint(get(out, sizeof(out)), ==, WIFI_HWADDR_FROM_LIVE);
	g_unlink(target);
	g_free(target);
	g_free(path);
}

static void test_live_failure(void)
{
	char out[32];

	setup();
	live_result = -1;
	g_assert_cmpint(get(out, sizeof(out)), <, 0);
}

int main(int argc, char **argv)
{
	int ret;

	g_test_init(&argc, &argv, NULL);
	dir = g_dir_make_tmp("test-wifi-hwaddr-XXXXXX", NULL);
	g_assert(dir != NULL);

	g_test_add_func("/wifi-hwaddr/record-used", test_record_used);
	g_test_add_func("/wifi-hwaddr/no-record-uses-live", test_no_record_uses_live);
	g_test_add_func("/wifi-hwaddr/bad-records-use-live", test_bad_records_use_live);
	g_test_add_func("/wifi-hwaddr/symlink-not-followed", test_symlink_not_followed);
	g_test_add_func("/wifi-hwaddr/live-failure", test_live_failure);
	ret = g_test_run();

	setup();
	g_rmdir(dir);
	g_free(dir);
	return ret;
}
