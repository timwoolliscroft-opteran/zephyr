/* dtls-client.c - dtls client */

/*
 * Copyright (c) 2015 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined(CONFIG_STDOUT_CONSOLE)
#include <stdio.h>
#define PRINT           printf
#else
#include <misc/printk.h>
#define PRINT           printk
#endif

#if defined(CONFIG_TINYDTLS_DEBUG)
#define DEBUG DEBUG_FULL
#else
#define DEBUG DEBUG_PRINT
#endif
#include "contiki/ip/uip-debug.h"

#include <zephyr.h>

#include <drivers/rand32.h>

#include <errno.h>

#include <net/ip_buf.h>
#include <net/net_core.h>
#include <net/net_socket.h>

#include <net/tinydtls.h>

/* Generated by http://www.lipsum.com/
 * 1202 bytes of Lorem Ipsum.
 *
 * This is the maximum we can send with encryption.
 */
static const char lorem_ipsum[] =
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Proin congue orci et lectus ultricies, sed elementum urna finibus. Nam bibendum, massa id sollicitudin finibus, massa ante pharetra lacus, nec semper felis metus eu massa. Curabitur gravida, neque a pulvinar suscipit, felis massa maximus neque, eu sagittis felis enim nec justo. Suspendisse sit amet sem a magna aliquam tincidunt. Mauris consequat ante in consequat auctor. Nam eu congue mauris, congue aliquet metus. Etiam elit ipsum, vehicula et lectus at, dignissim accumsan turpis. Sed magna nisl, tempor ut dolor sed, feugiat pharetra velit. Nulla sed purus at elit dapibus lobortis. In hac habitasse platea dictumst. Praesent quis libero id enim aliquet viverra eleifend non urna. Vivamus metus justo, dignissim eget libero molestie, tincidunt pellentesque purus. Quisque pulvinar, nisi sed egestas vestibulum, ante felis elementum justo, ut viverra nisl est sagittis leo. Curabitur pharetra eros at felis ultricies efficitur."
	"\n"
	"Ut rutrum urna vitae neque rhoncus, id dictum ex dictum. Suspendisse venenatis vel mauris sed maximus. Sed malesuada elit vel neque hendrerit, in accumsan odio sodales. Aliquam erat volutpat. Praesent non situ.\n";

struct data {
	bool fail;
	bool connected;
	int expecting;
	int ipsum_len;
	struct net_context *ctx;
};

static const unsigned char ecdsa_priv_key[] = {
			0xD9, 0xE2, 0x70, 0x7A, 0x72, 0xDA, 0x6A, 0x05,
			0x04, 0x99, 0x5C, 0x86, 0xED, 0xDB, 0xE3, 0xEF,
			0xC7, 0xF1, 0xCD, 0x74, 0x83, 0x8F, 0x75, 0x70,
			0xC8, 0x07, 0x2D, 0x0A, 0x76, 0x26, 0x1B, 0xD4};

static const unsigned char ecdsa_pub_key_x[] = {
			0xD0, 0x55, 0xEE, 0x14, 0x08, 0x4D, 0x6E, 0x06,
			0x15, 0x59, 0x9D, 0xB5, 0x83, 0x91, 0x3E, 0x4A,
			0x3E, 0x45, 0x26, 0xA2, 0x70, 0x4D, 0x61, 0xF2,
			0x7A, 0x4C, 0xCF, 0xBA, 0x97, 0x58, 0xEF, 0x9A};

static const unsigned char ecdsa_pub_key_y[] = {
			0xB4, 0x18, 0xB6, 0x4A, 0xFE, 0x80, 0x30, 0xDA,
			0x1D, 0xDC, 0xF4, 0xF4, 0x2E, 0x2F, 0x26, 0x31,
			0xD0, 0x43, 0xB1, 0xFB, 0x03, 0xE2, 0x2F, 0x4D,
			0x17, 0xDE, 0x43, 0xF9, 0xF9, 0xAD, 0xEE, 0x70};

#ifdef CONFIG_NETWORKING_IPV6_NO_ND
/* The peer is the server in our case. Just invent a mac
 * address for it because lower parts of the stack cannot set it
 * in this test as we do not have any radios.
 */
static uint8_t peer_mac[] = { 0x15, 0x0a, 0xbe, 0xef, 0xf0, 0x0d };
#endif

