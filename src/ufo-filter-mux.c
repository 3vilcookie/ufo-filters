#include <gmodule.h>
#include <CL/cl.h>

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-mux.h"

struct _UfoFilterMuxPrivate {
};

GType ufo_filter_mux_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterMux, ufo_filter_mux, UFO_TYPE_FILTER);

#define UFO_FILTER_MUX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_MUX, UfoFilterMuxPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

/* static GParamSpec *mux_properties[N_PROPERTIES] = { NULL, }; */

static void activated(EthosPlugin *plugin)
{
}

static void deactivated(EthosPlugin *plugin)
{
}

/* 
 * virtual methods 
 */
static void ufo_filter_mux_initialize(UfoFilter *filter)
{
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_mux_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoChannel *input_channels[2] = { NULL, NULL };
    input_channels[0] = ufo_filter_get_input_channel_by_name(filter, "input1");
    input_channels[1] = ufo_filter_get_input_channel_by_name(filter, "input2");
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);

    UfoBuffer *input1 = ufo_channel_pop(input_channels[0]);
    UfoBuffer *input2 = ufo_channel_pop(input_channels[1]);
    gint id1 = ufo_buffer_get_id(input1);
    gint id2 = ufo_buffer_get_id(input2);
    
    while ((input1 != NULL) || (input2 != NULL)) {
        while ((id1 < id2) && (input1 != NULL)) {
            ufo_channel_push(output_channel, input1);
            input1 = ufo_channel_pop(input_channels[0]);
            id1 = ufo_buffer_get_id(input1);
        }
        
        while ((id2 < id1) && (input2 != NULL)) {
            ufo_channel_push(output_channel, input2);
            input2 = ufo_channel_pop(input_channels[1]);
            id2 = ufo_buffer_get_id(input2);
        }

        if (input1 != NULL) {
            ufo_channel_push(output_channel, input1);
            input1 = ufo_channel_pop(input_channels[0]);
            id1 = input1 == NULL ? -1 : ufo_buffer_get_id(input1);
        }
        
        if (input2 != NULL) {
            ufo_channel_push(output_channel, input2);
            input2 = ufo_channel_pop(input_channels[1]);
            id2 = input2 == NULL ? -1 : ufo_buffer_get_id(input2);
        }
    }
    
    /* Discard one of the finishing buffers and push the other */
    ufo_channel_finish(output_channel);
}

static void ufo_filter_mux_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    /* Handle all properties accordingly */
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_mux_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    /* Handle all properties accordingly */
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_mux_class_init(UfoFilterMuxClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    EthosPluginClass *plugin_class = ETHOS_PLUGIN_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_mux_set_property;
    gobject_class->get_property = ufo_filter_mux_get_property;
    plugin_class->activated = activated;
    plugin_class->deactivated = deactivated;
    filter_class->initialize = ufo_filter_mux_initialize;
    filter_class->process = ufo_filter_mux_process;
}

static void ufo_filter_mux_init(UfoFilterMux *self)
{
}

G_MODULE_EXPORT EthosPlugin *ethos_plugin_register(void)
{
    return g_object_new(UFO_TYPE_FILTER_MUX, NULL);
}
