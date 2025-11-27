#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <cuda_runtime_api.h>
#include <iostream>
#include <string>
#include <map>
#include <ctime>

extern "C" {
#include "gstnvdsmeta.h"
}

#define MUXER_OUTPUT_WIDTH 1280
#define MUXER_OUTPUT_HEIGHT 720
#define MUXER_BATCH_TIMEOUT_USEC 40000
#define PGIE_CONFIG_FILE "/home/lab_sistemas/Proyecto-Lab-Integrados/dstest1_pgie_config.txt"
#define CONFIDENCE_THRESHOLD 0.4  // Umbral para falsos positivos

typedef struct {
  gdouble roi_x;      // normalizado (0-1)
  gdouble roi_y;      
  gdouble roi_w;      
  gdouble roi_h;      
  gint roi_x_px;      // en píxeles
  gint roi_y_px;
  gint roi_w_px;
  gint roi_h_px;
  gdouble max_dwell_time;  // segundos
} RoiConfig;

typedef struct {
  guint64 object_id;
  gdouble entry_ts;       // timestamp de entrada
  gboolean overtime_logged; // ya se registró el overtime
  gboolean low_confidence; // posible falso positivo
} TrackedObject;

static RoiConfig roi_cfg;
static std::string report_path;
static std::string input_file_path;
static std::string output_file_path;
static FILE *report_fp = nullptr;

// Mapa para tracking de objetos individuales
static std::map<guint64, TrackedObject> tracked_objects;

static guint g_total_detections = 0;
static guint g_total_overtime   = 0;
static gdouble g_t0 = 0.0;

typedef enum {
  MODE_VIDEO = 0,
  MODE_UDP   = 1,
  MODE_BOTH  = 2
} OutputMode;

static OutputMode g_mode = MODE_VIDEO;

/* ------------------------------------------------------------------------ */
/* Función para convertir ROI normalizado a píxeles                         */
/* ------------------------------------------------------------------------ */
static void
calculate_roi_pixels()
{
  roi_cfg.roi_x_px = (gint)(roi_cfg.roi_x * MUXER_OUTPUT_WIDTH);
  roi_cfg.roi_y_px = (gint)(roi_cfg.roi_y * MUXER_OUTPUT_HEIGHT);
  roi_cfg.roi_w_px = (gint)(roi_cfg.roi_w * MUXER_OUTPUT_WIDTH);
  roi_cfg.roi_h_px = (gint)(roi_cfg.roi_h * MUXER_OUTPUT_HEIGHT);
}

/* ------------------------------------------------------------------------ */
/* Función para obtener timestamp formateado mm:ss                          */
/* ------------------------------------------------------------------------ */
static std::string
get_timestamp_str(gdouble now)
{
  gdouble rel = now - g_t0;
  int rel_sec = (int)(rel + 0.5);
  int mm = rel_sec / 60;
  int ss = rel_sec % 60;
  
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%d:%02d", mm, ss);
  return std::string(buffer);
}

/* ------------------------------------------------------------------------ */
/* Escribir header del reporte al final                                     */
/* ------------------------------------------------------------------------ */
static void
write_report_header()
{
  if (!report_fp) return;
  
  // Reabrir archivo para escribir header al inicio
  fclose(report_fp);
  
  // Leer contenido actual
  FILE *temp = fopen(report_path.c_str(), "r");
  std::string content;
  if (temp) {
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), temp)) {
      content += buffer;
    }
    fclose(temp);
  }
  
  // Reescribir con header
  report_fp = fopen(report_path.c_str(), "w");
  if (report_fp) {
    fprintf(report_fp, "ROI: left: %d top: %d width: %d height: %d\n",
            roi_cfg.roi_x_px, roi_cfg.roi_y_px, 
            roi_cfg.roi_w_px, roi_cfg.roi_h_px);
    fprintf(report_fp, "Max time: %.0fs\n", roi_cfg.max_dwell_time);
    fprintf(report_fp, "Detected: %u (%u)\n", g_total_detections, g_total_overtime);
    fprintf(report_fp, "%s", content.c_str());
    fclose(report_fp);
    report_fp = nullptr;
  }
}

/* ------------------------------------------------------------------------ */
/* Bus callback                                                             */
/* ------------------------------------------------------------------------ */
static gboolean
bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("\n=== End of stream ===\n");
      g_print ("Detected: %u (%u)\n", g_total_detections, g_total_overtime);
      
      // Escribir header del reporte
      write_report_header();
      
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar *debug = nullptr;
      GError *error = nullptr;
      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);
      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Dynamic pad linking qtdemux -> h264parse                                 */
