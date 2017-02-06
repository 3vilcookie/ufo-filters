/*
 * Copyright (C) 2011-2015 Karlsruhe Institute of Technology
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

#include "ufo-rofex-average-dark-task.h"


struct _UfoRofexAverageDarkTaskPrivate {
    guint n_planes;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexAverageDarkTask, ufo_rofex_average_dark_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_AVERAGE_DARK_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_AVERAGE_DARK_TASK, UfoRofexAverageDarkTaskPrivate))

enum {
    PROP_0,
    PROP_N_PLANES,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_average_dark_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_AVERAGE_DARK_TASK, NULL));
}

static void
ufo_rofex_average_dark_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
}

static void
ufo_rofex_average_dark_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRequisition in_req;
    ufo_buffer_get_requisition(inputs[0], &in_req);

    requisition->n_dims = 2;
    requisition->dims[0] = in_req.dims[0];
    requisition->dims[1] = in_req.dims[1];
}

static guint
ufo_rofex_average_dark_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_average_dark_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_rofex_average_dark_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_rofex_average_dark_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexAverageDarkTaskPrivate *priv = UFO_ROFEX_AVERAGE_DARK_TASK_GET_PRIVATE (task);
    guint n_dets = requisition->dims[0];
    guint n_proj = requisition->dims[1];
    guint n_planes = priv->n_planes;
    guint n_slices = requisition->dims[2] / n_planes;

    gfloat *h_sino = ufo_buffer_get_host_array(inputs[0], NULL);
    gfloat *h_average = ufo_buffer_get_host_array(output, NULL);

    gfloat factor = 1.0 / (gfloat)(n_slices * n_proj);
    // factor = 0.0
    for (guint sliceInd = 0; sliceInd < n_slices; sliceInd++) {
      for (guint planeInd = 0; planeInd < n_dets; planeInd++) {
        for (guint detInd = 0; detInd < n_dets; detInd++) {
          for (guint projInd = 0; projInd < n_proj; projInd++)
          {
            guint valInd = detInd + n_dets * projInd +
                          (sliceInd * n_planes + planeInd) * n_dets * n_proj;

            gfloat val = (gfloat)h_sino[valInd];
            h_average[detInd + planeInd * n_dets] += val * factor;
          }
        }
      }
    }

    // Interpolate dark average
    gfloat valInterp = 0.0;
    for (guint planeInd = 0; planeInd < n_planes; planeInd++) {
        for (guint detInd = 0; detInd < n_dets; detInd++) {
            if (h_average[detInd + planeInd * n_dets] > 300) {
              valInterp = h_average[n_dets * planeInd + (detInd + 1)%n_dets] +
                          h_average[n_dets * planeInd + (detInd - 1)%n_dets];

              h_average[detInd + planeInd * n_dets] = 0.5 * valInterp;
            }
        }
    }

    return TRUE;
}


static void
ufo_rofex_average_dark_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexAverageDarkTaskPrivate *priv = UFO_ROFEX_AVERAGE_DARK_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_average_dark_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexAverageDarkTaskPrivate *priv = UFO_ROFEX_AVERAGE_DARK_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_average_dark_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_average_dark_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_average_dark_task_setup;
    iface->get_num_inputs = ufo_rofex_average_dark_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_average_dark_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_average_dark_task_get_mode;
    iface->get_requisition = ufo_rofex_average_dark_task_get_requisition;
    iface->process = ufo_rofex_average_dark_task_process;
}

static void
ufo_rofex_average_dark_task_class_init (UfoRofexAverageDarkTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_average_dark_task_set_property;
    oclass->get_property = ufo_rofex_average_dark_task_get_property;
    oclass->finalize = ufo_rofex_average_dark_task_finalize;

    properties[PROP_N_PLANES] =
              g_param_spec_uint ("number-of-planes",
                                 "The number of planes",
                                 "The number of planes",
                                 1, G_MAXUINT, 1,
                                 G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexAverageDarkTaskPrivate));
}

static void
ufo_rofex_average_dark_task_init(UfoRofexAverageDarkTask *self)
{
    self->priv = UFO_ROFEX_AVERAGE_DARK_TASK_GET_PRIVATE(self);
    self->priv->n_planes = 1;
}
