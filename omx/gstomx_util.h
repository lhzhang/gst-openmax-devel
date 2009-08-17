/*
 * Copyright (C) 2006-2007 Texas Instruments, Incorporated
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

#ifndef GSTOMX_UTIL_H
#define GSTOMX_UTIL_H

#include <glib.h>
#include <OMX_Core.h>
#include <OMX_Component.h>

#include <async_queue.h>
#include <sem.h>

/* Typedefs. */

typedef struct GOmxCore GOmxCore;
typedef struct GOmxPort GOmxPort;
typedef struct GOmxImp GOmxImp;
typedef struct GOmxSymbolTable GOmxSymbolTable;
typedef enum GOmxPortType GOmxPortType;

typedef void (*GOmxCb) (GOmxCore *core);
typedef void (*GOmxPortCb) (GOmxPort *port);

/* Enums. */

enum GOmxPortType
{
    GOMX_PORT_INPUT,
    GOMX_PORT_OUTPUT
};

/* Structures. */

struct GOmxSymbolTable
{
    OMX_ERRORTYPE (*init) (void);
    OMX_ERRORTYPE (*deinit) (void);
    OMX_ERRORTYPE (*get_handle) (OMX_HANDLETYPE *handle,
                                 OMX_STRING name,
                                 OMX_PTR data,
                                 OMX_CALLBACKTYPE *callbacks);
    OMX_ERRORTYPE (*free_handle) (OMX_HANDLETYPE handle);
};

struct GOmxImp
{
    guint client_count;
    void *dl_handle;
    GOmxSymbolTable sym_table;
    GMutex *mutex;
};

struct GOmxCore
{
    gpointer object; /**< GStreamer element. */

    OMX_HANDLETYPE omx_handle;
    OMX_ERRORTYPE omx_error;

    OMX_STATETYPE omx_state;
    GCond *omx_state_condition;
    GMutex *omx_state_mutex;

    GPtrArray *ports;

    GSem *done_sem;
    GSem *flush_sem;
    GSem *port_sem;

    GOmxCb settings_changed_cb;
    GOmxImp *imp;

    gboolean done;
};

struct GOmxPort
{
    GOmxCore *core;
    GOmxPortType type;

    guint num_buffers;
    gulong buffer_size;
    guint port_index;
    OMX_BUFFERHEADERTYPE **buffers;

    GMutex *mutex;
    gboolean enabled;
    gboolean omx_allocate; /**< Setup with OMX_AllocateBuffer rather than OMX_UseBuffer */
    AsyncQueue *queue;
};

/* Functions. */

void g_omx_init (void);
void g_omx_deinit (void);

GOmxCore *g_omx_core_new (gpointer object);
void g_omx_core_free (GOmxCore *core);
void g_omx_core_init (GOmxCore *core);
void g_omx_core_deinit (GOmxCore *core);
void g_omx_core_prepare (GOmxCore *core);
void g_omx_core_start (GOmxCore *core);
void g_omx_core_pause (GOmxCore *core);
void g_omx_core_stop (GOmxCore *core);
void g_omx_core_unload (GOmxCore *core);
void g_omx_core_set_done (GOmxCore *core);
void g_omx_core_wait_for_done (GOmxCore *core);
void g_omx_core_flush_start (GOmxCore *core);
void g_omx_core_flush_stop (GOmxCore *core);
OMX_HANDLETYPE g_omx_core_get_handle (GOmxCore *core);
GOmxPort *g_omx_core_get_port (GOmxCore *core, guint index);


GOmxPort *g_omx_port_new (GOmxCore *core, guint index);
void g_omx_port_free (GOmxPort *port);

#define G_OMX_PORT_GET_PARAM(port, idx, param) G_STMT_START {  \
        memset ((param), 0, sizeof (*(param)));            \
        (param)->nSize = sizeof (*(param));                \
        (param)->nVersion.s.nVersionMajor = 1;             \
        (param)->nVersion.s.nVersionMinor = 1;             \
        (param)->nPortIndex = (port)->port_index;          \
        OMX_GetParameter (g_omx_core_get_handle ((port)->core), idx, (param)); \
    } G_STMT_END