/* ------------------------------------------------------------------------ */
static void
cb_new_pad (GstElement *qtdemux, GstPad *pad, gpointer data)
{
  GstElement *h264parser = (GstElement *) data;
  gchar *name = gst_pad_get_name (pad);

  if (g_str_has_prefix (name, "video_")) {
    if (!gst_element_link_pads (qtdemux, name, h264parser, "sink")) {
      g_printerr ("No se pudo enlazar %s de qtdemux con sink de h264parse\n", name);
    } else {
      g_print ("Enlazado dinámico: %s -> h264parse\n", name);
    }
  }

  g_free (name);
}

/* ------------------------------------------------------------------------ */
/* Pad probe en nvdsosd: tracking multi-objeto + ROI + tiempo              */
/* ------------------------------------------------------------------------ */
static GstPadProbeReturn
osd_sink_pad_buffer_probe (GstPad *pad, GstPadProbeInfo *info, gpointer u_data)
{
  GstBuffer *buf = (GstBuffer *) info->data;
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);
  if (!batch_meta)
    return GST_PAD_PROBE_OK;

  gdouble now = g_get_real_time () / 1e6;
  
  if (g_t0 == 0.0) {
    g_t0 = now;
  }

  for (NvDsMetaList *l_frame = batch_meta->frame_meta_list;
       l_frame != nullptr; l_frame = l_frame->next) {

    NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) l_frame->data;

    // Set para rastrear qué objetos están actualmente en ROI
    std::map<guint64, bool> objects_in_roi_now;
    
    gboolean any_obj_in_roi = FALSE;
    gboolean any_overtime = FALSE;

    // Recorrer objetos detectados
    for (NvDsMetaList *l_obj = frame_meta->obj_meta_list;
         l_obj != nullptr; l_obj = l_obj->next) {

      NvDsObjectMeta *obj_meta = (NvDsObjectMeta *) l_obj->data;

      // Solo procesar personas (class_id == 2)
      if (obj_meta->class_id != 2) {
        // Ocultar otros objetos
        obj_meta->rect_params.border_width = 0;
        obj_meta->rect_params.has_bg_color = 0;
        obj_meta->rect_params.bg_color.alpha = 0.0f;
        obj_meta->rect_params.border_color.alpha = 0.0f;
        obj_meta->text_params.display_text = NULL;
        continue;
      }

      // Coordenadas del bounding box
      float x = obj_meta->rect_params.left;
      float y = obj_meta->rect_params.top;
      float w = obj_meta->rect_params.width;
      float h = obj_meta->rect_params.height;
      
      // Convertir ROI normalizado a píxeles para comparación
      float roi_x_px = roi_cfg.roi_x * MUXER_OUTPUT_WIDTH;
      float roi_y_px = roi_cfg.roi_y * MUXER_OUTPUT_HEIGHT;
      float roi_w_px = roi_cfg.roi_w * MUXER_OUTPUT_WIDTH;
      float roi_h_px = roi_cfg.roi_h * MUXER_OUTPUT_HEIGHT;

      // Verificar si está dentro de ROI
      if (x >= roi_x_px &&
          x + w <= roi_x_px + roi_w_px &&
          y >= roi_y_px &&
          y + h <= roi_y_px + roi_h_px) {

        any_obj_in_roi = TRUE;
        guint64 obj_id = obj_meta->object_id;
        objects_in_roi_now[obj_id] = true;

        // Detectar posibles falsos positivos (baja confianza)
        gboolean low_conf = (obj_meta->confidence < CONFIDENCE_THRESHOLD);

        // Si es nuevo objeto en ROI
        if (tracked_objects.find(obj_id) == tracked_objects.end()) {
          TrackedObject new_obj;
          new_obj.object_id = obj_id;
          new_obj.entry_ts = now;
          new_obj.overtime_logged = FALSE;
          new_obj.low_confidence = low_conf;
          tracked_objects[obj_id] = new_obj;

          std::string ts = get_timestamp_str(now);
          g_print("%s ENTER person [ID:%lu]%s\n", 
                  ts.c_str(), obj_id,
                  low_conf ? " [LOW_CONF]" : "");

          if (report_fp) {
            gdouble rel = now - g_t0;
            fprintf(report_fp, "%s person enter%s\n", 
                    ts.c_str(),
                    low_conf ? " (possible false positive)" : "");
            fflush(report_fp);
          }
        } else {
          // Objeto ya estaba siendo tracked
          TrackedObject &obj = tracked_objects[obj_id];
          gdouble dwell = now - obj.entry_ts;

          // Verificar si superó el tiempo y no se ha loggeado
          if (dwell > roi_cfg.max_dwell_time && !obj.overtime_logged) {
            obj.overtime_logged = TRUE;
            any_overtime = TRUE;

            std::string ts = get_timestamp_str(now);
            int dwell_sec = (int)(dwell + 0.5);
            
            g_print("%s person time %ds alert [ID:%lu]\n",
                    ts.c_str(), dwell_sec, obj_id);

            if (report_fp) {
              fprintf(report_fp, "%s person time %ds alert\n",
                      ts.c_str(), dwell_sec);
              fflush(report_fp);
            }
          } else if (dwell > roi_cfg.max_dwell_time) {
            any_overtime = TRUE;
          }
        }
      }
    }

    // Procesar objetos que SALIERON de la ROI
    auto it = tracked_objects.begin();
    while (it != tracked_objects.end()) {
      guint64 obj_id = it->first;
      
      if (objects_in_roi_now.find(obj_id) == objects_in_roi_now.end()) {
        // El objeto ya no está en ROI -> EXIT
        TrackedObject &obj = it->second;
        gdouble dwell = now - obj.entry_ts;
        gboolean overtime = (dwell > roi_cfg.max_dwell_time);

        g_total_detections++;
        if (overtime) {
          g_total_overtime++;
        }

        std::string ts = get_timestamp_str(now);
        int dwell_sec = (int)(dwell + 0.5);

        if (!obj.overtime_logged) {
          g_print("%s person time %ds%s [ID:%lu]%s\n",
                  ts.c_str(), dwell_sec,
                  overtime ? " alert" : "",
                  obj_id,
                  obj.low_confidence ? " [LOW_CONF]" : "");
        } else {
          g_print("%s EXIT person [ID:%lu]\n", ts.c_str(), obj_id);
        }

        if (report_fp && !obj.overtime_logged) {
          fprintf(report_fp, "%s person time %ds%s\n",
                  ts.c_str(), dwell_sec,
                  overtime ? " alert" : "");
          fflush(report_fp);
        }

        it = tracked_objects.erase(it);
      } else {
        ++it;
      }
    }

    // Dibujar rectángulo de ROI con color según estado
    NvDsDisplayMeta *display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
    if (!display_meta)
      continue;

    display_meta->num_rects = 1;
    NvOSD_RectParams *rect_params = &display_meta->rect_params[0];

    rect_params->left   = roi_cfg.roi_x * MUXER_OUTPUT_WIDTH;
    rect_params->top    = roi_cfg.roi_y * MUXER_OUTPUT_HEIGHT;
    rect_params->width  = roi_cfg.roi_w * MUXER_OUTPUT_WIDTH;
    rect_params->height = roi_cfg.roi_h * MUXER_OUTPUT_HEIGHT;
    rect_params->border_width = 3;
    rect_params->has_bg_color = 1;

    // Estado 1: ROI vacía -> verde suave
    if (!any_obj_in_roi) {
      rect_params->bg_color.red   = 0.0f;
      rect_params->bg_color.green = 0.6f;
      rect_params->bg_color.blue  = 0.0f;
      rect_params->bg_color.alpha = 0.2f;

    // Estado 2: objetos dentro pero sin sobretiempo -> verde marcado
    } else if (any_obj_in_roi && !any_overtime) {
      rect_params->bg_color.red   = 0.0f;
      rect_params->bg_color.green = 0.9f;
      rect_params->bg_color.blue  = 0.0f;
      rect_params->bg_color.alpha = 0.3f;

    // Estado 3: al menos un objeto con sobretiempo -> rojo
    } else {
      rect_params->bg_color.red   = 0.9f;
      rect_params->bg_color.green = 0.0f;
      rect_params->bg_color.blue  = 0.0f;
      rect_params->bg_color.alpha = 0.4f;
    }

    // Borde blanco
    rect_params->border_color.red   = 1.0f;
    rect_params->border_color.green = 1.0f;
    rect_params->border_color.blue  = 1.0f;
    rect_params->border_color.alpha = 1.0f;

    nvds_add_display_meta_to_frame(frame_meta, display_meta);
  }

  return GST_PAD_PROBE_OK;
}