/* This is my mac address
 */
static uint8_t my_mac[] = { 0x0a, 0xbe, 0xef, 0x15, 0xf0, 0x0d };

#ifdef CONFIG_NETWORKING_WITH_IPV6
#if 0
/* The 2001:db8::/32 is the private address space for documentation RFC 3849 */
#define MY_IPADDR { { { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x2 } } }
#define PEER_IPADDR { { { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1 } } }
#else
#define MY_IPADDR { { { 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0x17, 0x0a, 0xbe, 0xef, 0xf0, 0x0d, 0 } } }
#define PEER_IPADDR { { { 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0x08, 0xbe, 0xef, 0x15, 0xf0, 0x0d, 0 } } }
#endif
#else /* ipv6 */
/* The 192.0.2.0/24 is the private address space for documentation RFC 5737 */
#define MY_IPADDR { { { 192, 0, 2, 2 } } }
#define PEER_IPADDR { { { 192, 0, 2, 1 } } }

#endif
#define MY_PORT 8484
#define PEER_PORT 4242

#ifdef CONFIG_NETWORKING_WITH_IPV6
static const struct in6_addr in6addr_peer = PEER_IPADDR;
static struct in6_addr in6addr_my = MY_IPADDR;
#else
static struct in_addr in4addr_peer = PEER_IPADDR;
static struct in_addr in4addr_my = MY_IPADDR;
#endif

static inline void init_app(void)
{
	PRINT("%s: run dtls client\n", __func__);

	net_set_mac(my_mac, sizeof(my_mac));

#ifdef CONFIG_NETWORKING_WITH_IPV4
	{
		uip_ipaddr_t addr;
		uip_ipaddr(&addr, 192,0,2,2);
		uip_sethostaddr(&addr);
	}
#endif

#ifdef CONFIG_NETWORKING_IPV6_NO_ND
	{
		uip_ipaddr_t *addr;
		const uip_lladdr_t *lladdr = (const uip_lladdr_t *)&peer_mac;

		addr = (uip_ipaddr_t *)&in6addr_peer;
		uip_ds6_defrt_add(addr, 0);

		/* We cannot send to peer unless it is in neighbor
		 * cache. Neighbor cache should be populated automatically
		 * but do it here so that test works from first packet.
		 */
		uip_ds6_nbr_add(addr, lladdr, 0, NBR_REACHABLE);

		addr = (uip_ipaddr_t *)&in6addr_my;
		uip_ds6_addr_add(addr, 0, ADDR_MANUAL);
	}
#endif
}

static inline void reverse(unsigned char *buf, int len)
{
	int i, last = len - 1;

	for (i = 0; i < len / 2; i++) {
		unsigned char tmp = buf[i];
		buf[i] = buf[last - i];
		buf[last - i] = tmp;
	}
}

/* How many tics to wait for a network packet */
#define WAIT_TIME 5
#define WAIT_TICKS (WAIT_TIME * sys_clock_ticks_per_sec)

static inline void send_message(const char *name,
				dtls_context_t *ctx,
				session_t *session)
{
	struct data *user_data = (struct data *)dtls_get_app_data(ctx);
	struct net_buf *buf;

	buf = ip_buf_get_reserve_tx(UIP_IPUDPH_LEN);
	if (buf) {
		uint8_t *ptr;
		int pos = sys_rand32_get() % user_data->ipsum_len;

		user_data->expecting = user_data->ipsum_len - pos;

		ptr = net_buf_add(buf, user_data->expecting);
		memcpy(ptr, lorem_ipsum + pos, user_data->expecting);
		ip_buf_appdatalen(buf) = user_data->expecting;

		dtls_write(ctx, session,
			   ip_buf_appdata(buf), ip_buf_appdatalen(buf));

		/* The encrypted data to peer is actually sent by
		 * send_to_peer() so we need to release the buffer
		 * here.
		 */
		ip_buf_unref(buf);
	}
}

static inline bool wait_reply(const char *name,
			      struct dtls_context_t *dtls,
			      session_t *session)
{
	struct data *user_data = (struct data *)dtls_get_app_data(dtls);
	struct net_buf *buf;

	/* Wait for the answer */
	buf = net_receive(user_data->ctx, WAIT_TICKS);
	if (buf) {
		PRINT("Received data %p datalen %d\n",
		      ip_buf_appdata(buf), ip_buf_appdatalen(buf));

		dtls_handle_message(dtls, session, ip_buf_appdata(buf),
				    ip_buf_appdatalen(buf));

		ip_buf_unref(buf);
		return true;
	}

	return false;
}

