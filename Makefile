TARGET = libgstfifosink.so
OBJS = gstfifosink.o
DEPFILES = $(OBJS:%.o=%.d)

CC = gcc
PACKAGES = gstreamer-1.0 gstreamer-base-1.0
CFLAGS = -Wall -fPIC $(shell pkg-config --cflags $(PACKAGES))
CPPFLAGS = -MMD
LDLIBS = $(shell pkg-config --libs $(PACKAGES))

$(TARGET): $(OBJS)
	$(CC) -shared -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET) $(OBJS) $(DEPFILES)

install: $(TARGET)
	cp $(TARGET) ${HOME}/.local/share/gstreamer-1.0/plugins/

%.o: %.c
	gst-indent $<
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

-include $(DEPFILES)
