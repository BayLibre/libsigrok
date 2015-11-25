/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Hubert Chaumette <hchaumette@baylibre.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_GENERIC_IIO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_GENERIC_IIO_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <unistd.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "generic-iio"

#define SAMPLES_PER_READ	256

/** Private, per-device-instance driver context. */
struct device_priv {
	uint64_t limit_samples;
	uint64_t limit_msec;

	uint32_t num_channels;
	uint64_t samples_read;
	uint64_t samples_missed;
	int64_t start_time;
	int64_t last_sample_fin;

	struct iio_device *iio_dev;
	struct iio_buffer *iio_buf;
};

SR_PRIV struct sr_dev_inst *gen_iio_register_dev(struct sr_dev_driver *sdi,
						 struct iio_device *iiodev);

SR_PRIV int gen_iio_receive_data(int fd, int revents, void *cb_data);

#endif
