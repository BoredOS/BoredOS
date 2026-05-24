/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file os_abstraction.h Network stuff has many things that needs to be
 *                        included and/or implemented by default.
 *                        All those things are in this file.
 */

#ifndef NETWORK_CORE_OS_ABSTRACTION_H
#define NETWORK_CORE_OS_ABSTRACTION_H

/**
 * Abstraction of a network error where all implementation details of the
 * error codes are encapsulated in this class and the abstraction layer.
 */
class NetworkError {
private:
	int error;                   ///< The underlying error number from errno or WSAGetLastError.
	mutable std::string message; ///< The string representation of the error (set on first call to #AsString).
public:
	NetworkError(int error, std::string_view message = {});

	bool HasError() const;
	bool WouldBlock() const;
	bool IsConnectionReset() const;
	bool IsConnectInProgress() const;
	std::string_view AsString() const;

	static NetworkError GetLast();
};

/* Include standard stuff per OS */

/* Windows stuff */
#if defined(_WIN32)
#include <errno.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

/* Windows has some different names for some types */
typedef unsigned long in_addr_t;

/* Handle cross-compilation with --build=*-*-cygwin --host=*-*-mingw32 */
#if defined(__MINGW32__) && !defined(AI_ADDRCONFIG)
#	define AI_ADDRCONFIG               0x00000400
#endif

#if !(defined(__MINGW32__) || defined(__CYGWIN__))
	/* Windows has some different names for some types */
	typedef SSIZE_T ssize_t;
	typedef int socklen_t;
#	define IPPROTO_IPV6 41
#endif /* !(__MINGW32__ && __CYGWIN__) */
#endif /* _WIN32 */

/* BoredOS has a small kernel TCP/DNS syscall surface rather than POSIX
 * sockets. This shim exposes the subset OpenTTD needs for outbound TCP.
 */
#if defined(BOREDOS)
extern "C" {
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"
#include "libc/syscall_user.h"
}

#	define SOCKET int
#	define INVALID_SOCKET -1
#	define AF_UNSPEC 0
#	define AF_INET 2
#	define AF_INET6 10
#	define SOCK_STREAM 1
#	define SOCK_DGRAM 2
#	define SOL_SOCKET 1
#	define SO_REUSEADDR 2
#	define SO_BROADCAST 6
#	define SO_REUSEPORT 15
#	define SO_ERROR 4
#	define IPPROTO_TCP 6
#	define IPPROTO_IPV6 41
#	define TCP_NODELAY 1
#	define IPV6_V6ONLY 26
#	define IFF_BROADCAST 0x2
#	define AI_PASSIVE 0x01
#	define AI_ADDRCONFIG 0x20
#	define EAI_FAIL -4
#	define NI_NUMERICHOST 0x01
#	define FD_SETSIZE 64
#	define BOREDOS_SOCKET_TCP 1

typedef unsigned int socklen_t;
typedef long ssize_t;
typedef uint32_t in_addr_t;

struct in_addr {
	uint32_t s_addr;
};

struct in6_addr {
	uint8_t s6_addr[16];
};

struct sockaddr {
	uint16_t sa_family;
	char sa_data[14];
};

struct sockaddr_in {
	uint16_t sin_family;
	uint16_t sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};

