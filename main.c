// main.c
#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "nvds_meta.h"
#include "nvds_meta_schema.h"
#include "nvds_version.h"

// Estructura para la configuración de la app
typedef struct {
    gchar *input_uri;
    gdouble roi_left;
    gdouble roi_top;
    gdouble roi_width;
    gdouble roi_height;
    gdouble time_limit;      // segundos
    gchar *report_path;
} AppConfig;

// Estado por objeto (muy simple para v0)
typedef struct {
    gboolean inside;
    gdouble enter_time;   // tiempo de entrada actual al ROI
    gdouble total_time;   // tiempo acumulado dentro del ROI
    gboolean alerted;     // si ya superó el tiempo limite
} ObjState;

// Tabla global de objetos (id -> ObjState*)
static GHashTable *g_obj_table = NULL;

// Config global (para usarla en el pad probe)
static AppConfig g_cfg = {0};

// Función para calcular el tiempo en segundos a partir de buf_pts
static gdouble
get_frame_time_s(NvDsFrameMeta *frame_meta)
{
    // Usaremos buf_pts (nanosegundos) si viene seteado
    if (frame_meta->buf_pts != 0) {
        return (gdouble)frame_meta->buf_pts / 1e9;
    }
    // Fallback: usar número de frame / 30 fps (por ejemplo)
    return (gdouble)frame_meta->frame_num / 30.0;
}

// Devuelve TRUE si el centro del bounding box está dentro de la ROI
static gboolean
bbox_inside_roi(NvDsFrameMeta *frame_meta, NvDsObjectMeta *obj_meta)
{
    gdouble frame_w = frame_meta->source_frame_width;
    gdouble frame_h = frame_meta->source_frame_height;

    gdouble roi_x = g_cfg.roi_left * frame_w;
    gdouble roi_y = g_cfg.roi_top * frame_h;
    gdouble roi_w = g_cfg.roi_width * frame_w;
    gdouble roi_h = g_cfg.roi_height * frame_h;

    gdouble x = obj_meta->rect_params.left;
    gdouble y = obj_meta->rect_params.top;
    gdouble w = obj_meta->rect_params.width;
    gdouble h = obj_meta->rect_params.height;

    gdouble cx = x + w / 2.0;
    gdouble cy = y + h / 2.0;

    if (cx >= roi_x && cx <= roi_x + roi_w &&
        cy >= roi_y && cy <= roi_y + roi_h) {
        return TRUE;
    }
    return FALSE;
}

// Función auxiliar para obtener / crear estado de un objeto
static ObjState *
get_obj_state(guint64 obj_id)
{
    gpointer key = GUINT_TO_POINTER((guint)obj_id);
    ObjState *st = (ObjState *)g_hash_table_lookup(g_obj_table, key);
    if (!st) {
        st = g_new0(ObjState, 1);
        g_hash_table_insert(g_obj_table, key, st);
    }
    return st;
}

// Pad probe sobre nvdsosd (después de inferencia y tracker)
static GstPadProbeReturn
osd_sink_pad_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    GstBuffer *buf = (GstBuffer *)info->data;
    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
        return GST_PAD_PROBE_OK;

    for (NvDsMetaList *l_frame = batch_meta->frame_meta_list;
         l_frame != NULL; l_frame = l_frame->next) {

        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *)l_frame->data;
        gdouble t_now = get_frame_time_s(frame_meta);

        for (NvDsMetaList *l_obj = frame_meta->obj_meta_list;
             l_obj != NULL; l_obj = l_obj->next) {

            NvDsObjectMeta *obj_meta = (NvDsObjectMeta *)l_obj->data;
            guint64 obj_id = obj_meta->object_id;

            // DeepStream asigna object_id cuando hay tracker; si no, puede ser -1
            if (obj_id == (guint64)-1)
                continue;

            ObjState *st = get_obj_state(obj_id);
            gboolean inside = bbox_inside_roi(frame_meta, obj_meta);

            if (inside) {
                if (!st->inside) {
                    // Entró al ROI
                    st->inside = TRUE;
                    st->enter_time = t_now;
                } else {
                    // Ya estaba dentro, acumular tiempo
                    gdouble dt = t_now - st->enter_time;
                    st->total_time += dt;
                    st->enter_time = t_now; // reset para el siguiente frame

                    if (!st->alerted && st->total_time >= g_cfg.time_limit) {
                        st->alerted = TRUE;
                        g_print("ALERTA: objeto %lu excedió tiempo en ROI (%.2f s)\n",
                                (unsigned long)obj_id, st->total_time);
                        // Aquí más adelante podrías cambiar color ROI, etc.
                    }
                }
            } else {
                if (st->inside) {
                    // Salió del ROI, cerrar intervalo
                    gdouble dt = t_now - st->enter_time;
                    st->total_time += dt;
                    st->inside = FALSE;
                    st->enter_time = 0.0;
                }
            }
        }
    }

    return GST_PAD_PROBE_OK;
}

// Función para generar un reporte muy simple al final
static void
write_report(const gchar *path)
{
    if (!path) return;

    FILE *f = fopen(path, "w");
    if (!f) {
        g_printerr("No se pudo abrir archivo de reporte %s\n", path);
        return;
    }

    fprintf(f, "ROI (normalizada): left=%.3f top=%.3f width=%.3f height=%.3f\n",
            g_cfg.roi_left, g_cfg.roi_top, g_cfg.roi_width, g_cfg.roi_height);
    fprintf(f, "Tiempo limite: %.2f s\n\n", g_cfg.time_limit);
    fprintf(f, "Objeto\tTiempo_total_en_ROI(s)\tAlerta\n");

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, g_obj_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        guint64 obj_id = (guint64)GPOINTER_TO_UINT(key);
        ObjState *st = (ObjState *)value;
        fprintf(f, "%lu\t%.3f\t%s\n",
                (unsigned long)obj_id,
                st->total_time,
                st->alerted ? "SI" : "NO");
    }

    fclose(f);
    g_print("Reporte escrito en %s\n", path);
}

