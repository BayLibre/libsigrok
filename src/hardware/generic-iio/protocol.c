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

#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <glib/gstdio.h>
#include <iio.h>

#include "protocol.h"

/** Private, per-channel data. */
struct channel_priv {
	struct iio_channel *iio_chan;
	/* Data encoding is static in IIO channels */
	struct sr_analog_encoding *encoding;
	struct sr_analog_meaning *meaning;
};

static int chan_priv_init(struct sr_channel *chan)
{
	struct channel_priv *chanp = chan->priv;
	struct iio_channel *iiochan = chanp->iio_chan;
	struct sr_analog_encoding *encoding;
	struct sr_analog_meaning *meaning;
	ssize_t ret;
	char sign;
	char buf[50];
	unsigned int bits; /* Sample size */
	float scale = 1; /* Not taken into account in iio_channel_convert */
	float offset = 0; /* Ditto */

	if (!chanp)
		return SR_ERR;

	/* FIXME free it alongside channel_priv */
	meaning = g_malloc0(sizeof(struct sr_analog_meaning));
	if (!meaning)
		return SR_ERR;

	sscanf(chan->name, "%[^0-9]%*u", buf);
	sr_spew("buf = %s", buf);
	if (!strncmp(buf, "voltage", 50)
	    || !strncmp(buf, "altvoltage", 50)) {
		meaning->mq = SR_MQ_VOLTAGE;
		meaning->unit = SR_UNIT_VOLT;
		/* received in mV */
		scale /= 1000;
	} else if (!strncmp(buf, "current", 50)) {
		meaning->mq = SR_MQ_CURRENT;
		meaning->unit = SR_UNIT_AMPERE;
		/* received in mA */
		scale /= 1000;
	} else if (!strncmp(buf, "power", 50)) {
		meaning->mq = SR_MQ_POWER;
		meaning->unit = SR_UNIT_WATT;
		/* received in mW */
		scale /= 1000;
	} else if (!strncmp(buf, "timestamp", 50)) {
		meaning->mq = SR_MQ_TIME;
		meaning->unit = SR_UNIT_SECOND;
		/* FIXME check received unit */
	} else if (!strncmp(buf, "resistance", 50)) {
		meaning->mq = SR_MQ_RESISTANCE;
		meaning->unit = SR_UNIT_OHM;
	} else if (!strncmp(buf, "capacitance", 50)) {
		meaning->mq = SR_MQ_CAPACITANCE;
		meaning->unit = SR_UNIT_FARAD;
		/* received in nF */
		scale /= 1e9;
	} else if (!strncmp(buf, "temp", 50)) {
		meaning->mq = SR_MQ_TEMPERATURE;
		meaning->unit = SR_UNIT_CELSIUS;
		/* received in millidegrees C */
		scale /= 1000;
	} else if (!strncmp(buf, "pressure", 50)) {
		meaning->mq = SR_MQ_PRESSURE;
		meaning->unit = SR_UNIT_HECTOPASCAL;
		/* received in kilopascal */
		scale *= 10;
	} else if (!strncmp(buf, "humidityrelative", 50)) {
		meaning->mq = SR_MQ_RELATIVE_HUMIDITY;
		meaning->unit = SR_UNIT_PERCENTAGE;
		/* received in millipercent */
		scale /= 1000;
#if 0
	} else if (!strncmp(buf, "accel", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "anglvel", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "magn", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "illuminance", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "intensity", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "proximity", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "incli", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "rot", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "angl", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "cct", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "activity", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "steps", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "energy", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "distance", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "velocity", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
	} else if (!strncmp(buf, "concentration", 50)) {
		meaning->mq = SR_MQ_;
		meaning->unit = SR_UNIT_;
#endif
	} else {
		sr_err("Unknown quantity \"%s\"", buf);
		g_free(meaning);
		return SR_ERR;
	}

	/* FIXME free it alongside channel_priv */
	encoding = g_malloc0(sizeof(struct sr_analog_encoding));
	encoding->is_float = FALSE;

	/*
	 * iio_channel_read() takes care of converting samples to the system's
	 * endianness
	 */
#ifdef WORDS_BIGENDIAN
	encoding->is_bigendian = TRUE;
#else
	encoding->is_bigendian = FALSE;
#endif
	encoding->digits = 3;
	encoding->is_digits_decimal = TRUE;

	ret = iio_channel_attr_read(iiochan, "type", buf, sizeof(buf));
	if (ret < 0) {
		g_free(encoding);
		return SR_ERR;
	}

	sscanf(buf, "%*ce:%c%*u/%u>>%*u", &sign, &bits);
	encoding->is_signed = (sign == 's' || sign == 'S');
	encoding->unitsize = bits / CHAR_BIT;
	sr_dbg("IIO channel type: %s (signed: %u; bits: %u)",
	       buf, encoding->is_signed, bits);

	/* TODO handle device-wide scale/offset attributes */

	ret = iio_channel_attr_read(iiochan, "offset", buf, sizeof(buf));
	if (ret >= 0) {
		offset += scale * atof(buf);
	}
	/* What if abs(p = offset * q) > INT64_MAX? */
	sr_rational_set(&encoding->offset, offset * 1e6, 1e6);

	ret = iio_channel_attr_read(iiochan, "scale", buf, sizeof(buf));
	if (ret >= 0) {
		scale *= atof(buf);
	}
	/* What if abs(p = scale * q) > INT64_MAX? */
	/* FIXME p < 1 for nF */
	sr_rational_set(&encoding->scale, scale * 1e6, 1e6);

	sr_dbg("scale  = %f", scale);
	sr_spew("       = %ld / %lu", encoding->scale.p, encoding->scale.q);
	sr_dbg("offset = %f", offset);
	sr_spew("       = %f * %ld / %lu", scale,
		encoding->offset.p, encoding->offset.q);

	meaning->channels = g_slist_append(meaning->channels, chan);
	chanp->meaning = meaning;
	chanp->encoding = encoding;

	return SR_OK;
}

SR_PRIV struct sr_dev_inst *gen_iio_register_dev(struct sr_dev_driver *di,
						 struct iio_device *iiodev)
{
	struct device_priv *devp;
	struct sr_dev_inst *sdi;
	struct sr_channel *chan;
	struct iio_channel *iiochan;
	struct channel_priv *chanp;
	unsigned int nb_chan;
	unsigned int i;
	const char *id;
	const char *devname;

	if (!iiodev)
		return NULL;

	devname = iio_device_get_name(iiodev);
	nb_chan = iio_device_get_channels_count(iiodev);
	sr_dbg("IIO device %s has %u channel(s)", devname, nb_chan);

	/* FIXME free it somewhere */
	devp = g_malloc0(sizeof(struct device_priv));
	devp->iio_dev = iiodev;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Generic IIO");
	sdi->model = g_strdup(iio_device_get_name(iiodev));
	sdi->driver = di;
	sdi->priv = devp;

	for (i = 0; i < nb_chan; i++) {
		iiochan = iio_device_get_channel(iiodev, i);
		id = iio_channel_get_id(iiochan);
		sr_dbg("Channel #%d id: %s name: %s dir: %s",
		       i, id, iio_channel_get_name(iiochan),
		       iio_channel_is_output(iiochan) ? "OUT" : "IN");

		/*
		 * FIXME Maybe register each IIO device as a channel group?
		 */

		/* Register only input channels. */
		if (!iio_channel_is_scan_element(iiochan)
		    || iio_channel_is_output(iiochan)) {
			sr_dbg("Non-input channel %s ignored", id);
			continue;
		}

		chan = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, id);
		/* FIXME free it somewhere */
		chanp = g_malloc0(sizeof(struct channel_priv));
		chanp->iio_chan = iiochan;
		chan->priv = chanp;

		if (chan_priv_init(chan) != SR_OK) {
			sdi->channels = g_slist_remove(sdi->channels, chan);
			g_free(chan);
			g_free(chanp);
			continue;
		}

		/* Enable channel by default */
		iio_channel_enable(iiochan);
	}

	if (g_slist_length(sdi->channels) == 0) {
		sr_dbg("No input channels found, ignoring IIO device %s.",
		       devname);
		goto no_channel;
	}

	return sdi;

no_channel:
	g_free(devp);
	g_free(sdi);
	return NULL;
}