struct sockaddr_in6 {
	uint16_t sin6_family;
	uint16_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

struct sockaddr_storage {
	uint16_t ss_family;
	char __ss_padding[126];
};

struct addrinfo {
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};

struct ifaddrs {
	struct ifaddrs *ifa_next;
	char *ifa_name;
	unsigned int ifa_flags;
	struct sockaddr *ifa_addr;
	struct sockaddr *ifa_netmask;
	struct sockaddr *ifa_broadaddr;
	void *ifa_data;
};

struct timeval {
	long tv_sec;
	long tv_usec;
};

typedef struct {
	uint64_t bits;
} fd_set;

inline void FD_ZERO(fd_set *set) { set->bits = 0; }
inline void FD_SET(SOCKET s, fd_set *set) { if (s >= 0 && s < 64) set->bits |= 1ULL << s; }
inline int FD_ISSET(SOCKET s, fd_set *set) { return (s >= 0 && s < 64) ? ((set->bits >> s) & 1U) : 0; }

inline uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
inline uint16_t ntohs(uint16_t v) { return htons(v); }
inline uint32_t htonl(uint32_t v) { return __builtin_bswap32(v); }
inline uint32_t ntohl(uint32_t v) { return __builtin_bswap32(v); }
inline bool boredos_ensure_network()
{
	sys_network_poll();
	if (sys_network_is_initialized() && sys_network_has_ip()) return true;
	bool ready = sys_network_init() == 0 && sys_network_has_ip();
	sys_network_poll();
	return ready;
}
inline uint16_t boredos_parse_port(const char *service)
{
	uint32_t port = 0;
	while (*service >= '0' && *service <= '9') {
		port = (port * 10U) + (uint32_t)(*service - '0');
		if (port > 65535U) return 0;
		service++;
	}
	return (uint16_t)port;
}
inline bool boredos_parse_ipv4_literal(const char *node, net_ipv4_address_t *ip)
{
	if (node == nullptr || ip == nullptr) return false;

	uint8_t bytes[4]{};
	for (int part = 0; part < 4; part++) {
		if (*node < '0' || *node > '9') return false;

		uint32_t value = 0;
		while (*node >= '0' && *node <= '9') {
			value = (value * 10U) + (uint32_t)(*node - '0');
			if (value > 255U) return false;
			node++;
		}
		bytes[part] = (uint8_t)value;

		if (part < 3) {
			if (*node != '.') return false;
			node++;
		}
	}
	if (*node != '\0') return false;

	ip->bytes[0] = bytes[0];
	ip->bytes[1] = bytes[1];
	ip->bytes[2] = bytes[2];
	ip->bytes[3] = bytes[3];
	return true;
}

inline bool boredos_looks_ipv6_literal(const char *node)
{
	if (node == nullptr) return false;
	while (*node != '\0') {
		if (*node++ == ':') return true;
	}
	return false;
}

inline bool boredos_streq(const char *a, const char *b)
{
	if (a == nullptr || b == nullptr) return false;
	while (*a != '\0' && *b != '\0') {
		if (*a++ != *b++) return false;
	}
	return *a == '\0' && *b == '\0';
}

inline void boredos_strcpy(char *dst, const char *src)
{
	while ((*dst++ = *src++) != '\0') {}
}

struct BoredOSDNSCacheEntry {
	bool valid;
	char name[96];
	net_ipv4_address_t ip;
};

inline BoredOSDNSCacheEntry *boredos_dns_cache()
{
	static BoredOSDNSCacheEntry cache[8]{};
	return cache;
}

inline bool boredos_dns_cache_get(const char *node, net_ipv4_address_t *ip)
{
	if (node == nullptr || ip == nullptr) return false;
	for (int i = 0; i < 8; i++) {
		if (boredos_dns_cache()[i].valid && boredos_streq(boredos_dns_cache()[i].name, node)) {
			*ip = boredos_dns_cache()[i].ip;
			return true;
		}
	}
	return false;
}

inline void boredos_dns_cache_put(const char *node, const net_ipv4_address_t *ip)
{
	if (node == nullptr || ip == nullptr) return;
	size_t len = strlen(node);
	if (len == 0 || len >= sizeof(boredos_dns_cache()[0].name)) return;

	static uint8_t next_slot = 0;
	BoredOSDNSCacheEntry &entry = boredos_dns_cache()[next_slot++ % 8];
	entry.valid = true;
	boredos_strcpy(entry.name, node);
	entry.ip = *ip;
}

inline bool boredos_dns_lookup_retry(const char *node, net_ipv4_address_t *ip)
{
	if (boredos_dns_cache_get(node, ip)) {
		sys_serial_write("[OpenTTD NET] DNS cache hit\n");
		return true;
	}

	for (int attempt = 0; attempt < 3; attempt++) {
		sys_serial_write("[OpenTTD NET] DNS lookup host: ");
		sys_serial_write(node);
		sys_serial_write("\n");
		sys_network_poll();
		if (sys_dns_lookup(node, ip) == 0) {
			boredos_dns_cache_put(node, ip);
			return true;
		}
		sys_network_poll();
		sys_yield();
	}

	return false;
}

inline int socket(int family, int type, int)
{
	if (family != AF_INET && family != AF_UNSPEC) {
		errno = EINVAL;
		return INVALID_SOCKET;
	}
	if (type != SOCK_STREAM) {
		errno = EINVAL;
		return INVALID_SOCKET;
	}
	errno = 0;
	int fd = sys_socket_create(BOREDOS_SOCKET_TCP);
	if (fd < 0) {
		errno = ENOMEM;
		return INVALID_SOCKET;
	}
	return fd;
}

inline int connect(SOCKET s, const struct sockaddr *addr, socklen_t len)
{
	if (s == INVALID_SOCKET || addr == nullptr || len < sizeof(sockaddr_in) || addr->sa_family != AF_INET) {
		errno = EINVAL;
		return -1;
	}
	if (!boredos_ensure_network()) {
		errno = EIO;
		return -1;
	}

	const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(addr);
	net_ipv4_address_t ip{};
	uint32_t host_addr = ntohl(in->sin_addr.s_addr);
	ip.bytes[0] = (uint8_t)((host_addr >> 24) & 0xFF);
	ip.bytes[1] = (uint8_t)((host_addr >> 16) & 0xFF);
	ip.bytes[2] = (uint8_t)((host_addr >> 8) & 0xFF);
	ip.bytes[3] = (uint8_t)(host_addr & 0xFF);

	sys_serial_write("[OpenTTD NET] tcp connect begin\n");
	sys_network_poll();
	if (sys_socket_connect_start(s, &ip, ntohs(in->sin_port)) != 0) {
		sys_serial_write("[OpenTTD NET] tcp connect failed\n");
		errno = EIO;
		return -1;
	}
	sys_network_poll();
	sys_serial_write("[OpenTTD NET] tcp connect started\n");
	errno = EINPROGRESS;
	return -1;
}

inline int bind(SOCKET, const struct sockaddr *, socklen_t) { errno = ENOSYS; return -1; }
inline int listen(SOCKET, int) { errno = ENOSYS; return -1; }
inline SOCKET accept(SOCKET, struct sockaddr *, socklen_t *) { errno = ENOSYS; return INVALID_SOCKET; }
inline int closesocket(SOCKET s)
{
	if (s == INVALID_SOCKET) return 0;
	return sys_socket_close(s);
}

inline int setsockopt(SOCKET, int, int, const char *, socklen_t) { errno = 0; return 0; }
inline int getsockopt(SOCKET s, int, int optname, char *value, socklen_t *len)
{
	if (optname == SO_ERROR && value != nullptr && len != nullptr && *len >= (socklen_t)sizeof(int)) {
		int socket_error = sys_socket_error(s);
		*reinterpret_cast<int *>(value) = socket_error == 0 ? 0 : EIO;
		*len = sizeof(int);
		errno = 0;
		return 0;
	}
	errno = ENOSYS;
	return -1;
}
inline int getsockname(SOCKET, struct sockaddr *, socklen_t *) { errno = ENOSYS; return -1; }
inline int getpeername(SOCKET, struct sockaddr *, socklen_t *) { errno = ENOSYS; return -1; }
inline ssize_t send(SOCKET s, const char *data, size_t len, int)
{
	if (s == INVALID_SOCKET) {
		errno = EBADF;
		return -1;
	}
	sys_network_poll();
	int ret = sys_socket_send(s, data, len);
	sys_network_poll();
	if (ret == 0) {
		errno = EWOULDBLOCK;
		return -1;
	}
	if (ret < 0) errno = EIO;
	else errno = 0;
	return ret;
}

inline ssize_t recv(SOCKET s, char *data, size_t len, int)
{
	if (s == INVALID_SOCKET) {
		errno = EBADF;
		return -1;
	}
	sys_network_poll();
	int ret = sys_socket_recv_nb(s, data, len);
	if (ret == 0) {
		errno = EWOULDBLOCK;
		return -1;
	}
	if (ret == -2) return 0;
	if (ret < 0) {
		errno = EIO;
		return -1;
	}
	errno = 0;
	return ret;
}

inline ssize_t sendto(SOCKET, const char *, size_t, int, const struct sockaddr *, socklen_t) { errno = ENOSYS; return -1; }
inline ssize_t recvfrom(SOCKET, char *, size_t, int, struct sockaddr *, socklen_t *) { errno = ENOSYS; return -1; }
inline int select(int, fd_set *readfds, fd_set *writefds, fd_set *, struct timeval *)
{
	int ready = 0;
	uint64_t read_bits = readfds != nullptr ? readfds->bits : 0;
	uint64_t write_bits = writefds != nullptr ? writefds->bits : 0;
	sys_network_poll();
	if (readfds != nullptr) FD_ZERO(readfds);
	if (writefds != nullptr) FD_ZERO(writefds);
	if (writefds != nullptr) {
		for (SOCKET s = 0; s < FD_SETSIZE; s++) {
				if (((write_bits >> s) & 1ULL) == 0) continue;
				int mask = sys_socket_poll(s);
				if ((mask & (2 | 4)) != 0) {
					FD_SET(s, writefds);
					ready++;
				}
		}
	}
	if (readfds != nullptr) {
		for (SOCKET s = 0; s < FD_SETSIZE; s++) {
			if (((read_bits >> s) & 1ULL) == 0) continue;
			int mask = sys_socket_poll(s);
			if ((mask & 1) != 0) {
				FD_SET(s, readfds);
				ready++;
			}
		}
	}
	sys_network_poll();
	errno = 0;
	return ready;
}

inline int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
	if (res == nullptr || service == nullptr) return EAI_FAIL;
	*res = nullptr;
	if (hints != nullptr && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET) return EAI_FAIL;
	if (hints != nullptr && hints->ai_socktype != 0 && hints->ai_socktype != SOCK_STREAM) return EAI_FAIL;

