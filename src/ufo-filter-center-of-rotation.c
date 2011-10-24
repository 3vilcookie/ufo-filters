#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-center-of-rotation.h"

struct _UfoFilterCenterOfRotationPrivate {
    /* add your private data here */
    /* cl_kernel kernel; */
    gboolean use_sinograms; /**< FIXME: we should get this information from the buffer */
    float angle_step;
};

GType ufo_filter_center_of_rotation_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterCenterOfRotation, ufo_filter_center_of_rotation, UFO_TYPE_FILTER);

#define UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_CENTER_OF_ROTATION, UfoFilterCenterOfRotationPrivate))

enum {
    PROP_0,
    PROP_ANGLE_STEP, /* remove this or add more */
    PROP_USE_SINOGRAMS,
    N_PROPERTIES
};

static GParamSpec *center_of_rotation_properties[N_PROPERTIES] = { NULL, };

static void center_of_rotation_sinograms(UfoFilter *filter)
{
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    UfoBuffer *sinogram = ufo_channel_pop(input_channel);
    gint32 width, height;
    while (sinogram != NULL) {
        ufo_buffer_get_2d_dimensions(sinogram, &width, &height);

        float *proj_0 = ufo_buffer_get_cpu_data(sinogram, command_queue);
        float *proj_180 = proj_0 + (height-1) * width;

        const gint max_displacement = width / 2;
        const gsize N = max_displacement * 2 - 1;
        float *scores = g_malloc0(N * sizeof(float));

        for (int displacement = (-max_displacement + 1); displacement < 0; displacement++) {
            const int index = displacement + max_displacement - 1;
            for (int x = 0; x < width-ABS(displacement); x++) {
                float diff = proj_0[x] - proj_180[(width-ABS(displacement) - x + 1)];    
                scores[index] += diff * diff;
            }
        }

        for (int displacement = 0; displacement < max_displacement; displacement++) {
            const int index = displacement + max_displacement - 1; 
            for (int x = 0; x < width-displacement; x++) {
                float diff = proj_0[x+displacement] - proj_180[(width-x+1)];    
                scores[index] += diff * diff;
            }
        }
        
        int score_index = 0;
        float min_score = scores[0];
        for (int i = 1; i < N; i++) {
            if (scores[i] < min_score) {
                score_index = i;
                min_score = scores[i];
            } 
        }
        float center = (width + score_index - max_displacement + 1) / 2.0;
        g_message("Center of Rotation: %f", center);

        g_free(scores);

        ufo_channel_push(output_channel, sinogram);
        sinogram = ufo_channel_pop(input_channel);
    }
    ufo_channel_finish(output_channel);
}

static void center_of_rotation_projections(UfoFilter *filter)
{
    UfoFilterCenterOfRotationPrivate *priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(filter);
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    UfoBuffer *input = NULL;

    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);
    input = ufo_channel_pop(input_channel);
    float *proj_0 = ufo_buffer_get_cpu_data(input, command_queue);
    float *proj_180 = NULL;
    int counter = 0;

    /* Take all buffers until we got the opposite projection */
    input = ufo_channel_pop(input_channel);
    while (input != NULL) {
        if (ABS((counter++ * priv->angle_step) - 180.0f) < 0.001f) {
            proj_180 = ufo_buffer_get_cpu_data(input, command_queue); 
            break;
        }
        ufo_channel_push(output_channel, input);
        input = ufo_channel_pop(input_channel);
    }

    gint32 width, height;
    ufo_buffer_get_2d_dimensions(input, &width, &height);

    /* We have basically two parameters for tuning the performance: decreasing
     * max_displacement and not considering the whole images but just some of
     * the lines */
    const gint max_displacement = width / 2;
    const gsize N = max_displacement * 2 - 1;
    float *scores = g_malloc0(N * sizeof(float));
    float *grad = g_malloc0(N * sizeof(float));

    for (int displacement = (-max_displacement + 1); displacement < 0; displacement++) {
        const int index = displacement + max_displacement - 1;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width-ABS(displacement); x++) {
                float diff = proj_0[y*width+x] - proj_180[y*width + (width-ABS(displacement) - x + 1)];    
                scores[index] += diff * diff;
            }
        }
    }

    for (int displacement = 0; displacement < max_displacement; displacement++) {
        const int index = displacement + max_displacement - 1; 
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width-displacement; x++) {
                float diff = proj_0[y*width+x+displacement] - proj_180[y*width + (width-x+1)];    
                scores[index] += diff * diff;
            }
        }
    }
    grad[0] = 0.0;
    for (int i = 1; i < N; i++)
        grad[i] = scores[i] - scores[i-1];

    /* Find local minima. Actually, if max_displacement is not to large (like
     * width/2) the global maximum is always the correct maximum. */
    for (int i = 1; i < N; i++) 
        if (grad[i-1] < 0.0 && grad[i] > 0.0) 
            g_message("Local minimum at %f: %f", (width + i - max_displacement + 1) / 2.0, scores[i]);

    g_free(grad);
    g_free(scores);

    /* Push the 180° projection */
    ufo_channel_push(output_channel, input);

    /* Push any following projections */
    do {
        input = ufo_channel_pop(input_channel);
        ufo_channel_push(output_channel, input);
    }
    while (input != NULL);
}