static inline struct net_context *get_context(void)
{
	static struct net_addr peer_addr;
	static struct net_addr my_addr;
	struct net_context *ctx;

#ifdef CONFIG_NETWORKING_WITH_IPV6
	peer_addr.in6_addr = in6addr_peer;
	peer_addr.family = AF_INET6;

	my_addr.in6_addr = in6addr_my;
	my_addr.family = AF_INET6;
#else
	peer_addr.in_addr = in4addr_peer;
	peer_addr.family = AF_INET;

	my_addr.in_addr = in4addr_my;
	my_addr.family = AF_INET;
#endif

	ctx = net_context_get(IPPROTO_UDP,
			      &peer_addr, PEER_PORT,
			      &my_addr, MY_PORT);
	if (!ctx) {
		PRINT("%s: Cannot get network context\n", __func__);
		return NULL;
	}

	return ctx;
}

static int read_from_peer(struct dtls_context_t *ctx,
			  session_t *session,
			  uint8 *data, size_t len)
{
	struct data *user_data = (struct data *)dtls_get_app_data(ctx);
	int pos;

	PRINT("%s: read from peer %p len %d\n", __func__, data, len);

	if (user_data->expecting != len) {
		PRINT("%s: received %d bytes, expected %d\n",
		      __func__, len, user_data->expecting);
		user_data->fail = true;
		return 0;
	}

	/* In this test we reverse the received bytes.
	 * We could also just pass the data back as is.
	 */
	reverse(data, len);

	/* Did we get all the data back?
	 */
	pos = user_data->ipsum_len - user_data->expecting;

	if (memcmp(lorem_ipsum + pos, data, user_data->expecting)) {
		PRINT("%s: received data mismatch.\n", __func__);
		user_data->fail = true;
	}

	return 0;
}

static int send_to_peer(struct dtls_context_t *ctx,
			session_t *session,
			uint8 *data, size_t len)
{
	struct data *user_data = (struct data *)dtls_get_app_data(ctx);
	struct net_buf *buf;
	int max_data_len;
	uint8_t *ptr;

	buf = ip_buf_get_tx(user_data->ctx);
	if (!buf) {
		len = -ENOBUFS;
		goto out;
	}

	max_data_len = IP_BUF_MAX_DATA - UIP_IPUDPH_LEN;

	PRINT("%s: send to peer data %p len %d\n", __func__, data, len);

	if (len > max_data_len) {
		PRINT("%s: too much (%d bytes) data to send (max %d bytes)\n",
		      __func__, len, max_data_len);
		ip_buf_unref(buf);
		len = -EINVAL;
		goto out;
	}

	ptr = net_buf_add(buf, len);
	memcpy(ptr, data, len);
	ip_buf_appdatalen(buf) = len;

	if (net_send(buf)) {
		ip_buf_unref(buf);
	}

out:
	return len;
}

#ifdef DTLS_PSK
/* This function is the "key store" for tinyDTLS. It is called to
 * retrieve a key for the given identity within this particular
 * session. */
static int get_psk_info(struct dtls_context_t *ctx,
			const session_t *session,
			dtls_credentials_type_t type,
			const unsigned char *id, size_t id_len,
			unsigned char *result, size_t result_length)
{
	struct keymap_t {
		unsigned char *id;
		size_t id_length;
		unsigned char *key;
		size_t key_length;
	} psk[3] = {
		{ (unsigned char *)"Client_identity", 15,
		  (unsigned char *)"secretPSK", 9 },
		{ (unsigned char *)"default identity", 16,
		  (unsigned char *)"\x11\x22\x33", 3 },
		{ (unsigned char *)"\0", 2,
		  (unsigned char *)"", 1 }
	};

	if (type != DTLS_PSK_KEY) {
		return 0;
	}

	if (id) {
		int i;
		for (i = 0; i < sizeof(psk)/sizeof(struct keymap_t); i++) {
			if (id_len == psk[i].id_length &&
			    memcmp(id, psk[i].id, id_len) == 0) {
				if (result_length < psk[i].key_length) {
					dtls_warn("buffer too small for PSK");
					return dtls_alert_fatal_create(
						DTLS_ALERT_INTERNAL_ERROR);
				}

				memcpy(result, psk[i].key, psk[i].key_length);
				return psk[i].key_length;
			}
		}
	}

	return dtls_alert_fatal_create(DTLS_ALERT_DECRYPT_ERROR);
}
#endif /* DTLS_PSK */

