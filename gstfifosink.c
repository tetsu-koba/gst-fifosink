/*
 * Copyright (C) 2022 Tetsuyuki Kobayashi <tetsu.koba@gmail.com>
 */
/**
 * SECTION:element-gstfifosink
 *
 * The fifosink element writes to fifo(named pipe).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * mkfifo f
 * cat f > /dev/null &
 * gst-launch-1.0 -v videotestsrc ! 'video/x-raw, format=(string)I420' ! fifosink location=f
 * ]|
 * The fifosink element writes to fifo(named pipe) using vmsplice(2).
 * It is supported only on Linux.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <stdio.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gstfifosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_fifosink_debug_category);
#define GST_CAT_DEFAULT gst_fifosink_debug_category

/* prototypes */


static void gst_fifosink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_fifosink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_fifosink_dispose (GObject * object);
static gboolean gst_fifosink_start (GstBaseSink * sink);
static gboolean gst_fifosink_stop (GstBaseSink * sink);
static gboolean gst_fifosink_query (GstBaseSink * sink, GstQuery * query);
static gboolean gst_fifosink_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn gst_fifosink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static GstFlowReturn gst_fifosink_render_list (GstBaseSink * sink,
    GstBufferList * buffer_list);

enum
{
  PROP_0,
  PROP_LOCATION
};

/* pad templates */

static GstStaticPadTemplate gst_fifosink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstFifoSink, gst_fifosink, GST_TYPE_BASE_SINK,
    GST_DEBUG_CATEGORY_INIT (gst_fifosink_debug_category, "fifosink", 0,
        "debug category for fifosink element"));

static void
gst_fifosink_class_init (GstFifoSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_fifosink_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Fifo Sink", "Sink/Fifo", "Write data to a fifo(named pipe)",
      "Tetsuyuki Kobayashi <tetsu.koba@gmail.com>");

  gobject_class->set_property = gst_fifosink_set_property;
  gobject_class->get_property = gst_fifosink_get_property;
  gobject_class->dispose = gst_fifosink_dispose;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the fifo(named pipe) to write", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_fifosink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_fifosink_stop);
  base_sink_class->query = GST_DEBUG_FUNCPTR (gst_fifosink_query);
  base_sink_class->event = GST_DEBUG_FUNCPTR (gst_fifosink_event);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_fifosink_render);
  base_sink_class->render_list = GST_DEBUG_FUNCPTR (gst_fifosink_render_list);
}

static void
gst_fifosink_init (GstFifoSink * fifosink)
{
  fifosink->fd = -1;
  fifosink->filename = NULL;
  fifosink->uri = NULL;
  fifosink->bytes_written = 0;

  gst_base_sink_set_sync (GST_BASE_SINK (fifosink), FALSE);
}

static gboolean
gst_file_sink_set_location (GstFifoSink * sink, const gchar * location,
    GError ** error)
{
  if (sink->fd > 2) {
    g_warning ("Changing the `location' property on filesink when a file is "
        "open is not supported.");
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_STATE,
        "Changing the 'location' property on filesink when a file is "
        "open is not supported");
    return FALSE;
  }

  g_free (sink->filename);
  g_free (sink->uri);
  if (location != NULL) {
    /* we store the filename as we received it from the application. On Windows
     * this should be in UTF8 */
    sink->filename = g_strdup (location);
    sink->uri = gst_filename_to_uri (location, NULL);
    GST_INFO_OBJECT (sink, "filename : %s", sink->filename);
    GST_INFO_OBJECT (sink, "uri      : %s", sink->uri);
  } else {
    sink->filename = NULL;
    sink->uri = NULL;
  }

  return TRUE;
}