/* ------------------------------------------------------------------------ */
/* Parser de argumentos con formato --flag                                  */
/* ------------------------------------------------------------------------ */
static gboolean
parse_arguments(int argc, char *argv[])
{
  if (argc < 2) {
    g_printerr("Uso: %s vi-file <video.mp4> --left <val> --top <val> --width <val> --height <val> --time <s> --file-name <reporte.txt> vo-file <salida.mp4> --mode <video|udp|both>\n", argv[0]);
    return FALSE;
  }

  gboolean has_input = FALSE;
  gboolean has_left = FALSE, has_top = FALSE, has_width = FALSE, has_height = FALSE;
  gboolean has_time = FALSE, has_report = FALSE, has_output = FALSE, has_mode = FALSE;

  for (int i = 1; i < argc; i++) {
    if (g_strcmp0(argv[i], "vi-file") == 0 && i + 1 < argc) {
      input_file_path = argv[++i];
      has_input = TRUE;
    }
    else if (g_strcmp0(argv[i], "--left") == 0 && i + 1 < argc) {
      roi_cfg.roi_x = g_ascii_strtod(argv[++i], nullptr);
      has_left = TRUE;
    }
    else if (g_strcmp0(argv[i], "--top") == 0 && i + 1 < argc) {
      roi_cfg.roi_y = g_ascii_strtod(argv[++i], nullptr);
      has_top = TRUE;
    }
    else if (g_strcmp0(argv[i], "--width") == 0 && i + 1 < argc) {
      roi_cfg.roi_w = g_ascii_strtod(argv[++i], nullptr);
      has_width = TRUE;
    }
    else if (g_strcmp0(argv[i], "--height") == 0 && i + 1 < argc) {
      roi_cfg.roi_h = g_ascii_strtod(argv[++i], nullptr);
      has_height = TRUE;
    }
    else if (g_strcmp0(argv[i], "--time") == 0 && i + 1 < argc) {
      roi_cfg.max_dwell_time = g_ascii_strtod(argv[++i], nullptr);
      has_time = TRUE;
    }
    else if (g_strcmp0(argv[i], "--file-name") == 0 && i + 1 < argc) {
      report_path = argv[++i];
      has_report = TRUE;
    }
    else if (g_strcmp0(argv[i], "vo-file") == 0 && i + 1 < argc) {
      output_file_path = argv[++i];
      has_output = TRUE;
    }
    else if (g_strcmp0(argv[i], "--mode") == 0 && i + 1 < argc) {
      const gchar *mode_str = argv[++i];
      if (g_strcmp0(mode_str, "video") == 0) {
        g_mode = MODE_VIDEO;
      } else if (g_strcmp0(mode_str, "udp") == 0) {
        g_mode = MODE_UDP;
      } else if (g_strcmp0(mode_str, "both") == 0 || g_strcmp0(mode_str, "udp_video") == 0) {
        g_mode = MODE_BOTH;
      } else {
        g_printerr("Modo inválido: %s (use: video | udp | both)\n", mode_str);
        return FALSE;
      }
      has_mode = TRUE;
    }
  }

  if (!has_input || !has_left || !has_top || !has_width || !has_height ||
      !has_time || !has_report || !has_output || !has_mode) {
    g_printerr("Faltan parámetros requeridos\n");
    g_printerr("Ejemplo: %s vi-file street.mp4 --left 0.2 --top 0.3 --width 0.5 --height 0.4 --time 5 --file-name report.txt vo-file output.mp4 --mode video\n", argv[0]);
    return FALSE;
  }

  return TRUE;
}

