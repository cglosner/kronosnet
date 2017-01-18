#include "config.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "libknet.h"
#include "compat.h"
#include "host.h"
#include "link.h"
#include "logging.h"
#include "common.h"
#include "transports.h"

int _configure_common_socket(knet_handle_t knet_h, int sock, const char *type)
{
	int err = 0, savederrno = 0;
	int value;

	if (_fdset_cloexec(sock)) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s CLOEXEC socket opts: %s",
			type, strerror(savederrno));
		goto exit_error;
	}

	if (_fdset_nonblock(sock)) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s NONBLOCK socket opts: %s",
			type, strerror(savederrno));
		goto exit_error;
	}

#ifdef SO_RCVBUFFORCE
	value = KNET_RING_RCVBUFF;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUFFORCE, &value, sizeof(value)) < 0) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s receive buffer: %s",
			type, strerror(savederrno));
		goto exit_error;
	}
#else
	value = KNET_RING_RCVBUFF;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) < 0) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s SO_RECVBUF: %s",
			type, strerror(savederrno));
		goto exit_error;
	}
#endif

	value = KNET_RING_RCVBUFF;
#ifdef SO_SNDBUFFORCE
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUFFORCE, &value, sizeof(value)) < 0) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s send buffer: %s",
			type, strerror(savederrno));
		goto exit_error;
	}
#else
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)) < 0) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s SO_SNDBUF: %s",
			type, strerror(savederrno));
		goto exit_error;
	}
#endif

exit_error:
	errno = savederrno;
	return err;
}

int _configure_transport_socket(knet_handle_t knet_h, int sock, struct sockaddr_storage *address, const char *type)
{
	int err = 0, savederrno = 0;
	int value;

	if (_configure_common_socket(knet_h, sock, type) < 0) {
		savederrno = errno;
		err = -1;
		goto exit_error;
	}

#ifdef IP_FREEBIND
	value = 1;
	if (setsockopt(sock, SOL_IP, IP_FREEBIND, &value, sizeof(value)) <0) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set FREEBIND on %s socket: %s",
			type, strerror(savederrno));
		goto exit_error;
	}
#endif
#ifdef IP_BINDANY /* BSD */
	value = 1;
	if (setsockopt(sock, IPPROTO_IP, IP_BINDANY, &value, sizeof(value)) <0) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set BINDANY on %s socket: %s",
			type, strerror(savederrno));
		goto exit_error;
	}
#endif

	if (address->ss_family == AF_INET6) {
		value = 1;
		if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
			       &value, sizeof(value)) < 0) {
			savederrno = errno;
			err = -1;
			log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s IPv6 only: %s",
				type, strerror(savederrno));
			goto exit_error;

		}
#ifdef IPV6_MTU_DISCOVER
		value = IPV6_PMTUDISC_PROBE;
		if (setsockopt(sock, SOL_IPV6, IPV6_MTU_DISCOVER, &value, sizeof(value)) <0) {
			savederrno = errno;
			err = -1;
			log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set PMTUDISC on %s socket: %s",
				type, strerror(savederrno));
			goto exit_error;
		}
#else
		value = 1;
		if (setsockopt(sock, IPPROTO_IPV6, IPV6_DONTFRAG, &value, sizeof(value)) <0) {
			savederrno = errno;
			err = -1;
			log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set DONTFRAG on %s socket: %s",
				type, strerror(savederrno));
			goto exit_error;
		}
#endif
	} else {
#ifdef IP_MTU_DISCOVER
		value = IP_PMTUDISC_PROBE;
		if (setsockopt(sock, SOL_IP, IP_MTU_DISCOVER, &value, sizeof(value)) <0) {
			savederrno = errno;
			err = -1;
			log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set PMTUDISC on %s socket: %s",
				type, strerror(savederrno));
			goto exit_error;
		}
#else
		value = 1;
		if (setsockopt(sock, IPPROTO_IP, IP_DONTFRAG, &value, sizeof(value)) <0) {
			savederrno = errno;
			err = -1;
			log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set DONTFRAG on %s socket: %s",
				type, strerror(savederrno));
			goto exit_error;
		}
#endif
	}

	value = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s reuseaddr: %s",
			type, strerror(savederrno));
		goto exit_error;
	}

	if (_fdset_cloexec(sock)) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s CLOEXEC socket opts: %s",
			type, strerror(savederrno));
		goto exit_error;
	}

	if (_fdset_nonblock(sock)) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_TRANSPORT, "Unable to set %s NONBLOCK socket opts: %s",
			type, strerror(savederrno));
		goto exit_error;
	}

exit_error:
	errno = savederrno;
	return err;
}

int _init_socketpair(knet_handle_t knet_h, int *sock)
{
	int err = 0, savederrno = 0;
	int i;

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sock) != 0) {
		savederrno = errno;
		err = -1;
		log_err(knet_h, KNET_SUB_HANDLE, "Unable to initialize socketpair: %s",
			strerror(savederrno));
		goto exit_fail;
	}

	for (i = 0; i < 2; i++) {
		if (_configure_common_socket(knet_h, sock[i], "local socketpair") < 0) {
			savederrno = errno;
			err = -1;
			goto exit_fail;
		}
	}

