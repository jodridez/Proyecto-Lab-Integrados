#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gstnvdsmeta.h"
#include "nvds_analytics_meta.h"

/* Estructura para rastrear personas */
typedef struct {
    guint64 person_id;
    GstClockTime entry_time;
    GstClockTime last_seen;
    gboolean time_exceeded;
    gboolean reported;
    gfloat avg_confidence;
    guint frame_count;
} TrackedPerson;

/* Estructura de datos del ROI */
typedef struct {
    gint left;
    gint top;
    gint width;
    gint height;
    GHashTable *tracked_persons;
    guint max_time_seconds;
    guint total_persons_detected;
    guint persons_exceeded_time;
    FILE *report_file;
    gchar *report_filename;
    GstClockTime pipeline_start_time;
} ROIData;

/* Estructura de configuración de la aplicación */
typedef struct {
    gchar *input_file;
    gchar *output_file;
    gchar *report_file;
    gfloat roi_left;
    gfloat roi_top;
    gfloat roi_width;
    gfloat roi_height;
    guint max_time;
    gchar *mode; // "video", "udp", "udp_video"
    gchar *udp_host;
    guint udp_port;
} AppConfig;

/* Variables globales */
static GMainLoop *loop = NULL;
static ROIData roi_data = {0};
static guint frame_width = 1920;
static guint frame_height = 1080;

/* ============================================================================
 * FUNCIONES DE UTILIDAD
 * ============================================================================ */

/* Verifica si un bounding box está dentro del ROI */
static gboolean is_bbox_in_roi(float left, float top, float width, float height) {
    float cx = left + width / 2.0f;
    float cy = top + height / 2.0f;
    
    return (cx >= roi_data.left && cx <= roi_data.left + roi_data.width &&
            cy >= roi_data.top && cy <= roi_data.top + roi_data.height);
}

/* Libera memoria de persona rastreada */
static void free_tracked_person(gpointer data) {
    g_free(data);
}

/* Inicializa datos del ROI */
static void init_roi_data(AppConfig *config) {
    roi_data.left = (gint)(config->roi_left * frame_width);
    roi_data.top = (gint)(config->roi_top * frame_height);
    roi_data.width = (gint)(config->roi_width * frame_width);
    roi_data.height = (gint)(config->roi_height * frame_height);
    roi_data.max_time_seconds = config->max_time;
    roi_data.total_persons_detected = 0;
    roi_data.persons_exceeded_time = 0;
    roi_data.report_filename = g_strdup(config->report_file);
    roi_data.tracked_persons = g_hash_table_new_full(g_int64_hash, g_int64_equal, 
                                                      NULL, free_tracked_person);
    roi_data.pipeline_start_time = 0;
}

/* ============================================================================
 * PROCESAMIENTO DE METADATOS
 * ============================================================================ */

/* Dibuja el ROI en el frame */
static void draw_roi_overlay(NvDsFrameMeta *frame_meta, gboolean person_in_roi, 
                            gboolean time_exceeded) {
    NvDsBatchMeta *batch_meta = frame_meta->base_meta.batch_meta;
    NvDsDisplayMeta *display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
    
    NvOSD_RectParams *rect_params = &display_meta->rect_params[0];
    
    rect_params->left = roi_data.left;
    rect_params->top = roi_data.top;
    rect_params->width = roi_data.width;
    rect_params->height = roi_data.height;
    rect_params->border_width = 6;
    rect_params->has_bg_color = 1;
    rect_params->bg_color = (NvOSD_ColorParams){0.0, 0.0, 0.0, 0.3};
    
    /* Colores según estado:
     * Verde: ROI vacío
     * Amarillo: Persona dentro del tiempo permitido
     * Rojo: Persona excede el tiempo máximo
     */
    if (!person_in_roi) {
        // Verde
        rect_params->border_color = (NvOSD_ColorParams){0.0, 1.0, 0.0, 1.0};
    } else if (time_exceeded) {
        // Rojo
        rect_params->border_color = (NvOSD_ColorParams){1.0, 0.0, 0.0, 1.0};
    } else {
        // Amarillo
        rect_params->border_color = (NvOSD_ColorParams){1.0, 1.0, 0.0, 1.0};
    }
    
    display_meta->num_rects = 1;
    nvds_add_display_meta_to_frame(frame_meta, display_meta);
}

