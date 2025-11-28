#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <cuda_runtime_api.h>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include "gstnvdsmeta.h"
}

#define MUXER_OUTPUT_WIDTH 1280
#define MUXER_OUTPUT_HEIGHT 720
#define MUXER_BATCH_TIMEOUT_USEC 40000
#define PGIE_CONFIG_FILE "/home/lab_sistemas/Proyecto-Lab-Integrados/dstest1_pgie_config.txt"

// Estructura de configuracion
typedef struct {
  gdouble roi_x;      // normalizado (0-1)
  gdouble roi_y;      
  gdouble roi_w;      
  gdouble roi_h;      
  gint roi_x_px;      // en pixeles (cache para optimizacion)
  gint roi_y_px;
  gint roi_w_px;
  gint roi_h_px;
  gdouble max_dwell_time;  // segundos

  gboolean roi_occupied;
  gboolean roi_over_threshold;
  gdouble roi_entry_ts;    // segundos (reloj real)
} RoiConfig;

static RoiConfig roi_cfg;
static std::string report_path;
static std::string input_file_path;
static std::string output_file_path;
static FILE *report_fp = nullptr;

// Contadores globales
static guint g_total_detections = 0;
static guint g_total_overtime   = 0;
static gdouble g_t0 = 0.0;

typedef enum {
  MODE_VIDEO = 0,
  MODE_UDP   = 1,
  MODE_BOTH  = 2
} OutputMode;

static OutputMode g_mode = MODE_VIDEO;

// Pre-calculo de pixeles para evitar operaciones float en cada frame
static void
calculate_roi_pixels()
{
  roi_cfg.roi_x_px = (gint)(roi_cfg.roi_x * MUXER_OUTPUT_WIDTH);
  roi_cfg.roi_y_px = (gint)(roi_cfg.roi_y * MUXER_OUTPUT_HEIGHT);
  roi_cfg.roi_w_px = (gint)(roi_cfg.roi_w * MUXER_OUTPUT_WIDTH);
  roi_cfg.roi_h_px = (gint)(roi_cfg.roi_h * MUXER_OUTPUT_HEIGHT);
}

// Obtener timestamp formateado de manera eficiente
static void
get_timestamp_str(gdouble now, char* buffer, size_t size)
{
  gdouble rel = now - g_t0;
  int rel_sec = (int)(rel + 0.5);
  int mm = rel_sec / 60;
  int ss = rel_sec % 60;
  snprintf(buffer, size, "%d:%02d", mm, ss);
}

// Escritura del header al finalizar
static void
write_report_header()
{
  if (!report_fp) return;
  
  fclose(report_fp);
  
  // Leer contenido existente
  FILE *temp = fopen(report_path.c_str(), "r");
  std::string content;
  if (temp) {
    char buffer[4096];
    while (size_t len = fread(buffer, 1, sizeof(buffer) - 1, temp)) {
      buffer[len] = '\0';
      content += buffer;
    }
    fclose(temp);
  }
  
  // Reescribir
  report_fp = fopen(report_path.c_str(), "w");
  if (report_fp) {
    fprintf(report_fp, "ROI: left: %d top: %d width: %d height: %d\n",
            roi_cfg.roi_x_px, roi_cfg.roi_y_px, 
            roi_cfg.roi_w_px, roi_cfg.roi_h_px);
    fprintf(report_fp, "Max time: %.0fs\n", roi_cfg.max_dwell_time);
    fprintf(report_fp, "Detected: %u (%u)\n", g_total_detections, g_total_overtime);
    fputs(content.c_str(), report_fp);
    fclose(report_fp);
    report_fp = nullptr;
  }
}

static gboolean
bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("\n=== End of stream ===\n");
      g_print ("Detected: %u (%u)\n", g_total_detections, g_total_overtime);
      write_report_header();
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar *debug = nullptr;
      GError *error = nullptr;
      gst_message_parse_error (msg, &error, &debug);
      g_printerr ("Error: %s\nDebug: %s\n", error->message, debug ? debug : "none");
      g_free (debug);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