#ifdef DTLS_ECC
static int get_ecdsa_key(struct dtls_context_t *ctx,
			 const session_t *session,
			 const dtls_ecdsa_key_t **result)
{
	static const dtls_ecdsa_key_t ecdsa_key = {
		.curve = DTLS_ECDH_CURVE_SECP256R1,
		.priv_key = ecdsa_priv_key,
		.pub_key_x = ecdsa_pub_key_x,
		.pub_key_y = ecdsa_pub_key_y
	};

	*result = &ecdsa_key;
	return 0;
}

static int verify_ecdsa_key(struct dtls_context_t *ctx,
			    const session_t *session,
			    const unsigned char *other_pub_x,
			    const unsigned char *other_pub_y,
			    size_t key_size)
{
	return 0;
}
#endif /* DTLS_ECC */

static int handle_event(struct dtls_context_t *ctx, session_t *session,
			dtls_alert_level_t level, unsigned short code)
{
	dtls_debug("event: level %d code %d\n", level, code);

	if (level > 0) {
		/* alert code, quit */
	} else if (level == 0) {
		/* internal event */
		if (code == DTLS_EVENT_CONNECTED) {
			struct data *user_data =
				(struct data *)dtls_get_app_data(ctx);

			PRINT("*** Connected ***\n");

			/* We can send data now */
			user_data->connected = true;
		}
	}

	return 0;
}

static void init_dtls(struct data *user_data, dtls_context_t **dtls)
{
	static dtls_handler_t cb = {
		.write = send_to_peer,
		.read  = read_from_peer,
		.event = handle_event,
#ifdef DTLS_PSK
		.get_psk_info = get_psk_info,
#endif /* DTLS_PSK */
#ifdef DTLS_ECC
		.get_ecdsa_key = get_ecdsa_key,
		.verify_ecdsa_key = verify_ecdsa_key
#endif /* DTLS_ECC */
	};

	PRINT("DTLS client started\n");

#ifdef CONFIG_TINYDTLS_DEBUG
	dtls_set_log_level(DTLS_LOG_DEBUG);
#endif

	*dtls = dtls_new_context(user_data);
	if (*dtls) {
		dtls_set_handler(*dtls, &cb);
	}
}

void startup(void)
{
	static dtls_context_t *dtls;
	static session_t session;
	static struct data user_data;

	user_data.ipsum_len = strlen(lorem_ipsum);

	net_init();

	dtls_init();

	init_app();

	user_data.ctx = get_context();
	if (!user_data.ctx) {
		PRINT("%s: Cannot get network context\n", __func__);
		return;
	}

	init_dtls(&user_data, &dtls);
	if (!dtls) {
		PRINT("%s: Cannot get DTLS context\n", __func__);
		return;
	}

	dtls_session_init(&session);

	uip_ipaddr_copy(&session.addr.ipaddr, (uip_ipaddr_t *)&in6addr_peer);
	session.addr.port = uip_htons(PEER_PORT);

	PRINT("Trying to connect to ");
	PRINT6ADDR(&session.addr.ipaddr);
	PRINTF(":%d\n", uip_ntohs(session.addr.port));

	dtls_connect(dtls, &session);

	while (!user_data.fail) {
		if (user_data.connected) {
			send_message(__func__, dtls, &session);
		}
		if (!wait_reply(__func__, dtls, &session)) {
			if (user_data.connected) {
				break;
			}
		}
	}

	PRINT("Did not receive reply, closing.\n");
	dtls_close(dtls, &session);
}

#ifdef CONFIG_NANOKERNEL

#define STACKSIZE 3000
char fiberStack[STACKSIZE];

void main(void)
{
	fiber_start(&fiberStack[0], STACKSIZE,
			(nano_fiber_entry_t)startup, 0, 0, 7, 0);
}

#endif /* CONFIG_NANOKERNEL */