	net_ipv4_address_t ip{};
	if (node == nullptr) return EAI_FAIL;
	if (boredos_looks_ipv6_literal(node)) {
		sys_serial_write("[OpenTTD NET] IPv6 address skipped on BoredOS\n");
		return EAI_FAIL;
	}
	if (!boredos_parse_ipv4_literal(node, &ip)) {
		if (!boredos_ensure_network()) return EAI_FAIL;
		sys_serial_write("[OpenTTD NET] DNS lookup begin\n");
		if (!boredos_dns_lookup_retry(node, &ip)) {
			sys_serial_write("[OpenTTD NET] DNS lookup failed\n");
			return EAI_FAIL;
		}
		sys_network_poll();
		sys_serial_write("[OpenTTD NET] DNS lookup ok\n");
	}

	addrinfo *ai = static_cast<addrinfo *>(malloc(sizeof(addrinfo)));
	sockaddr_in *addr = static_cast<sockaddr_in *>(malloc(sizeof(sockaddr_in)));
	if (ai == nullptr || addr == nullptr) {
		if (ai != nullptr) free(ai);
		if (addr != nullptr) free(addr);
		return EAI_FAIL;
	}

	memset(ai, 0, sizeof(*ai));
	memset(addr, 0, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_port = htons(boredos_parse_port(service));
	uint32_t host_addr = ((uint32_t)ip.bytes[0] << 24) | ((uint32_t)ip.bytes[1] << 16) | ((uint32_t)ip.bytes[2] << 8) | (uint32_t)ip.bytes[3];
	addr->sin_addr.s_addr = htonl(host_addr);

	ai->ai_family = AF_INET;
	ai->ai_socktype = SOCK_STREAM;
	ai->ai_protocol = IPPROTO_TCP;
	ai->ai_addrlen = sizeof(sockaddr_in);
	ai->ai_addr = reinterpret_cast<sockaddr *>(addr);
	*res = ai;
	return 0;
}

inline void freeaddrinfo(struct addrinfo *ai)
{
	while (ai != nullptr) {
		addrinfo *next = ai->ai_next;
		if (ai->ai_addr != nullptr) free(ai->ai_addr);
		free(ai);
		ai = next;
	}
}
inline int getifaddrs(struct ifaddrs **ifap) { if (ifap) *ifap = nullptr; errno = ENOSYS; return -1; }
inline void freeifaddrs(struct ifaddrs *) {}
inline const char *gai_strerror(int) { return "BoredOS DNS lookup failed"; }
inline int getnameinfo(const struct sockaddr *, socklen_t, char *host, size_t hostlen, char *, size_t, int)
{
	if (hostlen > 0) host[0] = '\0';
	errno = ENOSYS;
	return EAI_FAIL;
}

#endif /* BOREDOS */

/* UNIX stuff */
#if defined(UNIX)
#	if defined(OPENBSD) || defined(__NetBSD__)
#		define AI_ADDRCONFIG 0
#	endif
#	define SOCKET int
#	define INVALID_SOCKET -1
#	define closesocket close
/* Need this for FIONREAD on solaris */
#	define BSD_COMP

/* Includes needed for UNIX-like systems */
#	include <unistd.h>
#	include <sys/ioctl.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <arpa/inet.h>
#	include <net/if.h>
#	include <ifaddrs.h>
#	if !defined(INADDR_NONE)
#		define INADDR_NONE 0xffffffff
#	endif

#	if defined(__GLIBC__) && (__GLIBC__ <= 2) && (__GLIBC_MINOR__ <= 1)
		typedef uint32_t in_addr_t;
#	endif

#	include <errno.h>
#	include <sys/time.h>
#	include <netdb.h>

#   if defined(__EMSCRIPTEN__)
/* Emscripten doesn't support AI_ADDRCONFIG and errors out on it. */
#		undef AI_ADDRCONFIG
#		define AI_ADDRCONFIG 0
/* Emscripten says it supports FD_SETSIZE fds, but it really only supports 64.
 * https://github.com/emscripten-core/emscripten/issues/1711 */
#		undef FD_SETSIZE
#		define FD_SETSIZE 64
#   endif

/* Haiku says it supports FD_SETSIZE fds, but it really only supports 512. */
#   if defined(__HAIKU__)
#		undef FD_SETSIZE
#		define FD_SETSIZE 512
#   endif

#endif /* UNIX */

#ifdef __EMSCRIPTEN__
/**
 * Emscripten doesn't set 'addrlen' for accept(), getsockname(), getpeername()
 * and recvfrom(), which confuses other functions and causes them to crash.
 * This function needs to be called after these four functions to make sure
 * 'addrlen' is patched up.
 *
 * https://github.com/emscripten-core/emscripten/issues/12996
 *
 * @param address The address returned by those four functions.
 * @return The correct value for addrlen.
 */
inline socklen_t FixAddrLenForEmscripten(struct sockaddr_storage &address)
{
	switch (address.ss_family) {
		case AF_INET6: return sizeof(struct sockaddr_in6);
		case AF_INET: return sizeof(struct sockaddr_in);
		default: NOT_REACHED();
	}
}
#endif


bool SetNonBlocking(SOCKET d);
bool SetNoDelay(SOCKET d);
bool SetReusePort(SOCKET d);
NetworkError GetSocketError(SOCKET d);

/* Make sure these structures have the size we expect them to be */
static_assert(sizeof(in_addr)  ==  4); ///< IPv4 addresses should be 4 bytes.
static_assert(sizeof(in6_addr) == 16); ///< IPv6 addresses should be 16 bytes.

struct SocketSender {
	SOCKET sock;

	ssize_t operator()(std::span<const uint8_t> buffer)
	{
		return send(this->sock, reinterpret_cast<const char *>(buffer.data()), static_cast<int>(buffer.size()), 0);
	}
};

struct SocketReceiver {
	SOCKET sock;

	ssize_t operator()(std::span<uint8_t> buffer)
	{
		return recv(this->sock, reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0);
	}
};

#endif /* NETWORK_CORE_OS_ABSTRACTION_H */
