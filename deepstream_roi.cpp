#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <cuda_runtime_api.h>
#include <iostream>
#include <string>

extern "C" {
#include "gstnvdsmeta.h"
}

#define MUXER_OUTPUT_WIDTH 1280
#define MUXER_OUTPUT_HEIGHT 720
#define MUXER_BATCH_TIMEOUT_USEC 40000

// Ajusta esta ruta al PGIE de deepstream-test1 si es distinto en tu Nano
#define PGIE_CONFIG_FILE "/opt/nvidia/deepstream/deepstream-6.0/sources/apps/sample_apps/deepstream-test1/dstest1_pgie_config.txt"

typedef struct {
  gdouble roi_x;
  gdouble roi_y;
  gdouble roi_w;
  gdouble roi_h;
  gdouble max_dwell_time;  // segundos

  gboolean roi_occupied;
  gboolean roi_over_threshold;
  gdouble roi_entry_ts;    // segundos (reloj real)
} RoiConfig;

static RoiConfig roi_cfg;
static std::string report_path;
static FILE *report_fp = nullptr;

// Contadores para el log
static guint g_total_detections = 0;   // personas que entran y salen (EXIT)
static guint g_total_overtime   = 0;   // de esas, cuántas superan el tiempo
static gdouble g_t0 = 0.0;             // tiempo de referencia para mm:ss

typedef enum {
  MODE_VIDEO = 0,  // solo archivo MP4
  MODE_UDP   = 1,  // solo UDP
  MODE_BOTH  = 2   // ambos
} OutputMode;

static OutputMode g_mode = MODE_VIDEO;


/* ------------------------------------------------------------------------ */
/* Manejo del bus (igual que el ejemplo base de GStreamer)                  */
/* ------------------------------------------------------------------------ */

static gboolean
bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
    g_print ("End of stream\n");
    // Resumen de detecciones: total (cuántos con sobretiempo)
    g_print ("Detected: %u (%u)\n", g_total_detections, g_total_overtime);
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
/* Enlace dinámico qtdemux -> h264parse                                     */
/* ------------------------------------------------------------------------ */

