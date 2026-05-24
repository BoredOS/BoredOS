/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file http_boredos.cpp BoredOS host-routed HTTPS implementation. */

#include "../../stdafx.h"
#include "http.h"

extern "C" {
#include "libc/string.h"
#include "libc/syscall_user.h"
}

#include "../../safeguards.h"

static bool BoredOSProxySendAll(int fd, const char *data, size_t len)
{
	int idle = 0;
	while (len > 0) {
		sys_network_poll();
		size_t chunk = std::min<size_t>(len, 4096);
		int sent = sys_socket_send(fd, data, chunk);
		if (sent < 0) return false;
		if (sent == 0) {
			if (++idle > 8000) return false;
			sys_yield();
			continue;
		}
		idle = 0;
		data += sent;
		len -= (size_t)sent;
	}
	return true;
}

static int BoredOSProxyRecvSome(int fd, char *data, size_t len)
{
	for (int i = 0; i < 8000; i++) {
		sys_network_poll();
		int ret = sys_socket_recv_nb(fd, data, len);
		if (ret > 0) return ret;
		if (ret == -2) return 0;
		sys_yield();
	}
	return -1;
}

static bool BoredOSProxyRecvLine(int fd, char *line, size_t line_len)
{
	if (line_len == 0) return false;
	size_t pos = 0;
	while (pos + 1 < line_len) {
		char ch = 0;
		int ret = BoredOSProxyRecvSome(fd, &ch, 1);
		if (ret <= 0) return false;
		if (ch == '\n') {
			line[pos] = '\0';
			return true;
		}
		if (ch != '\r') line[pos++] = ch;
	}
	line[pos] = '\0';
	return false;
}

static bool BoredOSProxyConnect(int *out_fd)
{
	if (out_fd == nullptr) return false;
	*out_fd = -1;

	sys_network_poll();
	if (!sys_network_is_initialized() || !sys_network_has_ip()) {
		if (sys_network_init() != 0 || !sys_network_has_ip()) {
			sys_serial_write("[OpenTTD HTTP] network not ready for host proxy\n");
			return false;
		}
	}

	net_ipv4_address_t host{};
	host.bytes[0] = 10;
	host.bytes[1] = 0;
	host.bytes[2] = 2;
	host.bytes[3] = 2;

	for (int attempt = 0; attempt < 3; attempt++) {
		int fd = sys_socket_create(1);
		if (fd < 0) {
			sys_serial_write("[OpenTTD HTTP] host proxy socket create failed\n");
			return false;
		}

		if (sys_socket_connect_start(fd, &host, 8173) != 0) {
			sys_socket_close(fd);
			sys_serial_write("[OpenTTD HTTP] host proxy connect_start failed\n");
			sys_yield();
			continue;
		}

		for (int i = 0; i < 2500; i++) {
			sys_network_poll();
			int mask = sys_socket_poll(fd);
			if ((mask & 4) != 0) {
				sys_socket_close(fd);
				sys_serial_write("[OpenTTD HTTP] host proxy socket error\n");
				fd = -1;
				break;
			}
			if ((mask & 2) != 0) {
				*out_fd = fd;
				sys_serial_write("[OpenTTD HTTP] host proxy connected\n");
				return true;
			}
			sys_yield();
		}

		if (fd >= 0) {
			sys_socket_close(fd);
			sys_serial_write("[OpenTTD HTTP] host proxy connect timeout\n");
		}
		sys_yield();
	}

	return false;
}

static void BoredOSWriteSizeLine(char *out, size_t out_len, size_t value)
{
	if (out_len < 2) return;
	char tmp[24];
	size_t pos = 0;
	do {
		tmp[pos++] = (char)('0' + (value % 10U));
		value /= 10U;
	} while (value != 0 && pos < sizeof(tmp));

	size_t out_pos = 0;
	while (pos > 0 && out_pos + 2 < out_len) out[out_pos++] = tmp[--pos];
	out[out_pos++] = '\n';
	out[out_pos] = '\0';
}