/* Procesa objetos detectados en el frame */
static GstPadProbeReturn osd_sink_pad_buffer_probe(GstPad *pad, GstPadProbeInfo *info,
                                                   gpointer u_data) {
    GstBuffer *buf = (GstBuffer *)info->data;
    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    
    if (!batch_meta) {
        return GST_PAD_PROBE_OK;
    }
    
    GstClockTime current_time = gst_util_get_timestamp();
    if (roi_data.pipeline_start_time == 0) {
        roi_data.pipeline_start_time = current_time;
    }
    
    gboolean person_in_roi = FALSE;
    gboolean time_exceeded = FALSE;
    
    for (NvDsMetaList *l_frame = batch_meta->frame_meta_list; l_frame != NULL; 
         l_frame = l_frame->next) {
        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *)(l_frame->data);
        
        // Procesar objetos detectados
        for (NvDsMetaList *l_obj = frame_meta->obj_meta_list; l_obj != NULL; 
             l_obj = l_obj->next) {
            NvDsObjectMeta *obj_meta = (NvDsObjectMeta *)(l_obj->data);
            
            // Solo procesar personas (class_id 0 en ResNet10 y PeopleNet)
            if (obj_meta->class_id != 0) {
                continue;
            }
            
            // Verificar si está en el ROI
            if (is_bbox_in_roi(obj_meta->rect_params.left, 
                              obj_meta->rect_params.top,
                              obj_meta->rect_params.width, 
                              obj_meta->rect_params.height)) {
                
                person_in_roi = TRUE;
                guint64 person_id = obj_meta->object_id;
                
                TrackedPerson *person = g_hash_table_lookup(roi_data.tracked_persons, 
                                                           &person_id);
                
                if (!person) {
                    // Nueva persona detectada en ROI
                    person = g_new0(TrackedPerson, 1);
                    person->person_id = person_id;
                    person->entry_time = current_time;
                    person->last_seen = current_time;
                    person->time_exceeded = FALSE;
                    person->reported = FALSE;
                    person->avg_confidence = obj_meta->confidence;
                    person->frame_count = 1;
                    
                    guint64 *key = g_new(guint64, 1);
                    *key = person_id;
                    g_hash_table_insert(roi_data.tracked_persons, key, person);
                    
                    roi_data.total_persons_detected++;
                    
                    g_print("Nueva persona detectada en ROI: ID=%lu\n", person_id);
                } else {
                    // Actualizar persona existente
                    person->last_seen = current_time;
                    person->frame_count++;
                    person->avg_confidence = (person->avg_confidence * (person->frame_count - 1) + 
                                            obj_meta->confidence) / person->frame_count;
                    
                    // Calcular tiempo en ROI
                    gdouble seconds_in_roi = (gdouble)(current_time - person->entry_time) / 
                                            GST_SECOND;
                    
                    // Verificar si excede el tiempo máximo
                    if (seconds_in_roi > roi_data.max_time_seconds && !person->time_exceeded) {
                        person->time_exceeded = TRUE;
                        roi_data.persons_exceeded_time++;
                        time_exceeded = TRUE;
                        g_print("ALERTA: Persona ID=%lu excedió el tiempo máximo (%.1fs)\n", 
                               person_id, seconds_in_roi);
                    }
                    
                    if (person->time_exceeded) {
                        time_exceeded = TRUE;
                    }
                }
            }
        }
        
        // Dibujar ROI con color según estado
        draw_roi_overlay(frame_meta, person_in_roi, time_exceeded);
    }
    
    // Limpiar personas que no han sido vistas recientemente
    GHashTableIter iter;
    gpointer key, value;
    GList *to_remove = NULL;
    
    g_hash_table_iter_init(&iter, roi_data.tracked_persons);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        TrackedPerson *person = (TrackedPerson *)value;
        gdouble time_since_seen = (gdouble)(current_time - person->last_seen) / GST_SECOND;
        
        // Remover si no se ha visto en 2 segundos
        if (time_since_seen > 2.0) {
            to_remove = g_list_prepend(to_remove, key);
        }
    }
    
    for (GList *l = to_remove; l != NULL; l = l->next) {
        g_hash_table_remove(roi_data.tracked_persons, l->data);
    }
    g_list_free(to_remove);
    
    return GST_PAD_PROBE_OK;
}

/* ============================================================================
 * GENERACIÓN DE REPORTE
 * ============================================================================ */