exit_fail:
	errno = savederrno;
	return err;
}

void _close_socketpair(knet_handle_t knet_h, int *sock)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (sock[i]) {
			close(sock[i]);
			sock[i] = 0;
		}
	}
}

/*
 * must be called with global read lock
 *
 * return -1 on error
 * return 0 if fd is invalid
 * return 1 if fd is valid
 */
int _is_valid_fd(knet_handle_t knet_h, int sockfd)
{
	int ret = 0;

	if (sockfd < 0) {
		errno = EINVAL;
		return -1;
	}

	if (sockfd > KNET_MAX_FDS) {
		errno = EINVAL;
		return -1;
	}

	if (knet_h->knet_transport_fd_tracker[sockfd].transport >= KNET_MAX_TRANSPORTS) {
		ret = 0;
	} else {
		ret = 1;
	}

	return ret;
}

/*
 * must be called with global write lock
 */

int _set_fd_tracker(knet_handle_t knet_h, int sockfd, uint8_t transport, uint8_t data_type, void *data)
{
	if (sockfd < 0) {
		errno = EINVAL;
		return -1;
	}

	if (sockfd > KNET_MAX_FDS) {
		errno = EINVAL;
		return -1;
	}

	knet_h->knet_transport_fd_tracker[sockfd].transport = transport;
	knet_h->knet_transport_fd_tracker[sockfd].data_type = data_type;
	knet_h->knet_transport_fd_tracker[sockfd].data = data;

	return 0;
}

/*
 * public api
 */

int knet_handle_get_transport_list(knet_handle_t knet_h,
				   struct transport_info *transport_list, size_t *transport_list_entries)
{
	int err = 0, savederrno = 0;
	int i, count;

	if (!knet_h) {
		errno = EINVAL;
		return -1;
	}

	if (!transport_list) {
		errno = EINVAL;
		return -1;
	}

	if (!transport_list_entries) {
		errno = EINVAL;
		return -1;
	}

	savederrno = pthread_rwlock_rdlock(&knet_h->global_rwlock);
	if (savederrno) {
		log_err(knet_h, KNET_SUB_HANDLE, "Unable to get read lock: %s",
			strerror(savederrno));
		errno = savederrno;
		return -1;
	}

	count = 0;

	/*
	 * we could potentially build this struct
	 * at knet_handle_new init time, but
	 * let's keep it dynamic in case at somepoint
	 * we need to init transports dynamically
	 * at runtime vs init time.
	 */

	for (i=0; i<KNET_MAX_TRANSPORTS; i++) {
		if (knet_h->transport_ops[i]) {
			transport_list[count].name = knet_h->transport_ops[i]->transport_name;
			transport_list[count].id = knet_h->transport_ops[i]->transport_id;
			count++;
		}
	}

	*transport_list_entries = count;

	pthread_rwlock_unlock(&knet_h->global_rwlock);

	return err;
}

const char *knet_handle_get_transport_name_by_id(knet_handle_t knet_h, uint8_t transport)
{
	int savederrno = 0;
	const char *name = NULL;

	if (!knet_h) {
		errno = EINVAL;
		return name;
	}

	if (transport >= KNET_MAX_TRANSPORTS) {
		errno = EINVAL;
		return name;
	}

	savederrno = pthread_rwlock_rdlock(&knet_h->global_rwlock);
	if (savederrno) {
		log_err(knet_h, KNET_SUB_HANDLE, "Unable to get read lock: %s",
			strerror(savederrno));
		errno = savederrno;
		return name;
	}

	if (knet_h->transport_ops[transport]) {
		name = knet_h->transport_ops[transport]->transport_name;
	} else {
		savederrno = ENOENT;
	}

	pthread_rwlock_unlock(&knet_h->global_rwlock);
	errno = savederrno;
	return name;
}

uint8_t knet_handle_get_transport_id_by_name(knet_handle_t knet_h, const char *name)
{
	int savederrno = 0;
	uint8_t err = KNET_MAX_TRANSPORTS;
	int i;

	if (!knet_h) {
		errno = EINVAL;
		return err;
	}

	if (!name) {
		errno = EINVAL;
		return err;
	}

	savederrno = pthread_rwlock_rdlock(&knet_h->global_rwlock);
	if (savederrno) {
		log_err(knet_h, KNET_SUB_HANDLE, "Unable to get read lock: %s",
			strerror(savederrno));
		errno = savederrno;
		return err;
	}

	for (i=0; i<KNET_MAX_TRANSPORTS; i++) {
		if (knet_h->transport_ops[i]) {
			if (!strcmp(knet_h->transport_ops[i]->transport_name, name)) {
				err = knet_h->transport_ops[i]->transport_id;
				break;
			}
		}
	}

	if (err == KNET_MAX_TRANSPORTS) {
		savederrno = EINVAL;
	}

	pthread_rwlock_unlock(&knet_h->global_rwlock);
	errno = savederrno;
	return err;
}