static size_t chan_read(struct sr_channel *chan, void *buf, size_t num_samples)
{
	struct device_priv *devp = chan->sdi->priv;
	struct iio_buffer *iiobuf = devp->iio_buf;
	struct channel_priv *chanp = chan->priv;
	struct sr_analog_encoding *encoding = chanp->encoding;
	struct iio_channel *iiochan = chanp->iio_chan;

	size_t read;
	unsigned int bytes = encoding->unitsize;

	read = iio_channel_read(iiochan, iiobuf, buf,
				bytes * num_samples);
	if (num_samples != read / bytes)
		sr_warn("Expected %lu samples, %lu read",
			num_samples, read / bytes);

	sr_dbg("read: %ld samples, %lu bytes", num_samples, read);

	return read / bytes;
}

SR_PRIV int gen_iio_receive_data(int fd, int revents, void *cb_data)
{
	uint32_t cur_time, elapsed_time;
	struct sr_datafeed_packet packet, framep;
	struct sr_datafeed_analog analog;
	struct sr_analog_meaning meaning;
	struct sr_analog_encoding encoding;
	struct sr_analog_spec spec;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct device_priv *devp;
	struct channel_priv *chanp;
	struct iio_buffer *iiobuf;
	struct iio_device *iiodev;
	GSList *chl;
	ssize_t ret;
	uint32_t samples_to_read;
	uint32_t samples_remaining;
	uint32_t tot_samples;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devp = sdi->priv;
	if (!devp)
		return TRUE;

	iiobuf = devp->iio_buf;
	if (!iiobuf)
		return TRUE;

	iiodev = devp->iio_dev;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	int digits = 3; /* FIXME what does it stand for? */
	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);

	/* NOTE Only for device with input channels... */
	ret = iio_buffer_refill(iiobuf);
	if (ret < 0) {
		sr_err("Unable to refill IIO buffer: %s", strerror(-ret));
		return TRUE;
	}
	sr_dbg("%ld bytes read from buffer", ret);

	tot_samples = ret / iio_device_get_sample_size(iiodev);
	if (devp->limit_samples > 0) {
		samples_remaining = devp->limit_samples - devp->samples_read;
		samples_to_read = MIN(tot_samples, samples_remaining);
	} else {
		samples_to_read = tot_samples;
	}

	framep.type = SR_DF_FRAME_BEGIN;
	sr_session_send(sdi, &framep);

	/* FIXME allocate in dev_acquisition_start() */
	int8_t *buf = g_malloc0_n(samples_to_read, sizeof(int64_t));

	for (chl = sdi->channels; chl; chl = chl->next) {
		ch = chl->data;
		chanp = ch->priv;

		analog.encoding = chanp->encoding;
		analog.meaning = chanp->meaning;
		analog.num_samples = chan_read(ch, buf, samples_to_read);

		analog.data = buf;
		sr_session_send(sdi, &packet);
	}

	framep.type = SR_DF_FRAME_END;
	sr_session_send(cb_data, &framep);

	devp->samples_read += analog.num_samples;
	if (devp->limit_samples > 0 &&
	    devp->samples_read >= devp->limit_samples) {
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		devp->last_sample_fin = g_get_monotonic_time();
		return TRUE;
	} else if (devp->limit_msec > 0) {
		cur_time = g_get_monotonic_time();
		elapsed_time = cur_time - devp->start_time;

		if (elapsed_time >= devp->limit_msec) {
			sr_info("Sampling time limit reached.");
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
			devp->last_sample_fin = g_get_monotonic_time();
			return TRUE;
		}
	}

	devp->last_sample_fin = g_get_monotonic_time();
	g_free(buf);

	return TRUE;
}