static void activated(EthosPlugin *plugin)
{
}

static void deactivated(EthosPlugin *plugin)
{
}

/* 
 * virtual methods 
 */
static void ufo_filter_center_of_rotation_initialize(UfoFilter *filter)
{
    /* Here you can code, that is called for each newly instantiated filter */
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_center_of_rotation_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));

    /* Calculate the principial horizontal displacement according to "Image
     * processing pipeline for synchrotron-radiation-based tomographic
     * microscopy" by C. Hintermüller et al. (2010, International Union of
     * Crystallography, Singapore).
     *
     * In the case of projections, the whole projection at angle 0 and 180 are
     * used for determination of the center of rotation. When using sinograms,
     * we can use the first and last row of the sinogram to determine a center
     * of rotation, which will be most likely worse than those for projections.
     */
    if (UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(filter)->use_sinograms)
        center_of_rotation_sinograms(filter);
    else
        center_of_rotation_projections(filter);
}

static void ufo_filter_center_of_rotation_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterCenterOfRotation *self = UFO_FILTER_CENTER_OF_ROTATION(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_ANGLE_STEP:
            self->priv->angle_step = g_value_get_double(value);
            break;
        case PROP_USE_SINOGRAMS:
            self->priv->use_sinograms = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_center_of_rotation_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterCenterOfRotation *self = UFO_FILTER_CENTER_OF_ROTATION(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_ANGLE_STEP:
            g_value_set_double(value, self->priv->angle_step);
            break;
        case PROP_USE_SINOGRAMS:
            g_value_set_boolean(value, self->priv->use_sinograms);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_center_of_rotation_class_init(UfoFilterCenterOfRotationClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    EthosPluginClass *plugin_class = ETHOS_PLUGIN_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_center_of_rotation_set_property;
    gobject_class->get_property = ufo_filter_center_of_rotation_get_property;
    plugin_class->activated = activated;
    plugin_class->deactivated = deactivated;
    filter_class->initialize = ufo_filter_center_of_rotation_initialize;
    filter_class->process = ufo_filter_center_of_rotation_process;

    /* install properties */
    center_of_rotation_properties[PROP_ANGLE_STEP] = 
        g_param_spec_double("angle-step",
            "Step between two successive projections",
            "Step between two successive projections",
            0.00001,   /* minimum */
            180.0,   /* maximum */
            1.0,   /* default */
            G_PARAM_READWRITE);

    center_of_rotation_properties[PROP_USE_SINOGRAMS] = 
        g_param_spec_boolean("use-sinograms",
            "Use sinograms instead of projections",
            "Use sinograms instead of projections",
            FALSE,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_ANGLE_STEP, center_of_rotation_properties[PROP_ANGLE_STEP]);
    g_object_class_install_property(gobject_class, PROP_USE_SINOGRAMS, center_of_rotation_properties[PROP_USE_SINOGRAMS]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterCenterOfRotationPrivate));
}

static void ufo_filter_center_of_rotation_init(UfoFilterCenterOfRotation *self)
{
    UfoFilterCenterOfRotationPrivate *priv = self->priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(self);
    priv->angle_step = 1.0;
    priv->use_sinograms = FALSE;
}

G_MODULE_EXPORT EthosPlugin *ethos_plugin_register(void)
{
    return g_object_new(UFO_TYPE_FILTER_CENTER_OF_ROTATION, NULL);
}
