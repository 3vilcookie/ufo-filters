/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
 *
 * This file is part of Ufo.
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <stdio.h>
#include <math.h>
#include "ufo-zeropadding-task.h"

#define CL_CHECK_ERROR(FUNC) \
{ \
cl_int err = FUNC; \
if (err != CL_SUCCESS) { \
fprintf(stderr, "Error %d executing %s on %d!\n",\
err, __FILE__, __LINE__); \
abort(); \
}; \
}


/**
 * SECTION:ufo-zeropadding-task
 * @Short_description: Write TIFF files
 * @Title: zeropadding
 *
 */

struct _UfoZeropaddingTaskPrivate {
    UfoResources *resources;
    cl_kernel zeropadding_kernel;
    guint oversampling;
    gint center_rot;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoZeropaddingTask, ufo_zeropadding_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_ZEROPADDING_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ZEROPADDING_TASK, UfoZeropaddingTaskPrivate))

enum {
    PROP_0,
    PROP_OVERSAMPLING,
    PROP_CENTER_ROT,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_zeropadding_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ZEROPADDING_TASK, NULL));
}

static void
ufo_zeropadding_task_setup (UfoTask *task,
                            UfoResources *resources,
                            GError **error)
{
    UfoZeropaddingTaskPrivate *priv = UFO_ZEROPADDING_TASK_GET_PRIVATE (task);
    priv->resources = resources;
    priv->zeropadding_kernel = ufo_resources_get_kernel(resources, "zeropadding_kernel.cl", "zeropadding_kernel", error);
}

static void
ufo_zeropadding_task_get_requisition (UfoTask *task,
                                      UfoBuffer **inputs,
                                      UfoRequisition *requisition)
{
    UfoZeropaddingTaskPrivate *priv = UFO_ZEROPADDING_TASK_GET_PRIVATE (task);

    UfoRequisition input_requisition;
    ufo_buffer_get_requisition (inputs[0], &input_requisition);

    requisition->n_dims = input_requisition.n_dims;
    requisition->dims[0] = (gsize) (2 * pow(2, ceil(log2f((float) input_requisition.dims[0]))) * priv->oversampling);
    requisition->dims[1] = input_requisition.dims[1];
}

static void
ufo_zeropadding_task_get_structure (UfoTask *task,
                                    guint *n_inputs,
                                    UfoInputParam **in_params,
                                    UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_PROCESSOR;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = 2;
}

static gboolean
ufo_zeropadding_task_process (UfoGpuTask *task,
                              UfoBuffer **inputs,
                              UfoBuffer *output,
                              UfoRequisition *requisition)
{
    UfoZeropaddingTaskPrivate *priv;
    UfoGpuNode *node;
    cl_command_queue cmd_queue;
    cl_mem in_mem, out_mem;
    cl_int xdim, offset;

    priv = UFO_ZEROPADDING_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    UfoRequisition input_requisition;
    ufo_buffer_get_requisition (inputs[0], &input_requisition);

    /* args */
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    xdim = (cl_int) input_requisition.dims[0];
    int center_pos = priv->center_rot;
    offset = (center_pos != -1) ? xdim - (xdim - center_pos) * 2 : 0;
    
    CL_CHECK_ERROR (clSetKernelArg (priv->zeropadding_kernel, 0, sizeof (cl_mem), &in_mem));
    CL_CHECK_ERROR (clSetKernelArg (priv->zeropadding_kernel, 1, sizeof (cl_int), &xdim));
    CL_CHECK_ERROR (clSetKernelArg (priv->zeropadding_kernel, 2, sizeof (cl_mem), &out_mem));

    /* execution */
    size_t local_work_size[] = {16,16};
    size_t working_dims[] = {requisition->dims[0]/2, requisition->dims[1]};
    CL_CHECK_ERROR (clEnqueueNDRangeKernel (cmd_queue,
                                            priv->zeropadding_kernel,
                                            requisition->n_dims,
                                            NULL,
                                            working_dims,
                                            local_work_size,
                                            0, NULL, NULL));
    return TRUE;
}

static void
ufo_zeropadding_task_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
    UfoZeropaddingTaskPrivate *priv = UFO_ZEROPADDING_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_OVERSAMPLING:
            priv->oversampling = g_value_get_uint (value);
            break;
        case PROP_CENTER_ROT:
            priv->center_rot = g_value_get_int (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_zeropadding_task_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
    UfoZeropaddingTaskPrivate *priv = UFO_ZEROPADDING_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_OVERSAMPLING:
            g_value_set_uint (value, priv->oversampling);
            break;
        case PROP_CENTER_ROT:
            g_value_set_int (value, priv->center_rot);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_zeropadding_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_zeropadding_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_zeropadding_task_setup;
    iface->get_structure = ufo_zeropadding_task_get_structure;
    iface->get_requisition = ufo_zeropadding_task_get_requisition;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_zeropadding_task_process;
}

static void
ufo_zeropadding_task_class_init (UfoZeropaddingTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_zeropadding_task_set_property;
    gobject_class->get_property = ufo_zeropadding_task_get_property;
    gobject_class->finalize = ufo_zeropadding_task_finalize;

    properties[PROP_OVERSAMPLING] =
        g_param_spec_uint ("oversampling",
            "Oversamling",
            "Oversamling coefficient",
            1, G_MAXUINT, 1,
            G_PARAM_READWRITE);

    properties[PROP_CENTER_ROT] =
        g_param_spec_int ("center-of-rotation",
            "Center of rotation",
            "Center of rotation of specimen",
            -1, G_MAXINT, -1,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoZeropaddingTaskPrivate));
}

static void
ufo_zeropadding_task_init(UfoZeropaddingTask *self)
{
    self->priv = UFO_ZEROPADDING_TASK_GET_PRIVATE(self);
    self->priv->oversampling = 1;
    self->priv->center_rot = -1;
}