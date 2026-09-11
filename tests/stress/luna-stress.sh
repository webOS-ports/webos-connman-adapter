#!/bin/sh
# Stress test for webos-connman-adapter, meant to run ON the webOS/LuneOS
# device (BusyBox sh compatible).
#
# Hammers the luna API with parallel queries, subscription churn and
# malformed/hostile payloads (regression payloads for previously fixed bugs:
# shell metacharacters in SSIDs, oversized SSIDs, negative channels, tiny
# scan intervals, out-of-range integers), while watching the daemon for
# crashes, restarts, fd leaks and RSS growth.
#
# Usage: luna-stress.sh [iterations]   (default 25)
#
# Exit code 0 = daemon survived with stable memory/fd usage.

ITERATIONS=${1:-25}
SERVICE=webos-connman-adapter
LUNA="luna-send -n 1 -a com.webos.service.connectionmanager"

fail() {
	echo "FAIL: $1"
	exit 1
}

get_pid() {
	# systemd MAINPID is authoritative
	systemctl show -p MainPID --value $SERVICE 2>/dev/null | tr -d ' '
}

get_rss() {
	awk '/VmRSS/{print $2}' /proc/$1/status 2>/dev/null
}

get_fds() {
	ls /proc/$1/fd 2>/dev/null | wc -l
}

PID=$(get_pid)
[ -n "$PID" ] && [ "$PID" != "0" ] || fail "$SERVICE is not running"

RSS_BEFORE=$(get_rss $PID)
FDS_BEFORE=$(get_fds $PID)
echo "Daemon pid=$PID rss=${RSS_BEFORE}kB fds=$FDS_BEFORE, running $ITERATIONS iterations"

# ---- payload sets -----------------------------------------------------------

# Valid queries (JSON payload after the URI)
run_valid_queries() {
	luna-send -n 1 luna://com.webos.service.connectionmanager/getstatus '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.connectionmanager/getinfo '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.connectionmanager/checkinternetstatus '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/getstatus '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/getprofilelist '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/getNetworks '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/getCountryCode '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/tethering/getState '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/tethering/getMaxStationCount '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/p2p/getstate '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wan/getstatus '{}' >/dev/null 2>&1
	luna-send -n 1 luna://com.palm.wan/getstatus '{}' >/dev/null 2>&1
}

# Malformed / hostile payloads. These are regression checks for fixed bugs -
# none of them may crash or wedge the daemon.
run_hostile_payloads() {
	# broken JSON and schema violations
	luna-send -n 1 luna://com.webos.service.wifi/getstatus 'not json' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.connectionmanager/getstatus '{"subscribe":"yes"}' >/dev/null 2>&1

	# scan: shell metacharacters in SSID (former command injection),
	# oversized SSIDs (former heap overflow), frequency-only scan (former
	# 5-byte buffer overflow), huge frequency counts
	luna-send -n 1 luna://com.webos.service.wifi/scan \
		'{"ssid":["x; touch /tmp/wca-pwned; true"]}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/scan \
		'{"ssid":["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"]}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/scan \
		'{"frequency":[2412,2417,2422,2427,2432,2437,2442,2447,2452,2457]}' >/dev/null 2>&1

	# setCountryCode: former popen() injection
	luna-send -n 1 luna://com.webos.service.wifi/setCountryCode \
		'{"countryCode":"US; reboot"}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/setCountryCode \
		'{"countryCode":"$(reboot)"}' >/dev/null 2>&1

	# tethering: negative channel (former guint wrap), out-of-range values
	luna-send -n 1 luna://com.webos.service.wifi/tethering/setState \
		'{"channel":-1}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/tethering/setState \
		'{"channel":100000}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.wifi/tethering/setMaxStationCount \
		'{"maxStationCount":-5}' >/dev/null 2>&1

	# setTechnologyState with one array absent (former 4-billion-iteration
	# loop on 32-bit) and with bogus technologies
	luna-send -n 1 luna://com.webos.service.connectionmanager/setTechnologyState \
		'{"enabled":["nosuchtech"]}' >/dev/null 2>&1
	luna-send -n 1 luna://com.webos.service.connectionmanager/setTechnologyState \
		'{}' >/dev/null 2>&1

	# findnetworks with a tiny interval (former busy-loop); the immediate
	# reply satisfies -n 1 so this cannot block
	luna-send -n 1 luna://com.webos.service.wifi/findnetworks \
		'{"subscribe":true,"interval":3}' >/dev/null 2>&1

	# changeNetwork against a nonexistent profile
	luna-send -n 1 luna://com.webos.service.wifi/changeNetwork \
		'{"profileId":999999,"passKey":"irrelevant1"}' >/dev/null 2>&1

	# setipv6 manual without prefix (former int-as-string config corruption)
	luna-send -n 1 luna://com.webos.service.connectionmanager/setipv6 \
		'{"method":"manual","address":"2001:db8::1","gateway":"2001:db8::ff","ssid":"NoSuchNetwork"}' >/dev/null 2>&1
}