/* ------------------------------------------------------------------------ */
/* MAIN                                                                     */
/* ------------------------------------------------------------------------ */
int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  if (!parse_arguments(argc, argv)) {
    return -1;
  }

  // Calcular ROI en píxeles
  calculate_roi_pixels();

  // Abrir archivo de reporte
  report_fp = fopen(report_path.c_str(), "w");
  if (!report_fp) {
    g_printerr("No se pudo abrir el archivo de reporte %s\n", report_path.c_str());
    return -1;
  }

  GMainLoop *loop = g_main_loop_new(nullptr, FALSE);

  // Crear elementos
  GstElement *pipeline    = gst_pipeline_new("deepstream_roi_app");
  GstElement *source      = gst_element_factory_make("filesrc", "file-source");
  GstElement *qtdemux     = gst_element_factory_make("qtdemux", "qtdemux");
  GstElement *h264parser  = gst_element_factory_make("h264parse", "h264-parser");
  GstElement *decoder     = gst_element_factory_make("nvv4l2decoder", "nvv4l2-decoder");
  GstElement *streammux   = gst_element_factory_make("nvstreammux", "stream-muxer");
  GstElement *pgie        = gst_element_factory_make("nvinfer", "primary-nvinference-engine");
  GstElement *nvvidconv   = gst_element_factory_make("nvvideoconvert", "nvvideo-converter");
  GstElement *nvosd       = gst_element_factory_make("nvdsosd", "nv-onscreendisplay");
  GstElement *nvvidconv2  = gst_element_factory_make("nvvideoconvert", "nvvideo-converter2");

  GstElement *encoder     = gst_element_factory_make("nvv4l2h264enc", "h264-encoder-file");
  GstElement *h264parser2 = gst_element_factory_make("h264parse", "h264-parser-2");
  GstElement *mux         = gst_element_factory_make("qtmux", "mp4-mux");
  GstElement *sink        = gst_element_factory_make("filesink", "file-sink");

  GstElement *encoder_udp = gst_element_factory_make("nvv4l2h264enc", "h264-encoder-udp");
  GstElement *payloader   = gst_element_factory_make("rtph264pay", "rtp-payloader");
  GstElement *udp_sink    = gst_element_factory_make("udpsink", "udp-sink");

  GstElement *tee         = gst_element_factory_make("tee", "tee");
  GstElement *queue_file  = gst_element_factory_make("queue", "queue-file");
  GstElement *queue_udp   = gst_element_factory_make("queue", "queue-udp");

  if (!pipeline || !source || !qtdemux || !h264parser || !decoder ||
      !streammux || !pgie || !nvvidconv || !nvosd || !nvvidconv2 ||
      !encoder || !h264parser2 || !mux || !sink ||
      !encoder_udp || !payloader || !udp_sink ||
      !tee || !queue_file || !queue_udp) {
    g_printerr("No se pudo crear uno o más elementos. Saliendo.\n");
    return -1;
  }

  // Configurar elementos
  g_object_set(G_OBJECT(source), "location", input_file_path.c_str(), nullptr);

  g_object_set(G_OBJECT(streammux),
               "batch-size", 1,
               "width", MUXER_OUTPUT_WIDTH,
               "height", MUXER_OUTPUT_HEIGHT,
               "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC,
               nullptr);

  g_object_set(G_OBJECT(pgie),
               "config-file-path", PGIE_CONFIG_FILE,
               nullptr);

  g_object_set(G_OBJECT(sink), "location", output_file_path.c_str(), nullptr);

  g_object_set(G_OBJECT(encoder_udp),
               "insert-sps-pps", TRUE,
               "bitrate", 4000000,
               nullptr);

  g_object_set(G_OBJECT(payloader),
               "config-interval", 1,
               "pt", 96,
               nullptr);

  g_object_set(G_OBJECT(udp_sink),
               "host", "127.0.0.1",
               "port", 5000,
               "sync", FALSE,
               "async", FALSE,
               nullptr);

  // Bus
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  guint bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  // Agregar elementos al pipeline
  gst_bin_add_many(GST_BIN(pipeline),
                   source, qtdemux, h264parser, decoder,
                   streammux, pgie, nvvidconv, nvosd, nvvidconv2,
                   nullptr);

  // Enlaces
  if (!gst_element_link(source, qtdemux)) {
    g_printerr("No se pudo enlazar source -> qtdemux\n");
    return -1;
  }

  if (!gst_element_link(h264parser, decoder)) {
    g_printerr("No se pudo enlazar h264parse -> decoder\n");
    return -1;
  }

  if (!gst_element_link_many(streammux, pgie, nvvidconv, nvosd, nvvidconv2, nullptr)) {
    g_printerr("No se pudo enlazar streammux -> ... -> nvvidconv2\n");
    return -1;
  }

  g_signal_connect(qtdemux, "pad-added", G_CALLBACK(cb_new_pad), h264parser);

  // Enlace decoder -> streammux
  {
    GstPad *sinkpad = gst_element_get_request_pad(streammux, "sink_0");
    GstPad *srcpad = gst_element_get_static_pad(decoder, "src");
    if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
      g_printerr("No se pudo enlazar decoder -> streammux\n");
      return -1;
    }
    gst_object_unref(sinkpad);
    gst_object_unref(srcpad);
  }

  // Configurar salida según modo
  if (g_mode == MODE_VIDEO) {
    gst_bin_add_many(GST_BIN(pipeline), encoder, h264parser2, mux, sink, nullptr);
    if (!gst_element_link_many(nvvidconv2, encoder, h264parser2, mux, sink, nullptr)) {
      g_printerr("No se pudo enlazar rama video\n");
      return -1;
    }
  } else if (g_mode == MODE_UDP) {
    gst_bin_add_many(GST_BIN(pipeline), encoder_udp, payloader, udp_sink, nullptr);
    if (!gst_element_link_many(nvvidconv2, encoder_udp, payloader, udp_sink, nullptr)) {
      g_printerr("No se pudo enlazar rama UDP\n");
      return -1;
    }
  } else if (g_mode == MODE_BOTH) {
    gst_bin_add_many(GST_BIN(pipeline),
                     tee, queue_file, encoder, h264parser2, mux, sink,
                     queue_udp, encoder_udp, payloader, udp_sink, nullptr);

    if (!gst_element_link(nvvidconv2, tee)) {
      g_printerr("No se pudo enlazar nvvidconv2 -> tee\n");
      return -1;
    }

    if (!gst_element_link_many(queue_file, encoder, h264parser2, mux, sink, nullptr)) {
      g_printerr("No se pudo enlazar rama archivo\n");
      return -1;
    }

    if (!gst_element_link_many(queue_udp, encoder_udp, payloader, udp_sink, nullptr)) {
      g_printerr("No se pudo enlazar rama UDP\n");
      return -1;
    }

    GstPad *tee_src_pad_file = gst_element_get_request_pad(tee, "src_%u");
    GstPad *queue_sink_pad_file = gst_element_get_static_pad(queue_file, "sink");
    gst_pad_link(tee_src_pad_file, queue_sink_pad_file);
    gst_object_unref(tee_src_pad_file);
    gst_object_unref(queue_sink_pad_file);

    GstPad *tee_src_pad_udp = gst_element_get_request_pad(tee, "src_%u");
    GstPad *queue_sink_pad_udp = gst_element_get_static_pad(queue_udp, "sink");
    gst_pad_link(tee_src_pad_udp, queue_sink_pad_udp);
    gst_object_unref(tee_src_pad_udp);
    gst_object_unref(queue_sink_pad_udp);
  }

  // Pad probe
  {
    GstPad *osd_sink_pad = gst_element_get_static_pad(nvosd, "sink");
    if (!osd_sink_pad) {
      g_printerr("No se pudo obtener el pad sink de nvdsosd\n");
      return -1;
    }
    gst_pad_add_probe(osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
                      osd_sink_pad_buffer_probe, nullptr, nullptr);
    gst_object_unref(osd_sink_pad);
  }

  // Imprimir configuración
  g_print("\n=== Configuración del Sistema ===\n");
  g_print("Archivo de entrada: %s\n", input_file_path.c_str());
  g_print("ROI (normalizado): left: %.2f top: %.2f width: %.2f height: %.2f\n",
          roi_cfg.roi_x, roi_cfg.roi_y, roi_cfg.roi_w, roi_cfg.roi_h);
  g_print("ROI (píxeles): left: %d top: %d width: %d height: %d\n",
          roi_cfg.roi_x_px, roi_cfg.roi_y_px, roi_cfg.roi_w_px, roi_cfg.roi_h_px);
  g_print("Tiempo máximo: %.0fs\n", roi_cfg.max_dwell_time);
  g_print("Modo: %s\n", g_mode == MODE_VIDEO ? "video" : 
                        g_mode == MODE_UDP ? "udp" : "both");
  g_print("Reporte: %s\n", report_path.c_str());
  g_print("Salida: %s\n", output_file_path.c_str());
  g_print("Umbral de confianza: %.2f\n", CONFIDENCE_THRESHOLD);
  g_print("================================\n\n");

  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  g_print("Reproduciendo...\n");
  g_main_loop_run(loop);

  // Limpieza
  g_print("\nDeteniendo pipeline...\n");
  gst_element_set_state(pipeline, GST_STATE_NULL);

  g_print("Liberando recursos...\n");
  gst_object_unref(GST_OBJECT(pipeline));
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);

  return 0;
}