void
gst_fifosink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFifoSink *fifosink = GST_FIFOSINK (object);

  GST_DEBUG_OBJECT (fifosink, "set_property");

  switch (property_id) {
    case PROP_LOCATION:
      gst_file_sink_set_location (fifosink, g_value_get_string (value), NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_fifosink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstFifoSink *fifosink = GST_FIFOSINK (object);

  GST_DEBUG_OBJECT (fifosink, "get_property");

  switch (property_id) {
    case PROP_LOCATION:
      g_value_set_string (value, fifosink->filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_fifosink_dispose (GObject * object)
{
  GstFifoSink *fifosink = GST_FIFOSINK (object);

  GST_DEBUG_OBJECT (fifosink, "dispose");

  /* clean up as possible.  may be called multiple times */
  g_free (fifosink->uri);
  fifosink->uri = NULL;
  g_free (fifosink->filename);
  fifosink->filename = NULL;

  G_OBJECT_CLASS (gst_fifosink_parent_class)->dispose (object);
}

static gboolean
gst_fifosink_open_file (GstFifoSink * sink)
{
  if (sink->filename == NULL || sink->filename[0] == '\0') {
    GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
        (("No file name specified for writing.")), (NULL));
    return FALSE;
  }

  int fd = open (sink->filename, O_WRONLY);
  if (fd < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        (("Could not open file \"%s\" for writing."), sink->filename),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
  sink->fd = fd;

  struct stat st;
  if (!fstat (sink->fd, &st) && S_ISFIFO (st.st_mode)) {
    int pipe_size = 1024 * 1024;
    FILE *f = fopen ("/proc/sys/fs/pipe-max-size", "r");
    if (f != NULL) {
      char buf[128];
      if (fgets (buf, sizeof (buf), f)) {
        pipe_size = atoi (buf);
      }
    }
    fcntl (sink->fd, F_SETPIPE_SZ, pipe_size);
  } else {
    GST_ERROR_OBJECT (sink, "%s is not fifo.", sink->filename);
    return FALSE;
  }
  return TRUE;
}

static void
gst_fifosink_close_file (GstFifoSink * sink)
{
  if (sink->fd > 0) {
    if (close (sink->fd) != 0) {
      GST_ELEMENT_ERROR (sink, RESOURCE, CLOSE,
          (("Error closing file \"%s\"."), sink->filename), GST_ERROR_SYSTEM);
    }

    GST_DEBUG_OBJECT (sink, "closed file");
    sink->fd = -1;
  }
}

/* start and stop processing, ideal for opening/closing the resource */
static gboolean
gst_fifosink_start (GstBaseSink * sink)
{
  GstFifoSink *fifosink = GST_FIFOSINK (sink);

  GST_DEBUG_OBJECT (fifosink, "start");

  fifosink->bytes_written = 0;
  return gst_fifosink_open_file (fifosink);
}

static gboolean
gst_fifosink_stop (GstBaseSink * sink)
{
  GstFifoSink *fifosink = GST_FIFOSINK (sink);

  GST_DEBUG_OBJECT (fifosink, "stop");

  gst_fifosink_close_file (fifosink);
  return TRUE;
}

/* notify subclass of query */
static gboolean
gst_fifosink_query (GstBaseSink * sink, GstQuery * query)
{
  gboolean res;
  GstFifoSink *fifosink = GST_FIFOSINK (sink);

  GST_DEBUG_OBJECT (fifosink, "query: GST_QUERY_TYPE (query)=%d",
      GST_QUERY_TYPE (query));

  switch (GST_QUERY_TYPE (query)) {
    default:
      res =
          GST_BASE_SINK_CLASS (gst_fifosink_parent_class)->query (sink, query);
      break;
  }
  return res;
}

/* notify subclass of event */
static gboolean
gst_fifosink_event (GstBaseSink * sink, GstEvent * event)
{
  GstFifoSink *fifosink = GST_FIFOSINK (sink);

  GST_DEBUG_OBJECT (fifosink, "event");

  return TRUE;
}

static gsize
fill_vectors (struct iovec *vecs, GstMapInfo * maps, guint n, GstBuffer * buf)
{
  GstMemory *mem;
  gsize size = 0;
  guint i;

  g_assert (gst_buffer_n_memory (buf) == n);

  for (i = 0; i < n; ++i) {
    mem = gst_buffer_peek_memory (buf, i);
    if (gst_memory_map (mem, &maps[i], GST_MAP_READ)) {
      vecs[i].iov_base = maps[i].data;
      vecs[i].iov_len = maps[i].size;
    } else {
      GST_WARNING ("Failed to map memory %p for reading", mem);
      vecs[i].iov_base = (void *) "";
      vecs[i].iov_len = 0;
    }
    size += vecs[i].iov_len;
  }

  return size;
}

static GstFlowReturn
gst_writev_buffers (GstObject * sink, gint fd, GstPoll * fdset,
    GstBuffer ** buffers, guint num_buffers, guint8 * mem_nums,
    guint total_mem_num, guint64 * bytes_written)
{
  struct iovec *vecs;
  GstMapInfo *map_infos;
  GstFlowReturn flow_ret;
  gsize size = 0;
  guint i, j;

  GST_LOG_OBJECT (sink, "%u buffers, %u memories", num_buffers, total_mem_num);

  vecs = g_newa (struct iovec, total_mem_num);
  map_infos = g_newa (GstMapInfo, total_mem_num);

  /* populate output vectors */
  for (i = 0, j = 0; i < num_buffers; ++i) {
    size += fill_vectors (&vecs[j], &map_infos[j], mem_nums[i], buffers[i]);
    j += mem_nums[i];
  }

  /* now write it all out! */
  {
    gssize ret, left;
    guint n_vecs = total_mem_num;

    left = size;

    do {
      int n = (n_vecs > IOV_MAX) ? IOV_MAX : n_vecs;
      do {
        ret = vmsplice (fd, vecs, n, 0);
      } while (ret < 0 && errno == EINTR);

      if (ret > 0) {
        if (bytes_written)
          *bytes_written += ret;
      }

      if (ret == left)
        break;

      if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /* do nothing, try again */
      } else if (ret < 0) {
        {
          switch (errno) {
            case ENOSPC:
              GST_ELEMENT_ERROR (sink, RESOURCE, NO_SPACE_LEFT, (NULL), (NULL));
              break;
            default:{
              GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
                  ("Error while writing to file descriptor %d: %s",
                      fd, g_strerror (errno)));
            }
          }
          flow_ret = GST_FLOW_ERROR;
          for (i = 0; i < total_mem_num; ++i)
            gst_memory_unmap (map_infos[i].memory, &map_infos[i]);

          return flow_ret;
        }
      } else if (ret < left) {
        /* skip vectors that have been written in full */
        while ((size_t) ret >= vecs[0].iov_len) {
          ret -= vecs[0].iov_len;
          left -= vecs[0].iov_len;
          ++vecs;
          --n_vecs;
        }
        g_assert (n_vecs > 0);
        /* skip partially written vector data */
        if (ret > 0) {
          vecs[0].iov_len -= ret;
          vecs[0].iov_base = ((guint8 *) vecs[0].iov_base) + ret;
          left -= ret;
        }
      }
    }
    while (left > 0);
  }

  flow_ret = GST_FLOW_OK;

  for (i = 0; i < total_mem_num; ++i)
    gst_memory_unmap (map_infos[i].memory, &map_infos[i]);

  return flow_ret;
}

static GstFlowReturn
gst_fifosink_render_buffers (GstFifoSink * sink, GstBuffer ** buffers,
    guint num_buffers, guint8 * mem_nums, guint total_mems)
{
  GstFlowReturn ret;
  guint64 bytes_written = 0;

  ret = gst_writev_buffers (GST_OBJECT_CAST (sink), sink->fd, NULL,
      buffers, num_buffers, mem_nums, total_mems, &bytes_written);

  sink->bytes_written += bytes_written;

  return ret;
}

static GstFlowReturn
gst_fifosink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstFlowReturn flow;
  GstFifoSink *sink;
  guint8 n_mem = gst_buffer_n_memory (buffer);

  sink = GST_FIFOSINK (bsink);
  GST_DEBUG_OBJECT (sink,
      "render:pts=%ld,dts=%ld,duration=%ld,nmem=%d",
      buffer->pts, buffer->dts, buffer->duration, n_mem);

  if (n_mem > 0)
    flow = gst_fifosink_render_buffers (sink, &buffer, 1, &n_mem, n_mem);
  else
    flow = GST_FLOW_OK;

  return flow;
}

/* Render a BufferList */
static GstFlowReturn
gst_fifosink_render_list (GstBaseSink * bsink, GstBufferList * buffer_list)
{
  GstFlowReturn flow;
  GstBuffer **buffers;
  GstFifoSink *sink;
  guint8 *mem_nums;
  guint total_mems;
  guint i, num_buffers;

  sink = GST_FIFOSINK (bsink);
  GST_DEBUG_OBJECT (sink, "render_list");

  num_buffers = gst_buffer_list_length (buffer_list);
  if (num_buffers == 0) {
    GST_LOG_OBJECT (sink, "empty buffer list");
    return GST_FLOW_OK;
  }

  /* extract buffers from list and count memories */
  buffers = g_newa (GstBuffer *, num_buffers);
  mem_nums = g_newa (guint8, num_buffers);
  for (i = 0, total_mems = 0; i < num_buffers; ++i) {
    buffers[i] = gst_buffer_list_get (buffer_list, i);
    mem_nums[i] = gst_buffer_n_memory (buffers[i]);
    total_mems += mem_nums[i];
  }

  flow =
      gst_fifosink_render_buffers (sink, buffers, num_buffers, mem_nums,
      total_mems);

  return flow;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "fifosink", GST_RANK_NONE,
      GST_TYPE_FIFOSINK);
}

#ifndef VERSION
#define VERSION "0.0.1"
#endif
#ifndef PACKAGE
#define PACKAGE "-"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "-"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "https://github.com/tetsu-koba/gst-fifosink"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    fifosink,
    "Write to fifo(named pipe) using vmsplice(2)",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