static void BoredOSLogSize(const char *prefix, size_t value, const char *suffix)
{
	char buf[96];
	size_t pos = 0;
	while (prefix != nullptr && *prefix != '\0' && pos + 1 < sizeof(buf)) buf[pos++] = *prefix++;

	char digits[24];
	size_t digit_count = 0;
	do {
		digits[digit_count++] = (char)('0' + (value % 10U));
		value /= 10U;
	} while (value != 0 && digit_count < sizeof(digits));
	while (digit_count > 0 && pos + 1 < sizeof(buf)) buf[pos++] = digits[--digit_count];

	while (suffix != nullptr && *suffix != '\0' && pos + 1 < sizeof(buf)) buf[pos++] = *suffix++;
	buf[pos] = '\0';
	sys_serial_write(buf);
}

/* static */ void NetworkHTTPSocketHandler::Connect(std::string_view uri, HTTPCallback *callback, std::string &&data)
{
	if (callback == nullptr) return;

	int fd = -1;
	if (!BoredOSProxyConnect(&fd)) {
		sys_serial_write("[OpenTTD HTTP] host proxy connect failed\n");
		callback->OnFailure();
		return;
	}

	const char *method = data.empty() ? "GET" : "POST";
	char size_line[32];
	BoredOSWriteSizeLine(size_line, sizeof(size_line), data.size());

	bool ok = BoredOSProxySendAll(fd, "OTHP1\n", 6) &&
		BoredOSProxySendAll(fd, method, strlen(method)) &&
		BoredOSProxySendAll(fd, "\n", 1) &&
		BoredOSProxySendAll(fd, uri.data(), uri.size()) &&
		BoredOSProxySendAll(fd, "\n", 1) &&
		BoredOSProxySendAll(fd, size_line, strlen(size_line)) &&
		(data.empty() || BoredOSProxySendAll(fd, data.data(), data.size()));
	if (!ok) {
		sys_socket_close(fd);
		sys_serial_write("[OpenTTD HTTP] host proxy request send failed\n");
		callback->OnFailure();
		return;
	}

	char line[64];
	if (!BoredOSProxyRecvLine(fd, line, sizeof(line))) {
		sys_socket_close(fd);
		sys_serial_write("[OpenTTD HTTP] host proxy response failed\n");
		callback->OnFailure();
		return;
	}

	if (line[0] != 'O' || line[1] != 'K' || line[2] != ' ') {
		sys_socket_close(fd);
		sys_serial_write("[OpenTTD HTTP] host proxy returned error\n");
		callback->OnFailure();
		return;
	}

	size_t total = 0;
	for (const char *p = line + 3; *p >= '0' && *p <= '9'; p++) total = (total * 10U) + (size_t)(*p - '0');
	BoredOSLogSize("[OpenTTD HTTP] host proxy response size ", total, "\n");

	std::unique_ptr<char[]> response = std::make_unique<char[]>(total == 0 ? 1 : total);
	size_t got = 0;
	size_t next_log = 256U * 1024U;
	while (got < total) {
		size_t want = std::min<size_t>(total - got, 4096);
		int ret = BoredOSProxyRecvSome(fd, response.get() + got, want);
		if (ret <= 0) {
			sys_socket_close(fd);
			sys_serial_write("[OpenTTD HTTP] host proxy response body failed\n");
			callback->OnFailure();
			return;
		}
		got += (size_t)ret;
		if (got >= next_log || got == total) {
			BoredOSLogSize("[OpenTTD HTTP] host proxy body received ", got, "\n");
			next_log += 256U * 1024U;
		}
	}

	sys_socket_close(fd);
	callback->OnReceiveData(std::move(response), total);
	callback->OnReceiveData(nullptr, 0);
}

/* static */ void NetworkHTTPSocketHandler::HTTPReceive()
{
}

void NetworkHTTPInitialize()
{
}

void NetworkHTTPUninitialize()
{
}
