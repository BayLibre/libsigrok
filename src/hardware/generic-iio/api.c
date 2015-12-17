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
#include <string.h>

SR_PRIV struct sr_dev_driver generic_iio_driver_info;

/*
 * FIXME Update options
 */

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
	struct sr_dev_inst *sdi;
	struct iio_device *iiodev;
	GSList *devices;
	unsigned int nb_devices;
	unsigned int major;
	unsigned int minor;
	unsigned int i;
	char git_tag[8];

	(void)options;

	drvc = di->context;
	devices = NULL;

	iio_library_get_version(&major, &minor, git_tag);
	sr_dbg("LibIIO version: %u.%u (git tag: %s)", major, minor, git_tag);

	iio_ctx = iio_create_default_context();

	if (!iio_ctx) {
		sr_err("Unable to create IIO context");
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Generic IIO");
	sdi->model = g_strdup("Generic IIO");
	sdi->driver = di;

	nb_devices = iio_context_get_devices_count(iio_ctx);
	sr_dbg("IIO context has %u device(s)", nb_devices);

	/* Create a sr_channel_group per IIO device. */
	for (i = 0; i < nb_devices; i++) {
		iiodev = iio_context_get_device(iio_ctx, i);
		gen_iio_channel_group_register(sdi, iiodev);
	}

	devices = g_slist_append(devices, sdi);
	drvc->instances = g_slist_append(drvc->instances, sdi);

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
		      const struct sr_channel_group *chg)
{
	struct channel_group_priv *chgp;
	int ret;

	(void)sdi;

	if (!chg)
		return SR_ERR_NA;

	chgp = chg->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(chgp->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(chgp->limit_msec);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *chg)
{
	struct channel_group_priv *chgp;
	GSList *chgl;
	int ret;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	ret = SR_OK;
	/*
	 * FIXME either set options per channel group or have them shared across
	 * all channel groups.
	 */
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		for (chgl = sdi->channel_groups; chgl; chgl = chgl->next) {
			chg = chgl->data;
			chgp = chg->priv;

			chgp->limit_samples = g_variant_get_uint64(data);
			chgp->limit_msec = 0;
		}
		break;
	case SR_CONF_LIMIT_MSEC:
		for (chgl = sdi->channel_groups; chgl; chgl = chgl->next) {
			chg = chgl->data;
			chgp = chg->priv;

			chgp->limit_msec = g_variant_get_uint64(data) * 1000;
			chgp->limit_samples = 0;
		}
		break;
	case SR_CONF_CONTINUOUS:
		for (chgl = sdi->channel_groups; chgl; chgl = chgl->next) {
			chg = chgl->data;
			chgp = chg->priv;

			chgp->limit_msec = 0;
			chgp->limit_samples = 0;
		}
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *chg)
{
	int ret = SR_OK;

	(void)sdi;
	(void)chg;

	switch (key) {
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
	struct sr_channel_group *chg;
	struct channel_group_priv *chgp;
	struct iio_buffer *iiobuf;
	int fd;
	GSList *chgl;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* Network context */
	if (!strcmp(iio_context_get_name(iio_ctx), "network")) {
		for (chgl = sdi->channel_groups; chgl; chgl = chgl->next) {
			chg = chgl->data;
			chgp = chg->priv;
			chgp->samples_read = 0;
			chgp->samples_missed = 0;

			/*
			 * FIXME Stream continuously instead... Use samplerate as a limit only?
			 * TODO Replace by timer-based IIO trigger if available?
			 */

			struct iio_device *iiodev = chgp->iio_dev;
			/* Allocate buffer for n multichannel samples */
			if (!chgp->iio_buf)
				chgp->iio_buf = iio_device_create_buffer(iiodev, SAMPLES_PER_READ, false);
			sr_dbg("%d samples big IIO buffer allocated", SAMPLES_PER_READ);

			iiobuf = chgp->iio_buf;
			if (!iiobuf) {
				sr_err("Unable to allocate IIO buffer");
				return SR_ERR;
			}

			chgp->start_time = g_get_monotonic_time();
		}

		/* Take latest iiobuf */
		fd = iio_buffer_get_poll_fd(iiobuf);
		sr_session_source_add(sdi->session, fd, POLLIN, 100,
				      gen_iio_receive_data_all, (void *)sdi);

	/* Not network context */
	} else {
		for (chgl = sdi->channel_groups; chgl; chgl = chgl->next) {
			chg = chgl->data;
			chgp = chg->priv;
			chgp->samples_read = 0;
			chgp->samples_missed = 0;

			/*
			 * FIXME Stream continuously instead... Use samplerate as a limit only?
			 * TODO Replace by timer-based IIO trigger if available?
			 */

			struct iio_device *iiodev = chgp->iio_dev;
			/* TODO Integrate buffer_size parameter into sigrok configuration */
			/* Allocate buffer for n multichannel samples */
			if (!chgp->iio_buf)
				chgp->iio_buf = iio_device_create_buffer(iiodev, SAMPLES_PER_READ, false);
			sr_dbg("%d samples big IIO buffer allocated", SAMPLES_PER_READ);

			iiobuf = chgp->iio_buf;
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
					      gen_iio_receive_data, (void *)chg);

			chgp->start_time = g_get_monotonic_time();
		}
	}

	/* Send header packet to the session bus. */
	std_session_send_df_header(sdi, LOG_PREFIX);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_channel_group *chg;
	struct channel_group_priv *chgp;
	struct iio_buffer *iiobuf;
	GSList *chgl;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	for (chgl = sdi->channel_groups; chgl; chgl = chgl->next) {
		chg = chgl->data;
		chgp = chg->priv;
		iiobuf = chgp->iio_buf;

		sr_session_source_remove(sdi->session,
					 iio_buffer_get_poll_fd(iiobuf));

		if (chgp->samples_missed > 0)
			sr_warn("%" PRIu64 " samples missed",
				chgp->samples_missed);

		iio_buffer_destroy(chgp->iio_buf);
	}

	/* Send last packet. */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

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
