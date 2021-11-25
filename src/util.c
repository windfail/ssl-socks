#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <string.h>

// return:-1    :error
//        other :sizeof data
int get_dst_addr(int sock, struct sockaddr_storage *ss, socklen_t *len)
{
	int error = getsockopt(sock, SOL_IPV6, IP6T_SO_ORIGINAL_DST, (struct sockaddr_in6*)ss, len);
	if (error) {
		error = getsockopt(sock, SOL_IP, SO_ORIGINAL_DST, (struct sockaddr_in*)ss, len);
		if (error ) {
			return -1;
		}
	}
	return 0;
}
