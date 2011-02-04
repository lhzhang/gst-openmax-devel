/*
 * Copyright (C) 2010 Texas Instruments.
 *
 * Author: Leonardo Sandoval <lsandoval@ti.com>
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

#ifndef GSTOMX_VP7DEC_H
#define GSTOMX_VP7DEC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_OMX_VP7DEC(obj) (GstOmxVP7Dec *) (obj)
#define GST_OMX_VP7DEC_TYPE (gst_omx_vp7dec_get_type ())

typedef struct GstOmxVP7Dec GstOmxVP7Dec;
typedef struct GstOmxVP7DecClass GstOmxVP7DecClass;

#include "gstomx_base_videodec.h"

struct GstOmxVP7Dec
{
    GstOmxBaseVideoDec omx_base;
};

struct GstOmxVP7DecClass
{
    GstOmxBaseVideoDecClass parent_class;
};

GType gst_omx_vp7dec_get_type (void);

G_END_DECLS

#endif /* GSTOMX_VP7DEC_H */
