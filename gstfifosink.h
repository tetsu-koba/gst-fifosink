/*
 * Copyright (C) 2022 Tetsuyuki Kobayashi <tetsu.koba@gmail.com>
 */

#ifndef _GST_FIFOSINK_H_
#define _GST_FIFOSINK_H_

#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS
#define GST_TYPE_FIFOSINK   (gst_fifosink_get_type())
#define GST_FIFOSINK(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FIFOSINK,GstFifoSink))
#define GST_FIFOSINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FIFOSINK,GstFifoSinkClass))
#define GST_IS_FIFOSINK(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FIFOSINK))
#define GST_IS_FIFOSINK_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FIFOSINK))
typedef struct _GstFifoSink GstFifoSink;
typedef struct _GstFifoSinkClass GstFifoSinkClass;

struct _GstFifoSink
{
  GstBaseSink base_fifosink;
  gchar *filename;
  gchar *uri;
  int fd;
  guint64 bytes_written;
};

struct _GstFifoSinkClass
{
  GstBaseSinkClass base_fifosink_class;
};

GType gst_fifosink_get_type (void);

G_END_DECLS
#endif /*_GST_FIFOSINK_H_*/
