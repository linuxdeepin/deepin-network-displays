// SPDX-FileCopyrightText: 2023 - 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "nd-meta-provider.h"
#include "nd-pulseaudio.h"
#include "nd-screencast-portal.h"
#include "nd-sink.h"

#include <glib-object.h>


G_BEGIN_DECLS

#define ND_TYPE_DBUS_SINK (nd_dbus_sink_get_type ())

G_DECLARE_FINAL_TYPE (NdDbusSink, nd_dbus_sink, ND, DBUS_SINK, GObject)

NdDbusSink *nd_dbus_sink_new (NdMetaProvider *meta_provider,
                              NdSink *sink,
                              GDBusConnection *bus);
void nd_sink_dbus_export (NdDbusSink *self);
void nd_sink_dbus_stop_export (NdDbusSink *self);
gboolean nd_dbus_sink_equal_sink (NdDbusSink *self,
                                  NdSink *sink);
gchar *nd_sink_dbus_get_hw_address (NdDbusSink *self);
gchar *nd_sink_dbus_get_name (NdDbusSink *self);

typedef void (*nd_handle_cancel_cb_t) (void *user_data);
void nd_sink_dbus_set_cancel_cb (NdDbusSink *self, nd_handle_cancel_cb_t cb, void *user_data);
NdSinkState nd_sink_dbus_get_status (NdDbusSink *self);
G_END_DECLS
