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

#include "protocol.h"

#include <config.h>
#include <time.h>
#include <iio.h>
#include <poll.h>

SR_PRIV struct sr_dev_driver generic_iio_driver_info;

/*
 * FIXME Update options, remove remaining ACME stuff
 */

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static struct iio_context *iio_ctx;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct iio_device *iiodev;
	GSList *devices;
	GSList *l;
	const char *conn = NULL;
	unsigned int nb_devices;
	unsigned int major;
	unsigned int minor;
	unsigned int i;
	char git_tag[8];
	gboolean use_network = FALSE;

	drvc = di->context;
	devices = NULL;

	iio_library_get_version(&major, &minor, git_tag);
	sr_dbg("LibIIO version: %u.%u (git tag: %s)", major, minor, git_tag);

	for (l = options; l; l = l->next) {
		src = l->data;

		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			use_network = TRUE;
		}
	}

	if (use_network) {
		iio_ctx = iio_create_network_context(conn);
	} else {
		iio_ctx = iio_create_default_context();
	}

	if (!iio_ctx) {
		sr_err("Unable to create IIO context");
		return NULL;
	}

	nb_devices = iio_context_get_devices_count(iio_ctx);
	sr_dbg("IIO context has %u device(s)", nb_devices);

	/* FIXME sigrk-cli doesn't expect several instances of a device */
	/* Create a sr_dev_inst per IIO device. */
	for (i = 0; i < nb_devices; i++) {
		iiodev = iio_context_get_device(iio_ctx, i);
		sdi = gen_iio_register_dev(di, iiodev);
		if (sdi) {
			devices = g_slist_append(devices, sdi);
			drvc->instances = g_slist_append(drvc->instances, sdi);
		}
	}

	return devices;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, NULL);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	/* Nothing to do here. */
	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	/* Nothing to do here. */
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(const struct sr_dev_driver *di)
{
	dev_clear(di);
	iio_context_destroy(iio_ctx);

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct device_priv *devp;
	int ret;

	(void)cg;

	devp = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devp->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devp->limit_msec);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct device_priv *devp;
	int ret;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devp = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devp->limit_samples = g_variant_get_uint64(data);
		devp->limit_msec = 0;
		break;
	case SR_CONF_LIMIT_MSEC:
		devp->limit_msec = g_variant_get_uint64(data) * 1000;
		devp->limit_samples = 0;
		break;
	case SR_CONF_CONTINUOUS:
		devp->limit_msec = 0;
		devp->limit_samples = 0;
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	int ret = SR_OK;

	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct device_priv *devp;
	struct iio_buffer *iiobuf;
	int fd;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devp = sdi->priv;
	devp->samples_read = 0;
	devp->samples_missed = 0;

	/*
	 * FIXME Stream continuously instead... Use samplerate as a limit only?
	 * TODO Replace by timer-based IIO trigger if available?
	 */

	struct iio_device *iiodev = devp->iio_dev;
	/* TODO Integrate buffer_size parameter into sigrok configuration */
	/* Allocate buffer for n multichannel samples */
	if (!devp->iio_buf)
		devp->iio_buf = iio_device_create_buffer(iiodev, SAMPLES_PER_READ, false);
	sr_dbg("%d samples big IIO buffer allocated", SAMPLES_PER_READ);

	iiobuf = devp->iio_buf;
	if (!iiobuf) {
		sr_err("Unable to allocate IIO buffer");
		return SR_ERR;
	}

	fd = iio_buffer_get_poll_fd(iiobuf);
	/* NOTE always times out with network context */
	/*
	 * FIXME iio_buffer_refill() already does the polling. Maybe replace
	 * with:
	 *
	 * iio_buffer_refill(iiobuf);
	 * // call gen_iio_receive_data() immediately.
	 * sr_session_source_add(sdi->session, -1, 0, 0,
	 * 			 gen_iio_receive_data, (void *)sdi);
	 */
	sr_session_source_add(sdi->session, fd, POLLIN, 100,
			      gen_iio_receive_data, (void *)sdi);

	/* Send header packet to the session bus. */
	std_session_send_df_header(sdi, LOG_PREFIX);
	devp->start_time = g_get_monotonic_time();

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct device_priv *devp;
	struct iio_buffer *iiobuf;

	(void)cb_data;

	devp = sdi->priv;
	iiobuf = devp->iio_buf;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	sr_session_source_remove(sdi->session, iio_buffer_get_poll_fd(iiobuf));

	/* Send last packet. */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	if (devp->samples_missed > 0)
		sr_warn("%" PRIu64 " samples missed", devp->samples_missed);

	iio_buffer_destroy(devp->iio_buf);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver generic_iio_driver_info = {
	.name = "generic-iio",
	.longname = "Generic IIO wrapper",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
