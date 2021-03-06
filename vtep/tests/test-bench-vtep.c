/* Copyright 2015 Outscale SAS
 *
 * This file is part of Butterfly.
 *
 * Butterfly is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as published
 * by the Free Software Foundation.
 *
 * Butterfly is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Butterfly.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <rte_config.h>
#include <rte_common.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <packetgraph/packetgraph.h>
#include <packetgraph/packets.h>
#include <packetgraph/brick.h>
#include <packetgraph/vtep.h>
#include <packetgraph/nop.h>
#include <packetgraph/utils/bench.h>
#include <packetgraph/utils/mempool.h>
#include <packetgraph/utils/bitmask.h>

void test_benchmark_vtep(void);

static void inside_to_vxlan(void)
{
	struct pg_error *error = NULL;
	struct pg_brick *vtep;
	struct pg_bench bench;
	struct pg_bench_stats stats;
	struct pg_brick *inside_nop;
	struct ether_addr mac1 = {{0x52,0x54,0x00,0x12,0x34,0x11}};
	struct ether_addr mac2 = {{0x52,0x54,0x00,0x12,0x34,0x21}};
	struct ether_addr mac3 = {{0x52,0x54,0x00,0x12,0x34,0x31}};
	uint32_t len;

	vtep = pg_vtep_new("vtep", 1, 1, EAST_SIDE, inet_addr("192.168.0.1"),
			   mac3, NO_PACKETS_CLEANUP | NO_INNERMAC_CKECK, &error);
	g_assert(!error);

	inside_nop = pg_nop_new("nop-input", 1, 1, &error);
	bench.input_brick = inside_nop;
	bench.input_side = WEST_SIDE;
	bench.output_brick = vtep;
	bench.output_side = EAST_SIDE;
	bench.output_poll = false;
	bench.max_burst_cnt = 3000000;
	bench.count_brick = pg_nop_new("nop-inside", 1, 1, &error);
	g_assert(!error);
	pg_brick_link(inside_nop, vtep, &error);
	g_assert(!error);
	pg_brick_link(vtep, bench.count_brick, &error);
	g_assert(!error);
	pg_vtep_add_vni(vtep, inside_nop, 0,
			inet_addr("224.0.0.5"), &error);
	g_assert(!error);
	bench.pkts_nb = 64;
	bench.pkts_mask = pg_mask_firsts(64);
	bench.pkts = pg_packets_create(bench.pkts_mask);
	bench.pkts = pg_packets_append_ether(
		bench.pkts,
		bench.pkts_mask,
		&mac1, &mac2,
		ETHER_TYPE_IPv4);
	len = sizeof(struct ipv4_hdr) + sizeof(struct udp_hdr) + 1356;
	pg_packets_append_ipv4(
		bench.pkts,
		bench.pkts_mask,
		0x000000EE, 0x000000CC, len, 17);
	bench.pkts = pg_packets_append_udp(
		bench.pkts,
		bench.pkts_mask,
		1000, 2000, 1356);
	bench.pkts = pg_packets_append_blank(bench.pkts, bench.pkts_mask, 1356);

	//g_assert(pg_bench_run(&bench, &stats, &error));
	pg_bench_run(&bench, &stats, &error);
	pg_error_print(error);

	/* We know that this brick burst all packets. */
	stats.pkts_burst = stats.pkts_sent;
	printf("[inside] ==> [vtep] ==> [count] (VXLAN side)\n");
	g_assert(pg_bench_print(&stats, NULL));

	pg_packets_free(bench.pkts, bench.pkts_mask);
	pg_brick_destroy(vtep);
	pg_brick_destroy(inside_nop);
	pg_brick_destroy(bench.count_brick);
}

static void vxlan_to_inside(void)
{
	struct pg_error *error = NULL;
	struct pg_brick *vtep;
	struct pg_bench bench;
	struct pg_bench_stats stats;
	struct pg_brick *outside_nop;
	struct ether_addr mac_vtep = {{0xb0,0xb1,0xb2,0xb3,0xb4,0xb5}};
	struct ether_addr mac1 = {{0x52,0x54,0x00,0x12,0x34,0x11}};
	struct ether_addr mac2 = {{0x52,0x54,0x00,0x12,0x34,0x21}};
	struct ether_addr mac3 = {{0x52,0x54,0x00,0x12,0x34,0x31}};
	struct ether_addr mac4 = {{0x52,0x54,0x00,0x12,0x34,0x41}};
	//struct ether_addr macff = {{0xff,0xff,0xff,0xff,0xff,0xff}};
	uint32_t len;

	vtep = pg_vtep_new("vtep", 1, 1, WEST_SIDE, 0x000000EE,
			   mac_vtep, NO_INNERMAC_CKECK, &error);
	g_assert(!error);

	outside_nop = pg_nop_new("nop-outside", 1, 1, &error);
	bench.input_brick = outside_nop;
	bench.input_side = WEST_SIDE;
	bench.output_brick = vtep;
	bench.output_side = EAST_SIDE;
	bench.output_poll = false;
	bench.max_burst_cnt = 1000000;
	bench.count_brick = pg_nop_new("nop-bench", 1, 1, &error);
	g_assert(!error);
	pg_brick_link(outside_nop, vtep, &error);
	g_assert(!error);
	pg_brick_link(vtep, bench.count_brick, &error);
	g_assert(!error);
	pg_vtep_add_vni(vtep, bench.count_brick, 1,
			inet_addr("224.0.0.1"), &error);
	pg_error_print(error);
	g_assert(!error);
	bench.pkts_nb = 64;
	bench.pkts_mask = pg_mask_firsts(64);
	bench.pkts = pg_packets_create(bench.pkts_mask);
	bench.pkts = pg_packets_append_ether(
		bench.pkts,
		bench.pkts_mask,
		&mac1, &mac2,
		ETHER_TYPE_IPv4);
	len = sizeof(struct ipv4_hdr) + sizeof(struct udp_hdr) +
		sizeof(struct vxlan_hdr) + sizeof(struct ether_hdr) + 1400;
	pg_packets_append_ipv4(
		bench.pkts,
		bench.pkts_mask,
		0x000000EE, 0x000000CC, len, 17);
	bench.pkts = pg_packets_append_udp(
		bench.pkts,
		bench.pkts_mask,
		1000, 2000, 1400);
	bench.pkts = pg_packets_append_ether(
		bench.pkts,
		bench.pkts_mask,
		&mac3, &mac4,
		ETHER_TYPE_IPv4);
	bench.pkts = pg_packets_append_blank(bench.pkts, bench.pkts_mask, 1400);

	g_assert(pg_bench_run(&bench, &stats, &error));
	/* We know that this brick burst all packets. */
	stats.pkts_burst = stats.pkts_sent;
	printf("[outside] ==> [vtep] ==> [count] (no VXLAN side)\n");
	g_assert(pg_bench_print(&stats, NULL));

	pg_packets_free(bench.pkts, bench.pkts_mask);
	pg_brick_destroy(vtep);
	pg_brick_destroy(outside_nop);
	pg_brick_destroy(bench.count_brick);
}

void test_benchmark_vtep(void)
{
	inside_to_vxlan();
	vxlan_to_inside();
}