// Enlace dinamico para qtdemux
static void
cb_new_pad (GstElement *qtdemux, GstPad *pad, gpointer data)
{
  GstElement *h264parser = (GstElement *) data;
  gchar *name = gst_pad_get_name (pad);

  if (g_str_has_prefix (name, "video_")) {
    if (!gst_element_link_pads (qtdemux, name, h264parser, "sink")) {
      g_printerr ("Fallo enlace dinamico: %s\n", name);
    } else {
      g_print ("Enlace dinamico exitoso: %s\n", name);
    }
  }
  g_free (name);
}

// Probe principal optimizado
static GstPadProbeReturn
osd_sink_pad_buffer_probe (GstPad *pad, GstPadProbeInfo *info, gpointer u_data)
{
  GstBuffer *buf = (GstBuffer *) info->data;
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);
  if (!batch_meta) return GST_PAD_PROBE_OK;

  gdouble now = g_get_real_time () / 1e6;
  if (g_t0 == 0.0) g_t0 = now;

  for (NvDsMetaList *l_frame = batch_meta->frame_meta_list; l_frame != nullptr; l_frame = l_frame->next) {
    NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) l_frame->data;
    gboolean roi_has_obj = FALSE;

    // Bucle de objetos
    for (NvDsMetaList *l_obj = frame_meta->obj_meta_list; l_obj != nullptr; l_obj = l_obj->next) {
      NvDsObjectMeta *obj_meta = (NvDsObjectMeta *) l_obj->data;

      // Clase 2 = Persona
      if (obj_meta->class_id != 2) {
        // Ocultar objetos no relevantes
        obj_meta->rect_params.border_width = 0;
        obj_meta->rect_params.has_bg_color = 0;
        obj_meta->text_params.display_text = NULL;
        continue;
      }

      // Optimizacion: Usar los valores pre-calculados en pixeles
      // en lugar de multiplicar floats en cada iteracion
      float x = obj_meta->rect_params.left;
      float y = obj_meta->rect_params.top;
      float w = obj_meta->rect_params.width;
      float h = obj_meta->rect_params.height;

      // Logica de ROI (todo el cuerpo dentro)
      if (x >= roi_cfg.roi_x_px &&
          x + w <= roi_cfg.roi_x_px + roi_cfg.roi_w_px &&
          y >= roi_cfg.roi_y_px &&
          y + h <= roi_cfg.roi_y_px + roi_cfg.roi_h_px) {
        roi_has_obj = TRUE;
        break; // Ya encontramos uno, no necesitamos revisar mas para activar la ROI
      }
    }

    // Maquina de estados de la ROI
    char time_buf[32];
    
    if (roi_has_obj && !roi_cfg.roi_occupied) {
      // ENTER
      roi_cfg.roi_occupied = TRUE;
      roi_cfg.roi_over_threshold = FALSE;
      roi_cfg.roi_entry_ts = now;

      get_timestamp_str(now, time_buf, sizeof(time_buf));
      g_print ("%s ENTER\n", time_buf);

      if (report_fp) {
        fprintf (report_fp, "ENTER,%.3f,,\n", now - g_t0);
      }

    } else if (!roi_has_obj && roi_cfg.roi_occupied) {
      // EXIT
      gdouble dwell = now - roi_cfg.roi_entry_ts;
      gboolean overtime = (dwell > roi_cfg.max_dwell_time);

      g_total_detections++;
      if (overtime) g_total_overtime++;

      get_timestamp_str(now, time_buf, sizeof(time_buf));
      g_print ("%s person time %ds%s\n", time_buf, (int)(dwell + 0.5), overtime ? " alert" : "");

      if (report_fp) {
        fprintf (report_fp, "EXIT,%.3f,%.3f,%s\n", now - g_t0, dwell, overtime ? "OVERTIME" : "OK");
      }

      roi_cfg.roi_occupied = FALSE;
      roi_cfg.roi_over_threshold = FALSE;
      roi_cfg.roi_entry_ts = 0.0;

    } else if (roi_has_obj && roi_cfg.roi_occupied && !roi_cfg.roi_over_threshold) {
      // CHECK TIMEOUT
      gdouble dwell = now - roi_cfg.roi_entry_ts;
      if (dwell > roi_cfg.max_dwell_time) {
        roi_cfg.roi_over_threshold = TRUE;
        
        get_timestamp_str(now, time_buf, sizeof(time_buf));
        g_print ("%s OVERTIME person time %ds (max %.0fs)\n", time_buf, (int)(dwell + 0.5), roi_cfg.max_dwell_time);

        if (report_fp) {
          fprintf (report_fp, "OVERTIME,%.3f,%.3f,OVERTIME\n", now - g_t0, dwell);
        }
      }
    }

    // Dibujar ROI en pantalla
    NvDsDisplayMeta *display_meta = nvds_acquire_display_meta_from_pool (batch_meta);
    if (display_meta) {
      display_meta->num_rects = 1;
      NvOSD_RectParams *rect = &display_meta->rect_params[0];

      rect->left   = roi_cfg.roi_x_px;
      rect->top    = roi_cfg.roi_y_px;
      rect->width  = roi_cfg.roi_w_px;
      rect->height = roi_cfg.roi_h_px;
      rect->border_width = 3;
      rect->has_bg_color = 1;
      rect->border_color = (NvOSD_ColorParams){1.0f, 1.0f, 1.0f, 1.0f}; // Blanco

      // Seleccion de color eficiente
      if (!roi_cfg.roi_occupied) {
        // Verde suave (Vacio)
        rect->bg_color = (NvOSD_ColorParams){0.0f, 0.6f, 0.0f, 0.2f};
      } else if (!roi_cfg.roi_over_threshold) {
        // Verde intenso (Ocupado OK)
        rect->bg_color = (NvOSD_ColorParams){0.0f, 0.9f, 0.0f, 0.3f};
      } else {
        // Rojo (Alerta)
        rect->bg_color = (NvOSD_ColorParams){1.0f, 0.0f, 0.0f, 0.6f};
      }
      nvds_add_display_meta_to_frame (frame_meta, display_meta);
    }
  }
  return GST_PAD_PROBE_OK;
}

