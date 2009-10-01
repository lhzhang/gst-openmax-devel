/*
 * Copyright (C) 2007-2009 Nokia Corporation.
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "gstomx_base_videoenc.h"
#include "gstomx.h"

#include <gst/video/video.h>

#include <string.h> /* for memset, strcmp */

enum
{
    ARG_0,
    ARG_BITRATE,
};

#define DEFAULT_BITRATE 500000

GSTOMX_BOILERPLATE (GstOmxBaseVideoEnc, gst_omx_base_videoenc, GstOmxBaseFilter, GST_OMX_BASE_FILTER_TYPE);


static GstStaticPadTemplate sink_template =
        GST_STATIC_PAD_TEMPLATE ("sink",
                GST_PAD_SINK,
                GST_PAD_ALWAYS,
                GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV_STRIDED (
                        GSTOMX_ALL_FORMATS, "[ 0, max ]"))
        );

static void
type_base_init (gpointer g_class)
{
    GstElementClass *element_class;

    element_class = GST_ELEMENT_CLASS (g_class);


    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&sink_template));
}

static void
set_property (GObject *obj,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    GstOmxBaseVideoEnc *self;

    self = GST_OMX_BASE_VIDEOENC (obj);

    switch (prop_id)
    {
        case ARG_BITRATE:
            self->bitrate = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
}

static void
get_property (GObject *obj,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    GstOmxBaseVideoEnc *self;

    self = GST_OMX_BASE_VIDEOENC (obj);

    switch (prop_id)
    {
        case ARG_BITRATE:
            /** @todo propagate this to OpenMAX when processing. */
            g_value_set_uint (value, self->bitrate);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
}

static void
type_class_init (gpointer g_class,
                 gpointer class_data)
{
    GObjectClass *gobject_class;

    gobject_class = G_OBJECT_CLASS (g_class);

    /* Properties stuff */
    {
        gobject_class->set_property = set_property;
        gobject_class->get_property = get_property;

        g_object_class_install_property (gobject_class, ARG_BITRATE,
                                         g_param_spec_uint ("bitrate", "Bit-rate",
                                                            "Encoding bit-rate",
                                                            0, G_MAXUINT, DEFAULT_BITRATE, G_PARAM_READWRITE));
    }
}

static gboolean
sink_setcaps (GstPad *pad,
              GstCaps *caps)
{
    GstOmxBaseVideoEnc *self;
    GstOmxBaseFilter *omx_base;

    GstVideoFormat format;
    gint width, height, rowstride;
    const GValue *framerate = NULL;

    self = GST_OMX_BASE_VIDEOENC (GST_PAD_PARENT (pad));
    omx_base = GST_OMX_BASE_FILTER (self);

    GST_INFO_OBJECT (omx_base, "setcaps (sink): %" GST_PTR_FORMAT, caps);

    g_return_val_if_fail (caps, FALSE);
    g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

    if (gst_video_format_parse_caps_strided (caps,
            &format, &width, &height, &rowstride))
    {
        /* Output port configuration: */
        OMX_PARAM_PORTDEFINITIONTYPE param;

        g_omx_port_get_config (omx_base->in_port, &param);

        param.format.video.eColorFormat = g_omx_fourcc_to_colorformat (
                gst_video_format_to_fourcc (format));
        param.format.video.nFrameWidth  = width;
        param.format.video.nFrameHeight = height;
        param.format.video.nStride      = rowstride;

        framerate = gst_structure_get_value (
                gst_caps_get_structure (caps, 0), "framerate");

        if (framerate)
        {
            self->framerate_num = gst_value_get_fraction_numerator (framerate);
            self->framerate_denom = gst_value_get_fraction_denominator (framerate);

            /* convert to Q.16 */
            param.format.video.xFramerate =
                (gst_value_get_fraction_numerator (framerate) << 16) /
                gst_value_get_fraction_denominator (framerate);
        }

        g_omx_port_set_config (omx_base->out_port, &param);
    }

    return TRUE;
}

static void
omx_setup (GstOmxBaseFilter *omx_base)
{
    GstOmxBaseVideoEnc *self;
    GOmxCore *gomx;

    self = GST_OMX_BASE_VIDEOENC (omx_base);
    gomx = (GOmxCore *) omx_base->gomx;

    GST_INFO_OBJECT (omx_base, "begin");

    {
        OMX_PARAM_PORTDEFINITIONTYPE param;

        /* Output port configuration. */
        g_omx_port_get_config (omx_base->out_port, &param);

        param.format.video.eCompressionFormat = self->compression_format;

        /** @todo this should be set with a property */
        param.format.video.nBitrate = self->bitrate;

        g_omx_port_set_config (omx_base->out_port, &param);

        /* some workarounds required for TI components. */
        {
            guint32 fourcc;
            gint width, height;
            gulong framerate;

            /* the component should do this instead */
            {
                g_omx_port_get_config (omx_base->in_port, &param);

                width = param.format.video.nFrameWidth;
                height = param.format.video.nFrameHeight;
                framerate = param.format.video.xFramerate;

                /* this is against the standard; nBufferSize is read-only. */
                fourcc = g_omx_colorformat_to_fourcc (param.format.video.eColorFormat);
                param.nBufferSize = gst_video_format_get_size_strided (
                        gst_video_format_from_fourcc (fourcc),
                        width, height, param.format.video.nStride);

                g_omx_port_set_config (omx_base->in_port, &param);
            }

            /* the component should do this instead */
            {
                g_omx_port_get_config (omx_base->out_port, &param);

                /* this is against the standard; nBufferSize is read-only. */
                param.nBufferSize = width * height;

                param.format.video.nFrameWidth = width;
                param.format.video.nFrameHeight = height;
                param.format.video.xFramerate = framerate;

                g_omx_port_set_config (omx_base->out_port, &param);
            }
        }
    }

    if (self->omx_setup)
        self->omx_setup (GST_OMX_BASE_FILTER (self));

    GST_INFO_OBJECT (omx_base, "end");
}

static void
type_instance_init (GTypeInstance *instance,
                    gpointer g_class)
{
    GstOmxBaseFilter *omx_base;
    GstOmxBaseVideoEnc *self;

    omx_base = GST_OMX_BASE_FILTER (instance);
    self = GST_OMX_BASE_VIDEOENC (instance);

    omx_base->omx_setup = omx_setup;

    gst_pad_set_setcaps_function (omx_base->sinkpad, sink_setcaps);

    self->bitrate = DEFAULT_BITRATE;
}