static void
cb_new_pad (GstElement *qtdemux, GstPad *pad, gpointer data)
{
  GstElement *h264parser = (GstElement *) data;
  gchar *name = gst_pad_get_name (pad);

  // Solo nos interesa el pad de video
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
/* Pad probe en nvdsosd: lógica ROI + tiempo de permanencia + overlay       */
/* ------------------------------------------------------------------------ */

static GstPadProbeReturn
osd_sink_pad_buffer_probe (GstPad *pad, GstPadProbeInfo *info, gpointer u_data)
{
  GstBuffer *buf = (GstBuffer *) info->data;
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);
  if (!batch_meta)
    return GST_PAD_PROBE_OK;

  // Tiempo actual (segundos, reloj real)
  gdouble now = g_get_real_time () / 1e6;
  
  // Inicializar tiempo de referencia para el log (solo una vez)
  if (g_t0 == 0.0) {
    g_t0 = now;
  }

  for (NvDsMetaList *l_frame = batch_meta->frame_meta_list;
       l_frame != nullptr; l_frame = l_frame->next) {

    NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) l_frame->data;

    gboolean roi_has_obj = FALSE;

    // Recorremos objetos detectados
    for (NvDsMetaList *l_obj = frame_meta->obj_meta_list;
         l_obj != nullptr; l_obj = l_obj->next) {

      NvDsObjectMeta *obj_meta = (NvDsObjectMeta *) l_obj->data;

      // 1) OCULTAR TODO LO QUE NO SEA PERSONA EN EL OSD
      if (obj_meta->class_id != 2) {
        // Quitamos el recuadro y el fondo para que no se vea
        obj_meta->rect_params.border_width = 0;
        obj_meta->rect_params.has_bg_color = 0;
        obj_meta->rect_params.bg_color.alpha = 0.0f;
        obj_meta->rect_params.border_color.alpha = 0.0f;

        // También podemos ocultar el texto (label)
        obj_meta->text_params.display_text = NULL;

        // No usamos este objeto para la ROI
        continue;
      }

      // 2) A partir de aquí SOLO PERSONAS (class_id == 2)

      float x = obj_meta->rect_params.left;
      float y = obj_meta->rect_params.top;
      float w = obj_meta->rect_params.width;
      float h = obj_meta->rect_params.height;

      // Condición: TODO el bounding box dentro de la ROI
      if (x >= roi_cfg.roi_x &&
          x + w <= roi_cfg.roi_x + roi_cfg.roi_w &&
          y >= roi_cfg.roi_y &&
          y + h <= roi_cfg.roi_y + roi_cfg.roi_h) {

        roi_has_obj = TRUE;
        break; // con una persona basta para marcar la ROI ocupada
      }
    }

  if (roi_has_obj && !roi_cfg.roi_occupied) {
    // Nuevo evento de ENTRADA
    roi_cfg.roi_occupied = TRUE;
    roi_cfg.roi_over_threshold = FALSE;
    roi_cfg.roi_entry_ts = now;

    // Tiempo relativo desde el inicio del video
    gdouble rel = now - g_t0;
    int rel_sec = (int)(rel + 0.5);  // redondear a s
    int mm = rel_sec / 60;
    int ss = rel_sec % 60;

    // Log más humano: 0:11 ENTER
    g_print ("%d:%02d ENTER\n", mm, ss);

    // En el CSV guardamos los segundos relativos (no epoch)
    if (report_fp) {
      fprintf (report_fp, "ENTER,%.3f,,\n", rel);
      fflush (report_fp);
    }


  } else if (!roi_has_obj && roi_cfg.roi_occupied) {
    // Evento de SALIDA
    gdouble dwell = now - roi_cfg.roi_entry_ts;
    gboolean overtime = (dwell > roi_cfg.max_dwell_time);

    // Actualizar contadores globales
    g_total_detections++;
    if (overtime) {
      g_total_overtime++;
    }

    // Tiempo relativo desde el inicio del video
    gdouble rel = now - g_t0;
    int rel_sec = (int)(rel + 0.5);  // redondear a s
    int mm = rel_sec / 60;
    int ss = rel_sec % 60;

    int dwell_sec = (int)(dwell + 0.5);

    // Log en el formato pedido:
    // 0:11 person time 4s
    // 0:18 person time 7s alert
    g_print ("%d:%02d person time %ds%s\n",
            mm, ss, dwell_sec, overtime ? " alert" : "");

    // En el CSV guardamos tiempo relativo, no epoch
    if (report_fp) {
      fprintf (report_fp, "EXIT,%.3f,%.3f,%s\n",
              rel, dwell, overtime ? "OVERTIME" : "OK");
      fflush (report_fp);
    }


      roi_cfg.roi_occupied = FALSE;
      roi_cfg.roi_over_threshold = FALSE;
      roi_cfg.roi_entry_ts = 0.0;

    } else if (roi_has_obj && roi_cfg.roi_occupied && !roi_cfg.roi_over_threshold) {
      // Sigue dentro; ver si ya se pasó del tiempo
      gdouble dwell = now - roi_cfg.roi_entry_ts;
      if (dwell > roi_cfg.max_dwell_time) {
        roi_cfg.roi_over_threshold = TRUE;

        gdouble rel = now - g_t0;
        int rel_sec = (int)(rel + 0.5);
        int mm = rel_sec / 60;
        int ss = rel_sec % 60;
        int dwell_sec = (int)(dwell + 0.5);

        g_print ("%d:%02d OVERTIME person time %ds (max %.0fs)\n",
                mm, ss, dwell_sec, roi_cfg.max_dwell_time);

        if (report_fp) {
          fprintf (report_fp, "OVERTIME,%.3f,%.3f,OVERTIME\n",
                  rel, dwell);
          fflush (report_fp);
        }
      }
    }

    // Dibujar rectángulo de la ROI con color según estado
    NvDsDisplayMeta *display_meta =
      nvds_acquire_display_meta_from_pool (batch_meta);
    if (!display_meta)
      continue;

    display_meta->num_rects = 1;
    NvOSD_RectParams *rect_params = &display_meta->rect_params[0];

    rect_params->left   = roi_cfg.roi_x;
    rect_params->top    = roi_cfg.roi_y;
    rect_params->width  = roi_cfg.roi_w;
    rect_params->height = roi_cfg.roi_h;
    rect_params->border_width = 3;

    rect_params->has_bg_color = 1;

    // Colores RGBA (0–1)
    // Estado 1: ROI sin persona (estado normal) → verde suave
    if (!roi_cfg.roi_occupied) {
      rect_params->bg_color.red   = 0.0f;
      rect_params->bg_color.green = 0.6f;
      rect_params->bg_color.blue  = 0.0f;
      rect_params->bg_color.alpha = 0.2f;

    // Estado 2: persona dentro PERO aún dentro del tiempo → verde más marcado
    } else if (roi_cfg.roi_occupied && !roi_cfg.roi_over_threshold) {
      rect_params->bg_color.red   = 0.0f;
      rect_params->bg_color.green = 0.9f;
      rect_params->bg_color.blue  = 0.0f;
      rect_params->bg_color.alpha = 0.3f;

    // Estado 3: persona dentro y SOBRETIEMPO → rojo
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

    nvds_add_display_meta_to_frame (frame_meta, display_meta);
  }

  return GST_PAD_PROBE_OK;
}