static void generate_report() {
    FILE *fp = fopen(roi_data.report_filename, "w");
    if (!fp) {
        g_printerr("Error al crear archivo de reporte: %s\n", roi_data.report_filename);
        return;
    }
    
    fprintf(fp, "ROI: left: %d top: %d width: %d height: %d\n",
            roi_data.left, roi_data.top, roi_data.width, roi_data.height);
    fprintf(fp, "Max time: %us\n", roi_data.max_time_seconds);
    fprintf(fp, "Detected: %u (%u)\n", 
            roi_data.total_persons_detected, roi_data.persons_exceeded_time);
    
    // Iterar sobre personas rastreadas y escribir información
    GHashTableIter iter;
    gpointer key, value;
    
    g_hash_table_iter_init(&iter, roi_data.tracked_persons);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        TrackedPerson *person = (TrackedPerson *)value;
        
        gdouble entry_time_sec = (gdouble)(person->entry_time - roi_data.pipeline_start_time) / 
                                GST_SECOND;
        gdouble duration = (gdouble)(person->last_seen - person->entry_time) / GST_SECOND;
        
        gint minutes = (gint)(entry_time_sec / 60);
        gint seconds = (gint)entry_time_sec % 60;
        
        fprintf(fp, "%d:%02d person time %.0fs", minutes, seconds, duration);
        
        if (person->time_exceeded) {
            fprintf(fp, " alert");
        }
        
        fprintf(fp, "\n");
    }
    
    fclose(fp);
    g_print("Reporte generado: %s\n", roi_data.report_filename);
}

/* ============================================================================
 * MANEJO DE SEÑALES Y BUS
 * ============================================================================ */

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *)data;
    
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("Fin del stream\n");
            g_main_loop_quit(loop);
            break;
        
        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;
            gst_message_parse_error(msg, &error, &debug);
            g_printerr("Error: %s\n", error->message);
            g_error_free(error);
            g_free(debug);
            g_main_loop_quit(loop);
            break;
        }
        
        default:
            break;
    }
    
    return TRUE;
}

static void sigint_handler(int sig) {
    g_print("\nInterrupción recibida, cerrando aplicación...\n");
    if (loop) {
        g_main_loop_quit(loop);
    }
}

/* ============================================================================
 * CONSTRUCCIÓN DEL PIPELINE
 * ============================================================================ */

