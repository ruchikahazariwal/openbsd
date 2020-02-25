/*	$OpenBSD: vmd.c,v 1.115 2019/08/14 07:34:49 anton Exp $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* cmpe */

/* taken from vmd.c */
#include <sys/param.h>	/* nitems */
#include <sys/queue.h>
#include <sys/wait.h>
#include <sys/cdefs.h>
#include <sys/stat.h>
#include <sys/tty.h>
#include <sys/ttycom.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <util.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#include <machine/specialreg.h>
#include <machine/vmmvar.h>

#include "proc.h"
#include "atomicio.h"
#include "vmd.h"

/* taken from viomb.h */
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/task.h>
#include <sys/pool.h>
#include <sys/sensors.h>

#include <uvm/uvm_extern.h>

#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>
#include <virtio.h>
#define VIOMBH_DEBUG 1

//void	viombh_worker(void *);
//void	viombh_inflate(struct viombh_softc *);
//void	viombh_deflate(struct viombh_softc *);
//int	viombh_config_change(struct virtio_softc *);
//void	viombh_read_config(struct viombh_softc *);
//void	viombh_vq_dequeue();
//int	viombh_inflate_intr(struct virtqueue *);
//int	viombh_deflate_intr(struct virtqueue *);
//int	viombh_stat_intr(struct virtqueue *);

struct virtio_balloon
{
	struct device *vdev;
	struct virtqueue *inflate_vq, *deflate_vq, *stats_vq;

	unsigned int num_pfns;
	uint32_t pfns[256];

	/* Memory statistics */
	struct virtio_balloon_stat stats[VIRTIO_BALLOON_S_NR];
};

static inline void update_stat(struct virtio_balloon *vb, int idx,
			       uint16_t tag, uint64_t val)
{
	//BUG_ON(idx >= VIRTIO_BALLOON_S_NR); -- need this
	vb->stats[idx].tag = tag;
	vb->stats[idx].val = val;
}

//#define pages_to_bytes(x) ((uint64_t)(x) << PAGE_SHIFT)
#define pages_to_bytes(x) ((uint64_t)(x) << 4096)

static void update_balloon_stats(struct virtio_balloon *vb)
{
	//unsigned long events[NR_VM_EVENT_ITEMS];
	unsigned long events[5];
	struct virtio_softc soft;
	int idx = 0;

	//all_vm_events(events);
	//si_meminfo(&i);

	// update_stat(vb, idx++, VIRTIO_BALLOON_S_SWAP_IN,
	// 			pages_to_bytes(events[PSWPIN]));
	// update_stat(vb, idx++, VIRTIO_BALLOON_S_SWAP_OUT,
	// 			pages_to_bytes(events[PSWPOUT]));
	// update_stat(vb, idx++, VIRTIO_BALLOON_S_MAJFLT, events[PGMAJFAULT]);
	// update_stat(vb, idx++, VIRTIO_BALLOON_S_MINFLT, events[PGFAULT]);
	// update_stat(vb, idx++, VIRTIO_BALLOON_S_MEMFREE,
	// 		pages_to_bytes(soft.freeram));
	// update_stat(vb, idx++, VIRTIO_BALLOON_S_MEMTOT,
	// 		pages_to_bytes(soft.totalram));
	update_stat(vb, idx++, VIRTIO_BALLOON_S_SWAP_IN,
 			pages_to_bytes(events[1]));
	update_stat(vb, idx++, VIRTIO_BALLOON_S_SWAP_OUT,
	 		pages_to_bytes(events[2]));
	update_stat(vb, idx++, VIRTIO_BALLOON_S_MAJFLT, events[3]);
	update_stat(vb, idx++, VIRTIO_BALLOON_S_MINFLT, events[4]);
// 	update_stat(vb, idx++, VIRTIO_BALLOON_S_MEMFREE,
// 			pages_to_bytes(soft.freeram));
// 	update_stat(vb, idx++, VIRTIO_BALLOON_S_MEMTOT,
// 			pages_to_bytes(soft.totalram));
  }

static int init(struct device *vdev) // device init code
{
	struct virtio_balloon *vb;
	vb->vdev = vdev;
	struct virtqueue *vqs[3];
	//vq_callback_t *callbacks[] = { balloon_ack, balloon_ack, stats_ack };
	const char *names[] = { "inflate", "deflate", "stats" };
	int err, nvqs;
	// nvqs = virtio_has_feature(vb->vdev, VIRTIO_BALLOON_F_STATS_VQ) ? 3 : 2;
}

static unsigned int features[] = {
	VIRTIO_BALLOON_F_MUST_TELL_HOST,
	VIRTIO_BALLOON_F_STATS_VQ,
};

//removed the dequeue function from here