/* ------------------------------------------------------------------------ */
/* MAIN                                                                     */
/* ------------------------------------------------------------------------ */

int
main (int argc, char *argv[])
{
  if (argc != 10) {
    g_printerr("Uso: %s <video.mp4> <roi_x> <roi_y> <roi_w> <roi_h> <max_dwell_s> <mode> <reporte.csv> <salida.mp4>\n",
              argv[0]);
    g_printerr("  mode = video | udp | both\n");
    return -1;
  }

  const gchar *input_file = argv[1];
  roi_cfg.roi_x = g_ascii_strtod (argv[2], nullptr);
  roi_cfg.roi_y = g_ascii_strtod (argv[3], nullptr);
  roi_cfg.roi_w = g_ascii_strtod (argv[4], nullptr);
  roi_cfg.roi_h = g_ascii_strtod (argv[5], nullptr);
  roi_cfg.max_dwell_time = g_ascii_strtod (argv[6], nullptr);
  roi_cfg.roi_occupied = FALSE;
  roi_cfg.roi_over_threshold = FALSE;
  roi_cfg.roi_entry_ts = 0.0;

  // Leer modo
  const gchar *mode_str = argv[7];
  if (g_strcmp0 (mode_str, "video") == 0) {
    g_mode = MODE_VIDEO;
  } else if (g_strcmp0 (mode_str, "udp") == 0) {
    g_mode = MODE_UDP;
  } else if (g_strcmp0 (mode_str, "both") == 0) {
    g_mode = MODE_BOTH;
  } else {
    g_printerr ("Modo inválido: %s (use: video | udp | both)\n", mode_str);
    return -1;
  }

  // CSV y nombre del MP4
  report_path = argv[8];
  const gchar *video_out  = argv[9];

  report_fp = fopen (report_path.c_str(), "w");
  if (!report_fp) {
    g_printerr ("No se pudo abrir el archivo de reporte %s para escritura\n",
                report_path.c_str());
    return -1;
  }
  fprintf (report_fp, "event,time,dwell,flag\n");
  fflush (report_fp);

  gst_init (&argc, &argv);

  GMainLoop *loop = g_main_loop_new (nullptr, FALSE);

  GstElement *pipeline    = nullptr;
  GstElement *source      = nullptr;
  GstElement *qtdemux     = nullptr;
  GstElement *h264parser  = nullptr;
  GstElement *decoder     = nullptr;
  GstElement *streammux   = nullptr;
  GstElement *pgie        = nullptr;
  GstElement *nvvidconv   = nullptr;
  GstElement *nvosd       = nullptr;
  GstElement *nvvidconv2  = nullptr;

  // Rama de archivo (MP4)
  GstElement *encoder     = nullptr;
  GstElement *h264parser2 = nullptr;
  GstElement *mux         = nullptr;
  GstElement *sink        = nullptr;

  // Rama UDP
  GstElement *encoder_udp = nullptr;
  GstElement *payloader   = nullptr;
  GstElement *udp_sink    = nullptr;

  // Duplicación (both)
  GstElement *tee         = nullptr;
  GstElement *queue_file  = nullptr;
  GstElement *queue_udp   = nullptr;


  GstBus *bus = nullptr;
  guint bus_watch_id;

  // Crear elementos
  pipeline   = gst_pipeline_new ("deepstream_roi_app");
  source     = gst_element_factory_make ("filesrc", "file-source");
  qtdemux    = gst_element_factory_make ("qtdemux", "qtdemux");
  h264parser = gst_element_factory_make ("h264parse", "h264-parser");
  decoder    = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");
  streammux  = gst_element_factory_make ("nvstreammux", "stream-muxer");
  pgie       = gst_element_factory_make ("nvinfer", "primary-nvinference-engine");
  nvvidconv  = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");
  nvosd      = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");
  nvvidconv2  = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter2");

  // Rama archivo
  encoder     = gst_element_factory_make ("nvv4l2h264enc", "h264-encoder-file");
  h264parser2 = gst_element_factory_make ("h264parse", "h264-parser-2");
  mux         = gst_element_factory_make ("qtmux", "mp4-mux");
  sink        = gst_element_factory_make ("filesink", "file-sink");

  // Rama UDP
  encoder_udp = gst_element_factory_make ("nvv4l2h264enc", "h264-encoder-udp");
  payloader   = gst_element_factory_make ("rtph264pay", "rtp-payloader");
  udp_sink    = gst_element_factory_make ("udpsink", "udp-sink");

  // Duplicación
  tee         = gst_element_factory_make ("tee", "tee");
  queue_file  = gst_element_factory_make ("queue", "queue-file");
  queue_udp   = gst_element_factory_make ("queue", "queue-udp");

  if (!pipeline || !source || !qtdemux || !h264parser || !decoder ||
      !streammux || !pgie || !nvvidconv || !nvosd || !nvvidconv2 ||
      !encoder || !h264parser2 || !mux || !sink ||
      !encoder_udp || !payloader || !udp_sink ||
      !tee || !queue_file || !queue_udp) {
    g_printerr ("No se pudo crear uno o más elementos. Saliendo.\n");
    return -1;
  }

  // Propiedades
  
  g_object_set (G_OBJECT (source), "location", input_file, nullptr);

  g_object_set (G_OBJECT (streammux),
                "batch-size", 1,
                "width", MUXER_OUTPUT_WIDTH,
                "height", MUXER_OUTPUT_HEIGHT,
                "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC,
                nullptr);

  g_object_set (G_OBJECT (pgie),
                "config-file-path", PGIE_CONFIG_FILE,
                nullptr);

  // Archivo de salida MP4
  g_object_set (G_OBJECT (sink), "location", video_out, nullptr);

  // Encoder UDP
  g_object_set (G_OBJECT (encoder_udp),
                "insert-sps-pps", TRUE,
                "bitrate", 4000000,  // 4 Mbps, ajustable
                nullptr);

  // Payloader RTP
  g_object_set (G_OBJECT (payloader),
                "config-interval", 1,
                "pt", 96,
                nullptr);

  // udpsink: AJUSTA 'host' a la IP de la computadora que recibe
  g_object_set (G_OBJECT (udp_sink),
                "host", "127.0.0.1",
                "port", 5000,
                "sync", FALSE,
                "async", FALSE,
                nullptr);

  // Bus
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);


  // Armar pipeline: tronco común
  gst_bin_add_many (GST_BIN (pipeline),
                    source, qtdemux, h264parser, decoder,
                    streammux, pgie, nvvidconv, nvosd,
                    nvvidconv2,
                    nullptr);

  // Enlaces estáticos iniciales
  if (!gst_element_link (source, qtdemux)) {
    g_printerr ("No se pudo enlazar source -> qtdemux\n");
    return -1;
  }

  if (!gst_element_link (h264parser, decoder)) {
    g_printerr ("No se pudo enlazar h264parse -> decoder\n");
    return -1;
  }

  // streammux -> pgie -> nvvidconv -> nvosd -> nvvidconv2
  if (!gst_element_link_many (streammux, pgie, nvvidconv, nvosd,
                              nvvidconv2, nullptr)) {
    g_printerr ("No se pudo enlazar streammux -> ... -> nvvidconv2\n");
    return -1;
  }

  // Enlace dinámico qtdemux -> h264parse
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (cb_new_pad), h264parser);

  // Enlace decoder(src) -> streammux(sink_0) (igual que antes)
  {
    GstPad *sinkpad = gst_element_get_request_pad (streammux, "sink_0");
    if (!sinkpad) {
      g_printerr ("Streammux request sink pad failed\n");
      return -1;
    }

    GstPad *srcpad = gst_element_get_static_pad (decoder, "src");
    if (!srcpad) {
      g_printerr ("Decoder get src pad failed\n");
      gst_object_unref (sinkpad);
      return -1;
    }

    if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
      g_printerr ("No se pudo enlazar decoder -> streammux\n");
      gst_object_unref (sinkpad);
      gst_object_unref (srcpad);
      return -1;
    }

    gst_object_unref (sinkpad);
    gst_object_unref (srcpad);
  }

  // Ahora, según el modo, armamos la COLA de salida

  if (g_mode == MODE_VIDEO) {
    // SOLO archivo MP4
    gst_bin_add_many (GST_BIN (pipeline),
                      encoder, h264parser2, mux, sink, nullptr);

    if (!gst_element_link_many (nvvidconv2, encoder, h264parser2, mux, sink, nullptr)) {
      g_printerr ("No se pudo enlazar nvvidconv2 -> encoder -> mux -> sink (video)\n");
      return -1;
    }

  } else if (g_mode == MODE_UDP) {
    // SOLO UDP
    gst_bin_add_many (GST_BIN (pipeline),
                      encoder_udp, payloader, udp_sink, nullptr);

    if (!gst_element_link_many (nvvidconv2, encoder_udp, payloader, udp_sink, nullptr)) {
      g_printerr ("No se pudo enlazar nvvidconv2 -> encoder_udp -> payloader -> udpsink\n");
      return -1;
    }

  } else if (g_mode == MODE_BOTH) {
    // AMBOS: usar tee para duplicar
    gst_bin_add_many (GST_BIN (pipeline),
                      tee,
                      queue_file, encoder, h264parser2, mux, sink,
                      queue_udp, encoder_udp, payloader, udp_sink,
                      nullptr);

    // nvvidconv2 -> tee
    if (!gst_element_link (nvvidconv2, tee)) {
      g_printerr ("No se pudo enlazar nvvidconv2 -> tee\n");
      return -1;
    }

    // Rama archivo: tee -> queue_file -> encoder -> h264parser2 -> mux -> sink
    if (!gst_element_link_many (queue_file, encoder, h264parser2, mux, sink, nullptr)) {
      g_printerr ("No se pudo enlazar rama archivo local\n");
      return -1;
    }

    // Rama UDP: tee -> queue_udp -> encoder_udp -> payloader -> udp_sink
    if (!gst_element_link_many (queue_udp, encoder_udp, payloader, udp_sink, nullptr)) {
      g_printerr ("No se pudo enlazar rama UDP\n");
      return -1;
    }

    // Conectar pads del tee a las colas
    GstPad *tee_src_pad_file = gst_element_get_request_pad (tee, "src_%u");
    GstPad *queue_sink_pad_file = gst_element_get_static_pad (queue_file, "sink");
    if (gst_pad_link (tee_src_pad_file, queue_sink_pad_file) != GST_PAD_LINK_OK) {
      g_printerr ("No se pudo enlazar tee -> queue_file\n");
      return -1;
    }
    gst_object_unref (tee_src_pad_file);
    gst_object_unref (queue_sink_pad_file);

    GstPad *tee_src_pad_udp = gst_element_get_request_pad (tee, "src_%u");
    GstPad *queue_sink_pad_udp = gst_element_get_static_pad (queue_udp, "sink");
    if (gst_pad_link (tee_src_pad_udp, queue_sink_pad_udp) != GST_PAD_LINK_OK) {
      g_printerr ("No se pudo enlazar tee -> queue_udp\n");
      return -1;
    }
    gst_object_unref (tee_src_pad_udp);
    gst_object_unref (queue_sink_pad_udp);
  }

  // Pad probe en el pad sink de nvdsosd
  {
    GstPad *osd_sink_pad = gst_element_get_static_pad (nvosd, "sink");
    if (!osd_sink_pad) {
      g_printerr ("No se pudo obtener el pad sink de nvdsosd\n");
      return -1;
    }
    gst_pad_add_probe (osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
                       osd_sink_pad_buffer_probe, nullptr, nullptr);
    gst_object_unref (osd_sink_pad);
  }

  g_print ("Usando archivo: %s\n", input_file);
  g_print ("ROI: left: %.0f top: %.0f width: %.0f height: %.0f\n",
          roi_cfg.roi_x, roi_cfg.roi_y, roi_cfg.roi_w, roi_cfg.roi_h);
  g_print ("Max time: %.0fs\n", roi_cfg.max_dwell_time);
  g_print ("Mode: %s\n", mode_str);
  g_print ("Reporte: %s\n", report_path.c_str());

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_print ("Running...\n");
  g_main_loop_run (loop);

  // Limpieza
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  if (report_fp) {
    fclose (report_fp);
    report_fp = nullptr;
  }

  return 0;
}
