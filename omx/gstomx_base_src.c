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

#include "gstomx_base_src.h"
#include "gstomx.h"

#include <string.h> /* for memset, memcpy */

enum
{
    ARG_0,
    ARG_COMPONENT_NAME,
    ARG_LIBRARY_NAME,
};

GSTOMX_BOILERPLATE (GstOmxBaseSrc, gst_omx_base_src, GstBaseSrc, GST_TYPE_BASE_SRC);

static void
setup_ports (GstOmxBaseSrc *self)
{
    OMX_PARAM_PORTDEFINITIONTYPE param;

    /* Input port configuration. */

    self->out_port = g_omx_core_get_port (self->gomx, 0);
    g_omx_port_get_config (self->out_port, &param);
    g_omx_port_setup (self->out_port, &param);

    if (self->setup_ports)
    {
        self->setup_ports (self);
    }
}

static gboolean
start (GstBaseSrc *gst_base)
{
    GstOmxBaseSrc *self;

    self = GST_OMX_BASE_SRC (gst_base);

    GST_LOG_OBJECT (self, "begin");

    g_omx_core_init (self->gomx);
    if (self->gomx->omx_error)
        return GST_STATE_CHANGE_FAILURE;

    GST_LOG_OBJECT (self, "end");

    return TRUE;
}

static gboolean
stop (GstBaseSrc *gst_base)
{
    GstOmxBaseSrc *self;

    self = GST_OMX_BASE_SRC (gst_base);

    GST_LOG_OBJECT (self, "begin");

    g_omx_core_stop (self->gomx);
    g_omx_core_unload (self->gomx);
    g_omx_core_deinit (self->gomx);

    if (self->gomx->omx_error)
        return GST_STATE_CHANGE_FAILURE;

    GST_LOG_OBJECT (self, "end");

    return TRUE;
}