#define G_OMX_PORT_SET_PARAM(port, idx, param) G_STMT_START {       \
        OMX_SetParameter (                                          \
            g_omx_core_get_handle ((port)->core), idx, (param));    \
    } G_STMT_END

/* I think we can remove these two:
 */
#define g_omx_port_get_config(port, param) \
        G_OMX_PORT_GET_PARAM (port, OMX_IndexParamPortDefinition, param)

#define g_omx_port_set_config(port, param) \
        G_OMX_PORT_SET_PARAM (port, OMX_IndexParamPortDefinition, param)



void g_omx_port_setup (GOmxPort *port, OMX_PARAM_PORTDEFINITIONTYPE *omx_port);
void g_omx_port_push_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer);
OMX_BUFFERHEADERTYPE *g_omx_port_request_buffer (GOmxPort *port);
void g_omx_port_release_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer);
void g_omx_port_resume (GOmxPort *port);
void g_omx_port_pause (GOmxPort *port);
void g_omx_port_flush (GOmxPort *port);
void g_omx_port_enable (GOmxPort *port);
void g_omx_port_disable (GOmxPort *port);
void g_omx_port_finish (GOmxPort *port);


OMX_COLOR_FORMATTYPE g_omx_fourcc_to_colorformat (guint32 fourcc);
guint32 g_omx_colorformat_to_fourcc (OMX_COLOR_FORMATTYPE eColorFormat);



/**
 * Basically like GST_BOILERPLATE / GST_BOILERPLATE_FULL, but follows the
 * init fxn naming conventions used by gst-openmax.  It expects the following
 * functions to be defined in the same src file following this macro
 * <ul>
 *   <li> type_base_init(gpointer g_class)
 *   <li> type_class_init(gpointer g_class, gpointer class_data)
 *   <li> type_instance_init(GTypeInstance *instance, gpointer g_class)
 * </ul>
 */
#define GSTOMX_BOILERPLATE_FULL(type, type_as_function, parent_type, parent_type_macro, additional_initializations) \
static void type_base_init (gpointer g_class);                                \
static void type_class_init (gpointer g_class, gpointer class_data);          \
static void type_instance_init (GTypeInstance *instance, gpointer g_class);   \
static parent_type ## Class *parent_class;                                    \
static void type_class_init_trampoline (gpointer g_class, gpointer class_data)\
{                                                                             \
    parent_class = g_type_class_ref (parent_type_macro);                      \
    type_class_init (g_class, class_data);                                    \
}                                                                             \
GType type_as_function ## _get_type (void)                                    \
{                                                                             \
    static GType _type = 0;                                                   \
    if (G_UNLIKELY (_type == 0))                                              \
    {                                                                         \
        GTypeInfo *type_info;                                                 \
        type_info = g_new0 (GTypeInfo, 1);                                    \
        type_info->class_size = sizeof (type ## Class);                       \
        type_info->base_init = type_base_init;                                \
        type_info->class_init = type_class_init_trampoline;                   \
        type_info->instance_size = sizeof (type);                             \
        type_info->instance_init = type_instance_init;                        \
        _type = g_type_register_static (parent_type_macro, #type, type_info, 0);\
        g_free (type_info);                                                   \
        additional_initializations (_type);                                   \
    }                                                                         \
    return _type;                                                             \
}
#define GSTOMX_BOILERPLATE(type,type_as_function,parent_type,parent_type_macro)    \
  GSTOMX_BOILERPLATE_FULL (type, type_as_function, parent_type, parent_type_macro, \
      __GST_DO_NOTHING)


/* Debug Macros:
 */

#define PRINT_BUFFER(obj, buffer)    G_STMT_START {             \
    if (buffer) {                                               \
      GST_DEBUG_OBJECT (obj, #buffer "=0x%08x (time=%"GST_TIME_FORMAT", duration=%"GST_TIME_FORMAT", flags=%08x)", (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), GST_TIME_ARGS (GST_BUFFER_DURATION(buffer)), GST_BUFFER_FLAGS (buffer)); \
    } else {                                                    \
      GST_DEBUG_OBJECT (obj, #buffer "=null");                  \
    }                                                           \
  } G_STMT_END


#endif /* GSTOMX_UTIL_H */
