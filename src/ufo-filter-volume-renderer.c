#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <stdio.h>
#include <math.h>
#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-volume-renderer.h"

/**
 * SECTION:ufo-filter-volume-renderer
 * @Short_description: Volume rendering
 * @Title: volumerenderer
 *
 * Render volumes with ray-casting.
 */

struct _UfoFilterVolumeRendererPrivate {
    cl_kernel kernel;
    guint width;
    guint height;
};

G_DEFINE_TYPE(UfoFilterVolumeRenderer, ufo_filter_volume_renderer, UFO_TYPE_FILTER)

#define UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_VOLUME_RENDERER, UfoFilterVolumeRendererPrivate))

enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPERTIES
};

static GParamSpec *volume_renderer_properties[N_PROPERTIES] = { NULL, };


static void ufo_filter_volume_renderer_initialize(UfoFilter *filter)
{
    UfoFilterVolumeRenderer *self = UFO_FILTER_VOLUME_RENDERER(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    self->priv->kernel = ufo_resource_manager_get_kernel(manager, "volume.cl", "rayCastVolume", &error);

    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}

static void ufo_filter_volume_renderer_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterVolumeRendererPrivate *priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    UfoBuffer *output = NULL;

    UfoResourceManager *manager = ufo_resource_manager();
    cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    gsize width, height, slices;
    width = height = slices = 256;
    guint8 *input = g_malloc0(width * height * slices);

    FILE *fp = fopen("/home/matthias/data/amd-volume/aneurism.raw", "rb");
    gsize read = fread(input, 1, width * height * slices, fp);
    fclose(fp);
    g_assert(read == width * height * slices);

    cl_image_format volume_format = {
        .image_channel_order = CL_LUMINANCE,
        .image_channel_data_type = CL_UNORM_INT8
    };

    gfloat view_matrix[4][4] = { 
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 0.5, 0.0, 0.5, 1.0 }
    };

    cl_int error = CL_SUCCESS;
    cl_mem volume_mem = clCreateImage3D(context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            &volume_format,
            width, height, slices,
            0, 0, input, &error); 
    CHECK_OPENCL_ERROR(error);

    cl_mem view_mem = clCreateBuffer(context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
            4 * 4 * sizeof(float), view_matrix, &error);
    CHECK_OPENCL_ERROR(error);

    cl_kernel kernel = priv->kernel;
    gfloat step_size = 0.003f;
    gfloat displacement = -0.3f;
    gfloat linear_ramp_slope = 0.1f;
    gfloat linear_ramp_constant = 0.01f;
    gfloat threshold = 0.083f;
    cl_uint steps = (cl_uint) ((1.414f + fabs(displacement)) / step_size);

    error |= clSetKernelArg(kernel, 0, sizeof(cl_mem), &volume_mem);
    error |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &view_mem);
    error |= clSetKernelArg(kernel, 3, sizeof(cl_uint), &steps);
    error |= clSetKernelArg(kernel, 4, sizeof(gfloat), &step_size);
    error |= clSetKernelArg(kernel, 5, sizeof(gfloat), &displacement);
    error |= clSetKernelArg(kernel, 6, sizeof(gfloat), &linear_ramp_slope);
    error |= clSetKernelArg(kernel, 7, sizeof(gfloat), &linear_ramp_constant);
    error |= clSetKernelArg(kernel, 8, sizeof(gfloat), &threshold);
    CHECK_OPENCL_ERROR(error);

    const size_t global_work_size[] = { priv->width, priv->height };
    const guint dimensions[2] = { priv->width, priv->height };
    ufo_channel_allocate_output_buffers(output_channel, 2, dimensions);
    gfloat angle = 0.0f;

    for (int i = 0; i < 256; i++) {
        output = ufo_channel_get_output_buffer(output_channel);
        cl_mem output_mem = ufo_buffer_get_device_array(output, command_queue);

        error = clSetKernelArg(kernel, 1, sizeof(cl_mem), &output_mem);
        CHECK_OPENCL_ERROR(error);

        /* TODO: manage copy event so that we don't have to block here */
        CHECK_OPENCL_ERROR(clEnqueueWriteBuffer(command_queue,
                    view_mem, CL_TRUE,
                    0, 4 * 4 * sizeof(float), view_matrix,
                    0, NULL, NULL));

        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue, kernel,
                    2, NULL, global_work_size, NULL,
                    0, NULL, NULL));

        ufo_channel_finalize_output_buffer(output_channel, output);

        /* rotate around the x-axis for now */
        const gfloat cos_angle = (gfloat) cos(angle);
        const gfloat sin_angle = (gfloat) sin(angle);
        view_matrix[0][0] = cos_angle;
        view_matrix[0][2] = sin_angle;
        view_matrix[2][0] = -sin_angle;
        view_matrix[2][2] = cos_angle;

        angle += 0.05f;
    }

    CHECK_OPENCL_ERROR(clReleaseMemObject(volume_mem)); 
    CHECK_OPENCL_ERROR(clReleaseMemObject(view_mem));
    ufo_channel_finish(output_channel);
    g_free(input);
}

static void ufo_filter_volume_renderer_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterVolumeRendererPrivate *priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_WIDTH:
            priv->width = g_value_get_uint(value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_volume_renderer_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterVolumeRendererPrivate *priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_WIDTH:
            g_value_set_uint(value, priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint(value, priv->height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_volume_renderer_class_init(UfoFilterVolumeRendererClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_volume_renderer_set_property;
    gobject_class->get_property = ufo_filter_volume_renderer_get_property;
    filter_class->initialize = ufo_filter_volume_renderer_initialize;
    filter_class->process = ufo_filter_volume_renderer_process;

    volume_renderer_properties[PROP_WIDTH] = 
        g_param_spec_uint("width",
            "Width",
            "Width of the output image",
            1, 8192, 512,
            G_PARAM_READWRITE);

    volume_renderer_properties[PROP_HEIGHT] = 
        g_param_spec_uint("height",
            "Height",
            "Height of the output image",
            1, 8192, 512,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_WIDTH, volume_renderer_properties[PROP_WIDTH]);
    g_object_class_install_property(gobject_class, PROP_HEIGHT, volume_renderer_properties[PROP_HEIGHT]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterVolumeRendererPrivate));
}

static void ufo_filter_volume_renderer_init(UfoFilterVolumeRenderer *self)
{
    self->priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(self);
    self->priv->width = 512;
    self->priv->height = 512;

    ufo_filter_register_output(UFO_FILTER(self), "output0", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_VOLUME_RENDERER, NULL);
}