static void
finalize (GObject *obj)
{
    GstOmxBaseSrc *self;

    self = GST_OMX_BASE_SRC (obj);

    g_omx_core_free (self->gomx);

    g_free (self->omx_component);
    g_free (self->omx_library);

    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GstFlowReturn
create (GstBaseSrc *gst_base,
        guint64 offset,
        guint length,
        GstBuffer **ret_buf)
{
    GOmxCore *gomx;
    GOmxPort *out_port;
    GstOmxBaseSrc *self;
    GstFlowReturn ret = GST_FLOW_OK;

    self = GST_OMX_BASE_SRC (gst_base);

    gomx = self->gomx;

    GST_LOG_OBJECT (self, "begin");

    GST_LOG_OBJECT (self, "state: %d", gomx->omx_state);

    if (gomx->omx_state == OMX_StateLoaded)
    {
        GST_INFO_OBJECT (self, "omx: prepare");

        setup_ports (self);
        g_omx_core_prepare (self->gomx);
    }

    out_port = self->out_port;

    while (out_port->enabled)
    {
        switch (gomx->omx_state)
        {
            case OMX_StateIdle:
                {
                    GST_INFO_OBJECT (self, "omx: play");
                    g_omx_core_start (gomx);
                }
                break;
            default:
                break;
        }

        switch (gomx->omx_state)
        {
            case OMX_StateExecuting:
                /* OK */
                break;
            default:
                GST_ERROR_OBJECT (self, "Whoa! very wrong");
                break;
        }

        {
            OMX_BUFFERHEADERTYPE *omx_buffer;

            GST_LOG_OBJECT (self, "request_buffer");
            omx_buffer = g_omx_port_request_buffer (out_port);

            if (omx_buffer)
            {
                GST_DEBUG_OBJECT (self, "omx_buffer: size=%lu, len=%lu, offset=%lu",
                                  omx_buffer->nAllocLen, omx_buffer->nFilledLen, omx_buffer->nOffset);

                if (omx_buffer->nFlags & OMX_BUFFERFLAG_EOS)
                {
                    GST_INFO_OBJECT (self, "got eos");
                    g_omx_core_set_done (gomx);
                    break;
                }

                if (omx_buffer->nFilledLen > 0)
                {
                    GstBuffer *buf;

                    if (out_port->enabled)
                    {
                        GstCaps *caps = NULL;

                        caps = gst_pad_get_negotiated_caps (gst_base->srcpad);

                        if (!caps)
                        {
                            /** @todo We shouldn't be doing this. */
                            GST_WARNING_OBJECT (self, "somebody didn't do his work");
                            gomx->settings_changed_cb (gomx);
                        }
                        else
                        {
                            GST_LOG_OBJECT (self, "caps already fixed");
                            gst_caps_unref (caps);
                        }
                    }

                    buf = omx_buffer->pAppPrivate;

                    if (buf && !(omx_buffer->nFlags & OMX_BUFFERFLAG_EOS))
                    {
                        GST_BUFFER_SIZE (buf) = omx_buffer->nFilledLen;
#if 0
                        if (self->use_timestamps)
                        {
                            GST_BUFFER_TIMESTAMP (buf) = omx_buffer->nTimeStamp * (GST_SECOND / OMX_TICKS_PER_SECOND);
                        }
#endif

                        omx_buffer->pAppPrivate = NULL;
                        omx_buffer->pBuffer = NULL;
                        omx_buffer->nFilledLen = 0;

                        *ret_buf = buf;

                        gst_buffer_unref (buf);
                    }
                    else
                    {
                        /* This is only meant for the first OpenMAX buffers,
                         * which need to be pre-allocated. */
                        /* Also for the very last one. */
                        gst_pad_alloc_buffer_and_set_caps (gst_base->srcpad,
                                                           GST_BUFFER_OFFSET_NONE,
                                                           omx_buffer->nFilledLen,
                                                           GST_PAD_CAPS (gst_base->srcpad),
                                                           &buf);

                        if (buf)
                        {
                            GST_WARNING_OBJECT (self, "couldn't zero-copy");
                            memcpy (GST_BUFFER_DATA (buf), omx_buffer->pBuffer + omx_buffer->nOffset, omx_buffer->nFilledLen);
#if 0
                            if (self->use_timestamps)
                            {
                                GST_BUFFER_TIMESTAMP (buf) = omx_buffer->nTimeStamp * (GST_SECOND / OMX_TICKS_PER_SECOND);
                            }
#endif

                            omx_buffer->nFilledLen = 0;
                            g_free (omx_buffer->pBuffer);
                            omx_buffer->pBuffer = NULL;

                            *ret_buf = buf;
                        }
                        else
                        {
                            GST_ERROR_OBJECT (self, "whoa!");
                        }
                    }

                    if (!omx_buffer->pBuffer)
                    {
                        GstBuffer *new_buf;
                        GstFlowReturn result;

                        GST_LOG_OBJECT (self, "allocate buffer");
                        result = gst_pad_alloc_buffer_and_set_caps (gst_base->srcpad,
                                                                    GST_BUFFER_OFFSET_NONE,
                                                                    omx_buffer->nAllocLen,
                                                                    GST_PAD_CAPS (gst_base->srcpad),
                                                                    &new_buf);

                        if (result == GST_FLOW_OK)
                        {
                            gst_buffer_ref (new_buf);
                            omx_buffer->pAppPrivate = new_buf;

                            omx_buffer->pBuffer = GST_BUFFER_DATA (new_buf);
                            omx_buffer->nAllocLen = GST_BUFFER_SIZE (new_buf);
                        }
                        else
                        {
                            GST_WARNING_OBJECT (self, "could not allocate buffer");
                            omx_buffer->pBuffer = g_malloc (omx_buffer->nAllocLen);
                        }
                    }

                    GST_LOG_OBJECT (self, "release_buffer");
                    g_omx_port_release_buffer (out_port, omx_buffer);
                    break;
                }
                else
                {
                    GST_WARNING_OBJECT (self, "empty buffer");
                    GST_LOG_OBJECT (self, "release_buffer");
                    g_omx_port_release_buffer (out_port, omx_buffer);
                    continue;
                }
            }
            else
            {
                GST_WARNING_OBJECT (self, "null buffer");
                /* ret = GST_FLOW_ERROR; */
                break;
            }
        }
    }

    if (!out_port->enabled)
    {
        GST_WARNING_OBJECT (self, "done");
        ret = GST_FLOW_UNEXPECTED;
    }

    GST_LOG_OBJECT (self, "end");

    return ret;
}

static gboolean
handle_event (GstBaseSrc *gst_base,
              GstEvent *event)
{
    GstOmxBaseSrc *self;

    self = GST_OMX_BASE_SRC (gst_base);

    GST_LOG_OBJECT (self, "begin");

    GST_DEBUG_OBJECT (self, "event: %s", GST_EVENT_TYPE_NAME (event));

    switch (GST_EVENT_TYPE (event))
    {
        case GST_EVENT_EOS:
            /* Close the output port. */
            g_omx_core_set_done (self->gomx);
            break;

        case GST_EVENT_NEWSEGMENT:
            break;

        default:
            break;
    }

    GST_LOG_OBJECT (self, "end");

    return TRUE;
}

static void
set_property (GObject *obj,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    GstOmxBaseSrc *self;

    self = GST_OMX_BASE_SRC (obj);

    switch (prop_id)
    {
        case ARG_COMPONENT_NAME:
            if (self->omx_component)
            {
                g_free (self->omx_component);
            }
            self->omx_component = g_value_dup_string (value);
            break;
        case ARG_LIBRARY_NAME:
            if (self->omx_library)
            {
                g_free (self->omx_library);
            }
            self->omx_library = g_value_dup_string (value);
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
    GstOmxBaseSrc *self;

    self = GST_OMX_BASE_SRC (obj);

    switch (prop_id)
    {
        case ARG_COMPONENT_NAME:
            g_value_set_string (value, self->omx_component);
            break;
        case ARG_LIBRARY_NAME:
            g_value_set_string (value, self->omx_library);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
}

static void
type_base_init (gpointer g_class)
{
}

static void
type_class_init (gpointer g_class,
                 gpointer class_data)
{
    GObjectClass *gobject_class;
    GstBaseSrcClass *gst_base_src_class;

    gobject_class = G_OBJECT_CLASS (g_class);
    gst_base_src_class = GST_BASE_SRC_CLASS (g_class);

    gobject_class->finalize = finalize;

    gst_base_src_class->start = start;
    gst_base_src_class->stop = stop;
    gst_base_src_class->event = handle_event;
    gst_base_src_class->create = create;

    /* Properties stuff */
    {
        gobject_class->set_property = set_property;
        gobject_class->get_property = get_property;

        g_object_class_install_property (gobject_class, ARG_COMPONENT_NAME,
                                         g_param_spec_string ("component-name", "Component name",
                                                              "Name of the OpenMAX IL component to use",
                                                              NULL, G_PARAM_READWRITE));

        g_object_class_install_property (gobject_class, ARG_LIBRARY_NAME,
                                         g_param_spec_string ("library-name", "Library name",
                                                              "Name of the OpenMAX IL implementation library to use",
                                                              NULL, G_PARAM_READWRITE));
    }
}

static void
type_instance_init (GTypeInstance *instance,
                    gpointer g_class)
{
    GstOmxBaseSrc *self;

    self = GST_OMX_BASE_SRC (instance);

    GST_LOG_OBJECT (self, "begin");

    /* GOmx */
    self->gomx = g_omx_core_new (self);

    {
        const char *tmp;
        tmp = g_type_get_qdata (G_OBJECT_CLASS_TYPE (g_class),
                                g_quark_from_static_string ("library-name"));
        self->omx_library = g_strdup (tmp);
        tmp = g_type_get_qdata (G_OBJECT_CLASS_TYPE (g_class),
                                g_quark_from_static_string ("component-name"));
        self->omx_component = g_strdup (tmp);
    }


    GST_LOG_OBJECT (self, "end");
}