static GstElement* create_pipeline(AppConfig *config) {
    GstElement *pipeline, *source, *h264parser, *decoder, *streammux, *pgie, *tracker;
    GstElement *nvvidconv, *nvosd, *tee, *queue1, *queue2;
    GstElement *nvvidconv_out, *encoder, *h264parser_out, *rtppay, *sink_udp;
    GstElement *nvvidconv_file, *encoder_file, *h264parser_file, *muxer, *sink_file;
    
    pipeline = gst_pipeline_new("roi-pipeline");
    
    // Elementos de entrada y decodificación
    source = gst_element_factory_make("filesrc", "file-source");
    h264parser = gst_element_factory_make("h264parse", "h264-parser");
    decoder = gst_element_factory_make("nvv4l2decoder", "nvv4l2-decoder");
    
    // Multiplexor
    streammux = gst_element_factory_make("nvstreammux", "stream-muxer");
    
    // Inferencia y tracking
    pgie = gst_element_factory_make("nvinfer", "primary-nvinference-engine");
    tracker = gst_element_factory_make("nvtracker", "tracker");
    
    // Conversión y OSD
    nvvidconv = gst_element_factory_make("nvvideoconvert", "nvvideo-converter");
    nvosd = gst_element_factory_make("nvdsosd", "nv-onscreendisplay");
    
    // Tee para dividir salidas
    tee = gst_element_factory_make("tee", "tee");
    
    if (!pipeline || !source || !h264parser || !decoder || !streammux || 
        !pgie || !tracker || !nvvidconv || !nvosd || !tee) {
        g_printerr("Error al crear elementos del pipeline\n");
        return NULL;
    }
    
    // Configurar elementos
    g_object_set(G_OBJECT(source), "location", config->input_file, NULL);
    
    g_object_set(G_OBJECT(streammux), 
                 "width", frame_width, 
                 "height", frame_height,
                 "batch-size", 1,
                 "batched-push-timeout", 4000000, 
                 NULL);
    
    g_object_set(G_OBJECT(pgie),
                 "config-file-path", 
                 "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt",
                 NULL);
    
    g_object_set(G_OBJECT(tracker),
                 "ll-lib-file", "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so",
                 "ll-config-file", "/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml",
                 "tracker-width", 640,
                 "tracker-height", 384,
                 NULL);
    
    g_object_set(G_OBJECT(nvosd), "process-mode", 1, NULL); // HW mode
    
    // Agregar elementos al pipeline
    gst_bin_add_many(GST_BIN(pipeline), source, h264parser, decoder, streammux, 
                     pgie, tracker, nvvidconv, nvosd, tee, NULL);
    
    // Vincular elementos
    if (!gst_element_link_many(source, h264parser, decoder, NULL)) {
        g_printerr("Error vinculando elementos de entrada\n");
        return NULL;
    }
    
    // Vincular decoder con streammux
    GstPad *sinkpad, *srcpad;
    sinkpad = gst_element_get_request_pad(streammux, "sink_0");
    srcpad = gst_element_get_static_pad(decoder, "src");
    if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
        g_printerr("Error vinculando decoder con streammux\n");
        return NULL;
    }
    gst_object_unref(sinkpad);
    gst_object_unref(srcpad);
    
    // Vincular resto del pipeline
    if (!gst_element_link_many(streammux, pgie, tracker, nvvidconv, nvosd, tee, NULL)) {
        g_printerr("Error vinculando elementos principales\n");
        return NULL;
    }
    
    // Configurar salidas según modo
    if (g_strcmp0(config->mode, "udp") == 0 || g_strcmp0(config->mode, "udp_video") == 0) {
        // Rama UDP
        queue1 = gst_element_factory_make("queue", "queue-udp");
        nvvidconv_out = gst_element_factory_make("nvvideoconvert", "nvvidconv-udp");
        encoder = gst_element_factory_make("nvv4l2h264enc", "encoder-udp");
        h264parser_out = gst_element_factory_make("h264parse", "h264parser-udp");
        rtppay = gst_element_factory_make("rtph264pay", "rtppay");
        sink_udp = gst_element_factory_make("udpsink", "udpsink");
        
        g_object_set(G_OBJECT(encoder), "bitrate", 4000000, NULL);
        g_object_set(G_OBJECT(rtppay), "config-interval", 1, "pt", 96, NULL);
        g_object_set(G_OBJECT(sink_udp), 
                     "host", config->udp_host,
                     "port", config->udp_port,
                     "sync", FALSE,
                     NULL);
        
        gst_bin_add_many(GST_BIN(pipeline), queue1, nvvidconv_out, encoder, 
                        h264parser_out, rtppay, sink_udp, NULL);
        
        if (!gst_element_link_many(queue1, nvvidconv_out, encoder, h264parser_out, 
                                   rtppay, sink_udp, NULL)) {
            g_printerr("Error vinculando rama UDP\n");
            return NULL;
        }
        
        // Vincular tee con rama UDP
        GstPad *tee_udp_pad = gst_element_get_request_pad(tee, "src_%u");
        GstPad *queue_udp_pad = gst_element_get_static_pad(queue1, "sink");
        if (gst_pad_link(tee_udp_pad, queue_udp_pad) != GST_PAD_LINK_OK) {
            g_printerr("Error vinculando tee con rama UDP\n");
            return NULL;
        }
        gst_object_unref(tee_udp_pad);
        gst_object_unref(queue_udp_pad);
    }
    
    if (g_strcmp0(config->mode, "video") == 0 || g_strcmp0(config->mode, "udp_video") == 0) {
        // Rama archivo
        queue2 = gst_element_factory_make("queue", "queue-file");
        nvvidconv_file = gst_element_factory_make("nvvideoconvert", "nvvidconv-file");
        encoder_file = gst_element_factory_make("nvv4l2h264enc", "encoder-file");
        h264parser_file = gst_element_factory_make("h264parse", "h264parser-file");
        muxer = gst_element_factory_make("qtmux", "muxer");
        sink_file = gst_element_factory_make("filesink", "filesink");
        
        g_object_set(G_OBJECT(encoder_file), "bitrate", 8000000, NULL);
        g_object_set(G_OBJECT(sink_file), "location", config->output_file, "sync", FALSE, NULL);
        
        gst_bin_add_many(GST_BIN(pipeline), queue2, nvvidconv_file, encoder_file,
                        h264parser_file, muxer, sink_file, NULL);
        
        if (!gst_element_link_many(queue2, nvvidconv_file, encoder_file, h264parser_file,
                                   muxer, sink_file, NULL)) {
            g_printerr("Error vinculando rama de archivo\n");
            return NULL;
        }
        
        // Vincular tee con rama archivo
        GstPad *tee_file_pad = gst_element_get_request_pad(tee, "src_%u");
        GstPad *queue_file_pad = gst_element_get_static_pad(queue2, "sink");
        if (gst_pad_link(tee_file_pad, queue_file_pad) != GST_PAD_LINK_OK) {
            g_printerr("Error vinculando tee con rama de archivo\n");
            return NULL;
        }
        gst_object_unref(tee_file_pad);
        gst_object_unref(queue_file_pad);
    }
    
    // Agregar probe al pad del OSD
    GstPad *osd_sink_pad = gst_element_get_static_pad(nvosd, "sink");
    if (!osd_sink_pad) {
        g_printerr("Error obteniendo pad del OSD\n");
        return NULL;
    }
    gst_pad_add_probe(osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
                     osd_sink_pad_buffer_probe, NULL, NULL);
    gst_object_unref(osd_sink_pad);
    
    return pipeline;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