// Parser de argumentos
static gboolean
parse_arguments(int argc, char *argv[])
{
  if (argc < 2) {
    g_printerr("Uso: %s vi-file <video> --left <0-1> --top <0-1> --width <0-1> --height <0-1> --time <s> --file-name <csv> vo-file <out> --mode <video|udp|both>\n", argv[0]);
    return FALSE;
  }

  // Mapa simple de parametros requeridos
  int params_found = 0;
  
  for (int i = 1; i < argc; i++) {
    if (i + 1 >= argc) continue; // Evitar overflow

    if (!g_strcmp0(argv[i], "vi-file")) {
      input_file_path = argv[++i]; params_found++;
    } else if (!g_strcmp0(argv[i], "--left")) {
      roi_cfg.roi_x = g_ascii_strtod(argv[++i], nullptr); params_found++;
    } else if (!g_strcmp0(argv[i], "--top")) {
      roi_cfg.roi_y = g_ascii_strtod(argv[++i], nullptr); params_found++;
    } else if (!g_strcmp0(argv[i], "--width")) {
      roi_cfg.roi_w = g_ascii_strtod(argv[++i], nullptr); params_found++;
    } else if (!g_strcmp0(argv[i], "--height")) {
      roi_cfg.roi_h = g_ascii_strtod(argv[++i], nullptr); params_found++;
    } else if (!g_strcmp0(argv[i], "--time")) {
      roi_cfg.max_dwell_time = g_ascii_strtod(argv[++i], nullptr); params_found++;
    } else if (!g_strcmp0(argv[i], "--file-name")) {
      report_path = argv[++i]; params_found++;
    } else if (!g_strcmp0(argv[i], "vo-file")) {
      output_file_path = argv[++i]; params_found++;
    } else if (!g_strcmp0(argv[i], "--mode")) {
      const gchar *m = argv[++i];
      if (!g_strcmp0(m, "video")) g_mode = MODE_VIDEO;
      else if (!g_strcmp0(m, "udp")) g_mode = MODE_UDP;
      else g_mode = MODE_BOTH;
      params_found++;
    }
  }

  if (params_found < 9) {
    g_printerr("Faltan parametros. Se requieren 9 argumentos configurables.\n");
    return FALSE;
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  if (!parse_arguments(argc, argv)) return -1;

  calculate_roi_pixels();
  roi_cfg.roi_occupied = FALSE;
  
  // Inicializar CSV
  report_fp = fopen(report_path.c_str(), "w");
  if (!report_fp) {
    g_printerr("Error abriendo reporte %s\n", report_path.c_str());
    return -1;
  }
  fprintf(report_fp, "event,time,dwell,flag\n");
  // No hacemos fflush aqui para ganar velocidad, el SO se encarga o el fclose final

  GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
  GstElement *pipeline = gst_pipeline_new("ds-roi-app");
  
  // Creacion de elementos masiva
  GstElement *source      = gst_element_factory_make("filesrc", "src");
  GstElement *qtdemux     = gst_element_factory_make("qtdemux", "demux");
  GstElement *h264parser  = gst_element_factory_make("h264parse", "parse1");
  GstElement *decoder     = gst_element_factory_make("nvv4l2decoder", "dec");
  GstElement *streammux   = gst_element_factory_make("nvstreammux", "mux");
  GstElement *pgie        = gst_element_factory_make("nvinfer", "pgie");
  GstElement *nvvidconv   = gst_element_factory_make("nvvideoconvert", "conv1");
  GstElement *nvosd       = gst_element_factory_make("nvdsosd", "osd");
  GstElement *nvvidconv2  = gst_element_factory_make("nvvideoconvert", "conv2");

  if (!pipeline || !source || !pgie || !nvosd) {
    g_printerr("Fallo al crear elementos core.\n");
    return -1;
  }

  // Configuracion Core
  g_object_set(G_OBJECT(source), "location", input_file_path.c_str(), nullptr);
  g_object_set(G_OBJECT(streammux), "batch-size", 1, "width", MUXER_OUTPUT_WIDTH, "height", MUXER_OUTPUT_HEIGHT, "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC, nullptr);
  g_object_set(G_OBJECT(pgie), "config-file-path", PGIE_CONFIG_FILE, nullptr);

  gst_bin_add_many(GST_BIN(pipeline), source, qtdemux, h264parser, decoder, streammux, pgie, nvvidconv, nvosd, nvvidconv2, nullptr);

  // Enlaces Core
  if (!gst_element_link(source, qtdemux) || !gst_element_link(h264parser, decoder) ||
      !gst_element_link_many(streammux, pgie, nvvidconv, nvosd, nvvidconv2, nullptr)) {
    g_printerr("Error enlazando pipeline core.\n");
    return -1;
  }
  g_signal_connect(qtdemux, "pad-added", G_CALLBACK(cb_new_pad), h264parser);

  // Enlace Decoder -> Muxer
  GstPad *sinkpad = gst_element_get_request_pad(streammux, "sink_0");
  GstPad *srcpad = gst_element_get_static_pad(decoder, "src");
  if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
    g_printerr("Error decoder->muxer\n");
    return -1;
  }
  gst_object_unref(sinkpad); gst_object_unref(srcpad);

  // Rama de salida
  if (g_mode == MODE_VIDEO || g_mode == MODE_BOTH) {
    GstElement *enc = gst_element_factory_make("nvv4l2h264enc", "enc");
    GstElement *parse2 = gst_element_factory_make("h264parse", "parse2");
    GstElement *mux = gst_element_factory_make("qtmux", "qtmux");
    GstElement *filesink = gst_element_factory_make("filesink", "fsink");
    
    g_object_set(G_OBJECT(filesink), "location", output_file_path.c_str(), nullptr);
    gst_bin_add_many(GST_BIN(pipeline), enc, parse2, mux, filesink, nullptr);
    
    if (g_mode == MODE_VIDEO) {
      gst_element_link_many(nvvidconv2, enc, parse2, mux, filesink, nullptr);
    } else {
      // Configuracion para BOTH con Tee y Queue
      GstElement *tee = gst_element_factory_make("tee", "tee");
      GstElement *q_file = gst_element_factory_make("queue", "q_file");
      GstElement *q_udp = gst_element_factory_make("queue", "q_udp");
      GstElement *enc_u = gst_element_factory_make("nvv4l2h264enc", "enc_u");
      GstElement *pay = gst_element_factory_make("rtph264pay", "pay");
      GstElement *udpsink = gst_element_factory_make("udpsink", "udp");

      g_object_set(G_OBJECT(enc_u), "insert-sps-pps", TRUE, "bitrate", 4000000, nullptr);
      g_object_set(G_OBJECT(pay), "config-interval", 1, "pt", 96, nullptr);
      g_object_set(G_OBJECT(udpsink), "host", "127.0.0.1", "port", 5000, "async", FALSE, nullptr);

      gst_bin_add_many(GST_BIN(pipeline), tee, q_file, q_udp, enc_u, pay, udpsink, nullptr);
      
      gst_element_link(nvvidconv2, tee);
      gst_element_link_many(q_file, enc, parse2, mux, filesink, nullptr);
      gst_element_link_many(q_udp, enc_u, pay, udpsink, nullptr);

      // Enlaces Tee
      GstPad *tee_file = gst_element_get_request_pad(tee, "src_%u");
      GstPad *tee_udp = gst_element_get_request_pad(tee, "src_%u");
      GstPad *sink_file = gst_element_get_static_pad(q_file, "sink");
      GstPad *sink_udp = gst_element_get_static_pad(q_udp, "sink");
      gst_pad_link(tee_file, sink_file);
      gst_pad_link(tee_udp, sink_udp);
      gst_object_unref(tee_file); gst_object_unref(tee_udp);
      gst_object_unref(sink_file); gst_object_unref(sink_udp);
    }
  } else {
      // Solo UDP
      GstElement *enc = gst_element_factory_make("nvv4l2h264enc", "enc_u");
      GstElement *pay = gst_element_factory_make("rtph264pay", "pay");
      GstElement *udpsink = gst_element_factory_make("udpsink", "udp");
      
      g_object_set(G_OBJECT(enc), "insert-sps-pps", TRUE, "bitrate", 4000000, nullptr);
      g_object_set(G_OBJECT(udpsink), "host", "127.0.0.1", "port", 5000, "async", FALSE, nullptr);

      gst_bin_add_many(GST_BIN(pipeline), enc, pay, udpsink, nullptr);
      gst_element_link_many(nvvidconv2, enc, pay, udpsink, nullptr);
  }

  // OSD Probe
  GstPad *osd_pad = gst_element_get_static_pad(nvosd, "sink");
  gst_pad_add_probe(osd_pad, GST_PAD_PROBE_TYPE_BUFFER, osd_sink_pad_buffer_probe, nullptr, nullptr);
  gst_object_unref(osd_pad);

  // Bus y Ejecucion
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  guint bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  g_print("Pipeline ejecutandose...\n");
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_main_loop_run(loop);

  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(pipeline));
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);

  return 0;
}