# Subscription churn: open a few subscriptions, let them live briefly, then
# cut them off (exercises the subscription-cancel paths). No `timeout` on
# BusyBox images, so kill by pid after a fixed delay.
run_subscription_churn() {
	luna-send -n 5 luna://com.webos.service.connectionmanager/getstatus '{"subscribe":true}' >/dev/null 2>&1 &
	S1=$!
	luna-send -n 5 luna://com.webos.service.wifi/getstatus '{"subscribe":true}' >/dev/null 2>&1 &
	S2=$!
	luna-send -n 5 luna://com.webos.service.wifi/tethering/getState '{"subscribe":true}' >/dev/null 2>&1 &
	S3=$!
	luna-send -n 5 luna://com.webos.service.wifi/p2p/getstate '{"subscribe":true}' >/dev/null 2>&1 &
	S4=$!
	luna-send -n 5 luna://com.webos.service.wifi/findnetworks '{"subscribe":true}' >/dev/null 2>&1 &
	S5=$!

	sleep 3
	kill $S1 $S2 $S3 $S4 $S5 2>/dev/null
	wait $S1 $S2 $S3 $S4 $S5 2>/dev/null
}

# ---- main loop ---------------------------------------------------------------

i=0
while [ $i -lt $ITERATIONS ]; do
	i=$((i + 1))

	# three parallel streams per iteration
	run_valid_queries &
	run_hostile_payloads &
	run_subscription_churn &
	wait

	NEWPID=$(get_pid)
	if [ "$NEWPID" != "$PID" ]; then
		fail "daemon crashed/restarted during iteration $i (pid $PID -> $NEWPID)"
	fi

	if [ -e /tmp/wca-pwned ]; then
		fail "command injection executed (found /tmp/wca-pwned)"
	fi

	if [ $((i % 5)) -eq 0 ]; then
		echo "  iteration $i/$ITERATIONS: rss=$(get_rss $PID)kB fds=$(get_fds $PID)"
	fi
done

# let pending replies settle before the final sample
sleep 3

RSS_AFTER=$(get_rss $PID)
FDS_AFTER=$(get_fds $PID)
RSS_GROWTH=$((RSS_AFTER - RSS_BEFORE))
FD_GROWTH=$((FDS_AFTER - FDS_BEFORE))

echo "Result: rss ${RSS_BEFORE}kB -> ${RSS_AFTER}kB (+${RSS_GROWTH}kB), fds $FDS_BEFORE -> $FDS_AFTER (+$FD_GROWTH)"

systemctl is-active --quiet $SERVICE || fail "daemon not active after test"

# thresholds: allow some allocator slack, catch real leaks
[ $RSS_GROWTH -lt 2048 ] || fail "RSS grew by ${RSS_GROWTH}kB (leak?)"
[ $FD_GROWTH -lt 10 ] || fail "fd count grew by $FD_GROWTH (fd leak?)"

echo "PASS"
exit 0