static void print_usage(const char *prog_name) {
    g_print("Uso: %s --vi-file <video> --left <val> --top <val> --width <val> "
            "--height <val> --time <seg> --file-name <reporte> --vo-file <salida> "
            "--mode <video|udp|udp_video> [--udp-host <host>] [--udp-port <puerto>]\n", 
            prog_name);
}

int main(int argc, char *argv[]) {
    AppConfig config = {0};
    config.udp_host = "192.168.0.255";
    config.udp_port = 5000;
    config.mode = "video";
    
    // Parsear argumentos
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--vi-file") == 0 && i + 1 < argc) {
            config.input_file = argv[++i];
        } else if (g_strcmp0(argv[i], "--left") == 0 && i + 1 < argc) {
            config.roi_left = g_strtod(argv[++i], NULL);
        } else if (g_strcmp0(argv[i], "--top") == 0 && i + 1 < argc) {
            config.roi_top = g_strtod(argv[++i], NULL);
        } else if (g_strcmp0(argv[i], "--width") == 0 && i + 1 < argc) {
            config.roi_width = g_strtod(argv[++i], NULL);
        } else if (g_strcmp0(argv[i], "--height") == 0 && i + 1 < argc) {
            config.roi_height = g_strtod(argv[++i], NULL);
        } else if (g_strcmp0(argv[i], "--time") == 0 && i + 1 < argc) {
            config.max_time = atoi(argv[++i]);
        } else if (g_strcmp0(argv[i], "--file-name") == 0 && i + 1 < argc) {
            config.report_file = argv[++i];
        } else if (g_strcmp0(argv[i], "--vo-file") == 0 && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (g_strcmp0(argv[i], "--mode") == 0 && i + 1 < argc) {
            config.mode = argv[++i];
        } else if (g_strcmp0(argv[i], "--udp-host") == 0 && i + 1 < argc) {
            config.udp_host = argv[++i];
        } else if (g_strcmp0(argv[i], "--udp-port") == 0 && i + 1 < argc) {
            config.udp_port = atoi(argv[++i]);
        }
    }
    
    // Validar argumentos requeridos
    if (!config.input_file || !config.report_file) {
        print_usage(argv[0]);
        return -1;
    }
    
    if (g_strcmp0(config.mode, "video") == 0 && !config.output_file) {
        g_printerr("Error: modo 'video' requiere --vo-file\n");
        return -1;
    }
    
    // Inicializar GStreamer
    gst_init(&argc, &argv);
    
    // Inicializar ROI
    init_roi_data(&config);
    
    g_print("Configuración:\n");
    g_print("  Video entrada: %s\n", config.input_file);
    g_print("  ROI: [%.2f, %.2f] - %.2fx%.2f\n", 
            config.roi_left, config.roi_top, config.roi_width, config.roi_height);
    g_print("  Tiempo máximo: %u segundos\n", config.max_time);
    g_print("  Reporte: %s\n", config.report_file);
    g_print("  Modo: %s\n", config.mode);
    
    // Crear pipeline
    GstElement *pipeline = create_pipeline(&config);
    if (!pipeline) {
        g_printerr("Error creando pipeline\n");
        return -1;
    }
    
    // Configurar bus
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    guint bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);
    
    // Configurar manejador de señales
    signal(SIGINT, sigint_handler);
    
    // Iniciar pipeline
    g_print("Iniciando pipeline...\n");
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    // Loop principal
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    
    // Limpieza
    g_print("Deteniendo pipeline...\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    
    generate_report();
    
    g_source_remove(bus_watch_id);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    
    g_hash_table_destroy(roi_data.tracked_persons);
    g_free(roi_data.report_filename);
    
    g_print("Aplicación finalizada\n");
    
    return 0;
}