// Parseo muy simple de argumentos
static gboolean
parse_args(int argc, char *argv[], AppConfig *cfg)
{
    // Defaults
    cfg->roi_left = 0.3;
    cfg->roi_top = 0.3;
    cfg->roi_width = 0.4;
    cfg->roi_height = 0.4;
    cfg->time_limit = 5.0;
    cfg->report_path = g_strdup("report.txt");

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--input") && i + 1 < argc) {
            cfg->input_uri = g_strdup(argv[++i]);
        } else if (!strcmp(argv[i], "--roi-left") && i + 1 < argc) {
            cfg->roi_left = g_ascii_strtod(argv[++i], NULL);
        } else if (!strcmp(argv[i], "--roi-top") && i + 1 < argc) {
            cfg->roi_top = g_ascii_strtod(argv[++i], NULL);
        } else if (!strcmp(argv[i], "--roi-width") && i + 1 < argc) {
            cfg->roi_width = g_ascii_strtod(argv[++i], NULL);
        } else if (!strcmp(argv[i], "--roi-height") && i + 1 < argc) {
            cfg->roi_height = g_ascii_strtod(argv[++i], NULL);
        } else if (!strcmp(argv[i], "--time-limit") && i + 1 < argc) {
            cfg->time_limit = g_ascii_strtod(argv[++i], NULL);
        } else if (!strcmp(argv[i], "--report") && i + 1 < argc) {
            g_free(cfg->report_path);
            cfg->report_path = g_strdup(argv[++i]);
        } else {
            g_print("Argumento no reconocido: %s\n", argv[i]);
        }
    }

    if (!cfg->input_uri) {
        g_printerr("Debes especificar --input video\n");
        return FALSE;
    }

    return TRUE;
}

int
main(int argc, char *argv[])
{
    GstElement *pipeline, *osd;
    GstBus *bus;
    GstMessage *msg;
    GstPad *osd_sink_pad;
    GMainLoop *loop;

    gst_init(&argc, &argv);

    if (!parse_args(argc, argv, &g_cfg)) {
        return -1;
    }

    // Crear la tabla de objetos
    g_obj_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);

    // Ruta al archivo de configuración del modelo (ajusta esto)
    const gchar *pgie_config_file = "/opt/nvidia/deepstream/deepstream-6.3/samples/configs/deepstream-app/config_infer_primary.txt";

    // Pipeline básico para un solo video (batch-size=1)
    // Nota: aquí simplifico usando uridecodebin para que soporte varios formatos
    gchar *pipeline_desc = g_strdup_printf(
        "uridecodebin uri=file://%s name=srcbin ! "
        "nvvideoconvert ! "
        "video/x-raw(memory:NVMM),format=NV12 ! "
        "nvstreammux name=mux batch-size=1 width=1280 height=720 ! "
        "nvinfer config-file-path=%s ! "
        "nvtracker ll-lib-file=/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so ! "
        "nvdsosd name=osd ! "
        "nveglglessink sync=false",
        g_cfg.input_uri,
        pgie_config_file
    );

    g_print("Pipeline: %s\n", pipeline_desc);

    GError *error = NULL;
    pipeline = gst_parse_launch(pipeline_desc, &error);
    g_free(pipeline_desc);

    if (!pipeline) {
        g_printerr("No se pudo crear pipeline: %s\n", error ? error->message : "error desconocido");
        if (error) g_error_free(error);
        return -1;
    }

    // Obtener el elemento nvdsosd por nombre y poner pad probe en su sink
    osd = gst_bin_get_by_name(GST_BIN(pipeline), "osd");
    if (!osd) {
        g_printerr("No se encontró elemento 'osd' en el pipeline\n");
        gst_object_unref(pipeline);
        return -1;
    }

    osd_sink_pad = gst_element_get_static_pad(osd, "sink");
    if (!osd_sink_pad) {
        g_printerr("No se pudo obtener pad sink de osd\n");
        gst_object_unref(osd);
        gst_object_unref(pipeline);
        return -1;
    }

    gst_pad_add_probe(osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
                      osd_sink_pad_buffer_probe, NULL, NULL);
    gst_object_unref(osd_sink_pad);
    gst_object_unref(osd);

    // Ejecutar pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    bus = gst_element_get_bus(pipeline);

    // Bucle simple hasta EOS o ERROR
    gboolean terminate = FALSE;
    while (!terminate) {
        msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                         GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

        if (msg != NULL) {
            GError *err;
            gchar *dbg_info;

            switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &dbg_info);
                g_printerr("Error recibido de elemento %s: %s\n",
                           GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Información de depuración: %s\n",
                           dbg_info ? dbg_info : "none");
                g_clear_error(&err);
                g_free(dbg_info);
                terminate = TRUE;
                break;
            case GST_MESSAGE_EOS:
                g_print("Fin de flujo (EOS)\n");
                terminate = TRUE;
                break;
            default:
                break;
            }
            gst_message_unref(msg);
        }
    }

    // Detener pipeline
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);

    // Escribir reporte
    write_report(g_cfg.report_path);

    // Liberar memoria
    if (g_obj_table) {
        g_hash_table_destroy(g_obj_table);
    }
    g_free(g_cfg.input_uri);
    g_free(g_cfg.report_path);

    return 0;
}
