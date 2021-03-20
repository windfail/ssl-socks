#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <string.h>

// return:-1    :error
//        other :sizeof data
int get_dst_addr(int sock, uint8_t *data)
{
	struct sockaddr_in6 dst_addr6;
	socklen_t socklen = sizeof(dst_addr6);
	struct sockaddr_in dst_addr;

	int error = getsockopt(sock, SOL_IPV6, IP6T_SO_ORIGINAL_DST, &dst_addr6, &socklen);
	if (error) {
		socklen = sizeof(dst_addr);
		error = getsockopt(sock, SOL_IP, SO_ORIGINAL_DST, &dst_addr, &socklen);
		if (error ) {
			return -1;
		}
		data[0] = 1;
		memcpy(&data[1], &dst_addr.sin_addr, 4);
		memcpy(&data[5], &dst_addr.sin_port, 2);
		return 7;
	} else {
		data[0] = 4;
		memcpy(&data[1], &dst_addr6.sin6_addr, 16);
		memcpy(&data[17], &dst_addr6.sin6_port, 2);
		return 19;
	}
	return 0;
}
