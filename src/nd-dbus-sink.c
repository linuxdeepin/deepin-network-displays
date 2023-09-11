// SPDX-FileCopyrightText: 2023 - 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "nd-dbus-sink.h"

#include "nd-dbus-manager.h"

#include <gio/gio.h>

struct _NdDbusSink
{
  GObject parent_instance;
  GDBusNodeInfo *network_display_sink_info;
  guint registration_id;
  GCancellable *cancellable;
  gboolean is_portal_init_running;
  GDBusProxy *notify_proxy;
  GDBusProxy *display_proxy;
  // 需要在初始化时完成以下变量的初始化
  GDBusConnection *bus;
  NdMetaProvider *provider;
  NdScreencastPortal *portal;
  NdPulseaudio *pulse;
  gboolean x11;
  NdSink *sink; // meta_sink
  gchar *dbus_path;
  // 初始化sink时完成以下dbus属性初始化
  gchar *name;
  NdSinkState status;
  guint32 strength;
  gchar *hw_address;
  // 目前nm的机制,peer的属性不会改变,只有创建新的peer会更新属性

  nd_handle_cancel_cb_t nd_handle_cancel_cb;
  void *nd_handle_cancel_cb_user_data;

  guint unload_pa_module_source_id;
};

enum
{
  PROP_PROVIDER = 1,
  PROP_PORTAL,
  PROP_PULSE,
  PROP_X11,
  PROP_SINK,
  PROP_BUS,
  PROP_LAST,
};

#define DEEPIN_ND_SINK_DBUS_PATH "/com/deepin/Cooperation/NetworkDisplay"
#define DEEPIN_ND_SINK_DBUS_INTERFACE "com.deepin.Cooperation.NetworkDisplay.Sink"

G_DEFINE_TYPE (NdDbusSink, nd_dbus_sink, G_TYPE_OBJECT)

static GParamSpec *props[PROP_LAST] = {
  NULL,
};

static GstElement *sink_create_video_source_cb (NdDbusSink *self,
                                                NdSink *sink);
static GstElement *sink_create_audio_source_cb (NdDbusSink *self,
                                                NdSink *sink);
static void sink_notify_state_cb (NdDbusSink *self,
                                  GParamSpec *pspec,
                                  NdSink *sink);
static void nd_dbus_screencast_portal_init_async_cb (GObject *source_object,
                                                     GAsyncResult *res,
                                                     gpointer user_data);
static void
handle_sink_method_call (GDBusConnection *connection,
                         const gchar *sender,
                         const gchar *object_path,
                         const gchar *interface_name,
                         const gchar *method_name,
                         GVariant *parameters,
                         GDBusMethodInvocation *invocation,
                         gpointer user_data);
static GVariant *handle_sink_get_property (GDBusConnection *connection,
                                           const gchar *sender,
                                           const gchar *object_path,
                                           const gchar *interface_name,
                                           const gchar *property_name,
                                           GError **error,
                                           gpointer user_data);

static void emit_nd_manager_value_changed (const NdDbusSink *self,
                                           const gchar *property_name,
                                           GVariant *property_value);
static void set_prop_status (NdDbusSink *self, gint32 status);
static void send_notify (NdDbusSink *self, const gchar *body);
static void handle_cancel (NdDbusSink *self);
static void set_prop_name (NdDbusSink *self, const gchar *name);
static void init_pulse_async (NdDbusSink *self);

static NdSink *stream_sink = NULL; // 目前一定是 p2p sink

NdDbusSink *
nd_dbus_sink_new (NdMetaProvider *provider,
                  NdSink *sink,
                  GDBusConnection *bus)
{
  return g_object_new (ND_TYPE_DBUS_SINK,
                       "provider", provider,
                       "sink", sink,
                       "bus", bus,
                       NULL);
}

gchar *
nd_sink_dbus_get_hw_address (NdDbusSink *self)
{
  return g_strdup (self->hw_address);
}

gchar *
nd_sink_dbus_get_name (NdDbusSink *self)
{
  return g_strdup (self->name);
}

void
nd_sink_dbus_set_cancel_cb (NdDbusSink *self, nd_handle_cancel_cb_t cb, void *user_data)
{
  self->nd_handle_cancel_cb = cb;
  self->nd_handle_cancel_cb_user_data = user_data;
}

NdSinkState
nd_sink_dbus_get_status (NdDbusSink *self)
{
  return self->status;
}

static void
nd_dbus_sink_finalize (GObject *object)
{
  D_ND_DEBUG ("dbus sink finalize");
  G_OBJECT_CLASS (nd_dbus_sink_parent_class)->finalize (object);

  NdDbusSink *self = ND_DBUS_SINK (object);
  g_dbus_connection_unregister_object (self->bus, self->registration_id);
  g_dbus_node_info_unref (self->network_display_sink_info);

  g_clear_object (&self->provider);
  g_clear_object (&self->portal);
  g_clear_object (&self->pulse);
  g_clear_object (&self->sink);
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->display_proxy);
  g_clear_object (&self->notify_proxy);
}

static void
nd_dbus_sink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  // 暂时没有get需要
  //  NdDbusSink *sink = ND_DBUS_SINK (object);
  switch (prop_id)
    {
    case PROP_PROVIDER:
      break;
    case PROP_PORTAL:
      break;
    case PROP_SINK:
      break;
    case PROP_X11:
      break;
    case PROP_PULSE:
      break;
    case PROP_BUS:
      break;
    }
}

static void
nd_dbus_sink_handle_property_changed (NdDbusSink *self, GParamSpec *pspec, NdSink *sink)
{
  g_autofree gchar *sink_name = NULL;
  g_object_get (self->sink, "display-name", &sink_name, NULL);
  if (sink_name)
    set_prop_name (self, g_strdup (sink_name));
}

static void
nd_dbus_sink_prop_init (NdDbusSink *self)
{
  if (self->sink)
    {
      // 同时初始化必要属性
      g_autofree gchar *sink_hw_address = NULL;
      g_object_get (self->sink, "hw-address", &sink_hw_address, NULL);
      self->hw_address = g_strdup (sink_hw_address);
      g_autofree gchar *sink_name = NULL;
      g_object_get (self->sink, "display-name", &sink_name, NULL);
      self->name = g_strdup (sink_name);
      gint sink_strength = 0;
      g_object_get (self->sink, "strength", &sink_strength, NULL);
      self->strength = sink_strength;
      NdSinkState state = ND_SINK_STATE_DISCONNECTED;
      g_object_get (self->sink, "state", &state, NULL);
      self->status = state;

      g_signal_connect_object (self->sink,
                               "notify",
                               (GCallback) nd_dbus_sink_handle_property_changed,
                               self,
                               G_CONNECT_SWAPPED);
    }
}

// g_object_set无法设置以下属性,只能在构造函数中初始化
static void
nd_dbus_sink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  NdDbusSink *self = ND_DBUS_SINK (object);
  NdMetaProvider *meta_provider = NULL;
  NdPulseaudio *pulse = NULL;
  NdScreencastPortal *portal = NULL;
  NdSink *sink = NULL;
  GDBusConnection *bus = NULL;
  switch (prop_id)
    {
    case PROP_PROVIDER:
      meta_provider = g_value_get_object (value);
      if (meta_provider)
        {
          self->provider = g_object_ref (meta_provider);
        }
      break;
    case PROP_PULSE:
      pulse = g_value_get_object (value);
      if (pulse)
        {
          self->pulse = g_object_ref (pulse);
        }
      break;
    case PROP_SINK:
      sink = g_value_get_object (value);
      if (sink)
        {
          self->sink = g_object_ref (sink);
          nd_dbus_sink_prop_init (self);
        }
      break;
    case PROP_X11:
      self->x11 = g_value_get_boolean (value);
      break;
    case PROP_PORTAL:
      portal = g_value_get_object (value);
      if (portal)
        {
          self->portal = g_object_ref (portal);
        }
      break;
    case PROP_BUS:
      bus = g_value_get_object (value);
      if (bus)
        {
          self->bus = g_object_ref (bus);
        }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
nd_dbus_sink_class_init (NdDbusSinkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = nd_dbus_sink_get_property; // 应该不需要get
  object_class->set_property = nd_dbus_sink_set_property;

  object_class->finalize = nd_dbus_sink_finalize;
  // G_PARAM_CONSTRUCT_ONLY：只有在构造函数中才能设置以下属性
  props[PROP_PROVIDER] = g_param_spec_object (
      "provider",
      "The sink provider",
      "The sink provider (usually a MetaProvider) that finds the available sinks.",
      ND_TYPE_PROVIDER,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_PULSE] = g_param_spec_object (
      "pulse",
      "pulseaudio",
      "pulseaudio play audio",
      ND_TYPE_PULSEAUDIO,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),

  props[PROP_X11] = g_param_spec_boolean (
      "x11",
      "x11",
      "Whether to use the x11 protocol",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_SINK] = g_param_spec_object (
      "sink",
      "NdSink",
      "NdSink",
      ND_TYPE_SINK,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_BUS] = g_param_spec_object (
      "bus",
      "dbus",
      "dbus connection",
      G_TYPE_DBUS_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  // portal初始化是异步的，可以设置属性
  props[PROP_PORTAL] = g_param_spec_object (
      "portal",
      "xdg desktop portal",
      "xdg desktop portal that provide screencast",
      ND_TYPE_SCREENCAST_PORTAL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),

  g_object_class_install_properties (object_class, PROP_LAST, props);
}

static void
nd_dbus_sink_init (NdDbusSink *self)
{
  const gchar *network_display_sink_interface = "<node>"
                                                "  <interface name='com.deepin.Cooperation.NetworkDisplay.Sink'>"
                                                "    <property name='Status' type='u' access='read'/>"
                                                "    <property name='Name' type='s' access='read'/>"
                                                "    <property name='Strength' type='u' access='read'/>"
                                                "    <property name='HwAddress' type='s' access='read'/>"
                                                "    <method name='Connect'></method>"
                                                "    <method name='Cancel'></method>"
                                                "  </interface>"
                                                "</node>";
  self->network_display_sink_info = g_dbus_node_info_new_for_xml (network_display_sink_interface, NULL);
  g_assert (self->network_display_sink_info != NULL);

  g_autoptr (GError) error = NULL;
  self->notify_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                      G_DBUS_PROXY_FLAGS_NONE,
                                                      NULL,
                                                      "org.freedesktop.Notifications",
                                                      "/org/freedesktop/Notifications",
                                                      "com.deepin.dde.Notification",
                                                      NULL,
                                                      &error);
  if (!self->notify_proxy)
    {
      D_ND_WARNING ("Unable to connect Notifications: %s\n", error->message);
    }

  self->display_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       NULL,
                                                       "com.deepin.daemon.Display",
                                                       "/com/deepin/daemon/Display",
                                                       "com.deepin.daemon.Display",
                                                       NULL,
                                                       &error);
  if (!self->display_proxy)
    {
      D_ND_WARNING ("Unable to connect display: %s\n", error->message);
    }
}

static void
nd_sink_start_stream_real (NdDbusSink *self)
{
  if (!self->portal && !self->x11)
    {
      D_ND_WARNING ("Cannot start streaming right now as we don't have a portal!");
      return;
    }

  // 正常返回的是P2P sink
  stream_sink = g_object_ref (nd_sink_start_stream (self->sink));

  // 返回的stream_sink是p2p sink
  if (!stream_sink)
    {
      D_ND_WARNING ("NdWindow: Could not start streaming!");
      return;
    }
  g_signal_connect_object (stream_sink,
                           "create-source",
                           (GCallback) sink_create_video_source_cb,
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (stream_sink,
                           "create-audio-source",
                           (GCallback) sink_create_audio_source_cb,
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (stream_sink,
                           "notify::state",
                           (GCallback) sink_notify_state_cb,
                           self,
                           G_CONNECT_SWAPPED);
  /* We might have moved into the error state in the meantime. */
  sink_notify_state_cb (self, NULL, stream_sink);
  // TODO 使用 g_object_bind_property 进行单项数据绑定或者监听属性变化
  /*
  g_ptr_array_add (self->sink_property_bindings,
                  g_object_ref (g_object_bind_property (self->stream_sink,
                                                      "missing-video-codec",
                                                      self->stream_video_install,
                                                      "codecs",
                                                      G_BINDING_SYNC_CREATE)));
                                                      */

  // 连接之后可以关闭扫描,但是manager状态不会变化
  g_object_set (self->provider, "discover", FALSE, NULL);
}

static void
nd_dbus_screencast_portal_init_async_cb (GObject *source_object,
                                         GAsyncResult *res,
                                         gpointer user_data)
{
  NdDbusSink *self = NULL;
  g_autoptr (GError) error = NULL;
  if (!g_async_initable_init_finish (G_ASYNC_INITABLE (source_object), res, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          D_ND_WARNING ("Error initializing screencast portal: %s", error->message);

          self = ND_DBUS_SINK (user_data);
          self->is_portal_init_running = FALSE;
          /* Unknown method means the portal does not exist, give a slightly
           * more specific warning then.
           */
          if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD))
            {
              D_ND_WARNING ("Screencast portal is unavailable! It is required to select the monitor to stream!");
            }
          D_ND_WARNING ("XDG_SESSION_TYPE is: %s", g_getenv ("XDG_SESSION_TYPE"));
          if (error->code == G_IO_ERROR_FAILED && (g_strcmp0 (g_getenv ("XDG_SESSION_TYPE"), "wayland") == 0))
            {
              // 用户取消share的情况下
              D_ND_WARNING ("portal init failed in wayland, need abort require");
              // 应该还需要修改状态，用于通知前端
              handle_cancel (self);
              g_object_unref (source_object);
              return;
            }
          D_ND_WARNING ("Falling back to X11! You need to fix your setup to avoid issues (XDG Portals and/or mutter screencast support)!");
          self->x11 = TRUE;
          init_pulse_async(self);
        }
      g_object_unref (source_object);
      return;
    }

  self = ND_DBUS_SINK (user_data);
  self->x11 = FALSE;
  self->is_portal_init_running = FALSE;
  self->portal = ND_SCREENCAST_PORTAL (source_object);
  init_pulse_async(self);
}

// from find_sink_list_row_activated_cb 连接设备
static void
init_portal_async (NdDbusSink *self)
{
  NdScreencastPortal *portal = NULL;
  self->cancellable = g_cancellable_new ();
  portal = nd_screencast_portal_new ();
  self->portal = portal;
  g_async_initable_init_async (G_ASYNC_INITABLE (portal),
                               G_PRIORITY_LOW,
                               self->cancellable,
                               nd_dbus_screencast_portal_init_async_cb,
                               self);
}

static void
nd_dbus_pulseaudio_init_async_cb (GObject *source_object,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
  NdDbusSink *self = NULL;
  g_autoptr (GError) error = NULL;
  if (!g_async_initable_init_finish (G_ASYNC_INITABLE (source_object), res, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        D_ND_WARNING ("Error initializing pulse audio sink: %s", error->message);
      g_object_unref (source_object);
      return;
    }

  self = ND_DBUS_SINK (user_data);
  self->pulse = ND_PULSEAUDIO (source_object);
  D_ND_INFO ("Nd pulseaudio module init succeed, start stream real");
  nd_sink_start_stream_real (self);
}

static void
init_pulse_async (NdDbusSink *self)
{
  NdPulseaudio *pulse = NULL;
  self->cancellable = g_cancellable_new ();
  pulse = nd_pulseaudio_new ();
  g_async_initable_init_async (G_ASYNC_INITABLE (pulse),
                               G_PRIORITY_LOW,
                               self->cancellable,
                               nd_dbus_pulseaudio_init_async_cb,
                               self);
}

static void
stop_stream (NdDbusSink *self)
{
  if (!stream_sink)
    return;
  nd_sink_stop_stream (stream_sink);
  // 关闭串流之后,重新开启扫描
  g_object_set (self->provider, "discover", TRUE, NULL);
}

static gboolean
sink_real_cancel (gpointer user_data)
{
  NdDbusSink *self = ND_DBUS_SINK (user_data);
  stop_stream (self);
  if (stream_sink)
    {
      g_signal_handlers_disconnect_by_data (stream_sink, self);
      g_object_unref (stream_sink);
      stream_sink = NULL;
    }
  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);
  if (self->portal)
    g_object_unref (self->portal);
  if (self->nd_handle_cancel_cb)
    self->nd_handle_cancel_cb (self->nd_handle_cancel_cb_user_data);
  return G_SOURCE_REMOVE;
}

// pa模块卸载完成后继续其他退出操作
static void
nd_pulseaudio_unload_module_cb (pa_context *c, int success, void *userdata)
{
  NdDbusSink *self = ND_DBUS_SINK (userdata);
  D_ND_INFO ("unload pa module end, continue cancel");
  sink_real_cancel (self);
  g_source_remove (self->unload_pa_module_source_id);
  self->unload_pa_module_source_id = 0;
}

static const guint quit_timeout = 25;

static void
handle_cancel (NdDbusSink *self)
{
  nd_pulseaudio_unload_module (self->pulse, nd_pulseaudio_unload_module_cb, self);
  self->unload_pa_module_source_id = g_timeout_add_seconds (quit_timeout, sink_real_cancel, self);
}

static GstElement *
sink_create_video_source_cb (NdDbusSink *self, NdSink *sink)
{
  GstBin *bin = NULL;
  GstElement *res = NULL;
  GstElement *dst = NULL;
  GstElement *src = NULL;

  bin = GST_BIN (gst_bin_new ("screencast source bin"));
  D_ND_DEBUG ("use x11: %d", self->x11);
  if (self->x11)
    {
      // x11下需要处理多屏场景，先判断屏幕数量，再获取主屏的坐标和尺寸，计算后设置给ximagesrc，达到只获取主屏显示内容的效果。
      guint screen_size = 0;
      gint16 start_x = 0;
      gint16 start_y = 0;
      guint16 width = 0;
      guint16 height = 0;
      guint end_x = 0;
      guint end_y = 0;
      if (self->display_proxy)
        {
          g_autoptr (GVariant) property_value = g_dbus_proxy_get_cached_property (self->display_proxy, "Monitors");
          if (property_value)
            {
              g_autoptr (GVariantIter) monitor_iter = NULL;
              g_variant_get (property_value, "ao", &monitor_iter);
              while (g_variant_iter_loop (monitor_iter, "&o", NULL))
                {
                  screen_size++;
                }
            }

          if (screen_size >= 2)
            {
              property_value = g_dbus_proxy_get_cached_property (self->display_proxy, "PrimaryRect");
              if (property_value)
                {
                  g_variant_get (property_value, "(nnqq)", &start_x, &start_y, &width, &height);
                  D_ND_INFO ("Primary rect: %d %d %d %d", start_x, start_y, width, height);
                  end_x = start_x + width - 1;
                  end_y = start_y + height - 1;
                }
            }
        }

      src = gst_element_factory_make ("ximagesrc", "X11 screencast source");

      g_object_set (src,
                    "startx", start_x,
                    "starty", start_y,
                    "endx", end_x,
                    "endy", end_y,
                    NULL);
    }
  else
    src = nd_screencast_portal_get_source (self->portal);

  if (!src)
    g_error ("Error creating video source element, likely a missing dependency!");

  gst_bin_add (bin, src);

  dst = gst_element_factory_make ("intervideosink", "inter video sink");
  if (!dst)
    g_error ("Error creating intervideosink, missing dependency!");
  g_object_set (dst,
                "channel", "nd-inter-video",
                "max-lateness", (gint64) -1,
                "sync", FALSE,
                NULL);
  gst_bin_add (bin, dst);

  gst_element_link_many (src, dst, NULL);

  res = gst_element_factory_make ("intervideosrc", "screencastsrc");
  g_object_set (res,
                "do-timestamp", FALSE,
                "timeout", (guint64) G_MAXUINT64,
                "channel", "nd-inter-video",
                NULL);

  gst_bin_add (bin, res);

  gst_element_add_pad (GST_ELEMENT (bin),
                       gst_ghost_pad_new ("src", gst_element_get_static_pad (res, "src")));

  g_object_ref_sink (bin);
  return GST_ELEMENT (bin);
}

static GstElement *
sink_create_audio_source_cb (NdDbusSink *self, NdSink *sink)
{
  GstElement *res = NULL;

  if (!self->pulse)
    return NULL;

  res = nd_pulseaudio_get_source (self->pulse);

  return g_object_ref_sink (res);
}

static void
sink_notify_state_cb (NdDbusSink *self, GParamSpec *pspec, NdSink *sink)
{
  NdSinkState state = ND_SINK_STATE_DISCONNECTED;

  g_object_get (sink, "state", &state, NULL);
  D_ND_INFO ("Got state change notification from streaming sink to state %s", g_enum_to_string (ND_TYPE_SINK_STATE, state));
  set_prop_status (self, state);
  switch (state)
    {
    case ND_SINK_STATE_DISCONNECTED:
    case ND_SINK_STATE_ERROR:
      handle_cancel (self);
      send_notify (self, "error");
      break;
    case ND_SINK_STATE_ENSURE_FIREWALL:
      break;
    case ND_SINK_STATE_WAIT_P2P:
      break;
    case ND_SINK_STATE_WAIT_SOCKET:
      break;
    case ND_SINK_STATE_WAIT_STREAMING:
      break;
    case ND_SINK_STATE_STREAMING:
      send_notify (self, "streaming");
      break;
    default:
      break;
    }
}

gboolean
nd_dbus_sink_equal_sink (NdDbusSink *self, NdSink *sink)
{
  // 通过self的sink和sink的mac地址对比，是否是同一个设备
  //  g_autofree gchar *sink_hw_address = NULL;
  //  g_object_get (sink, "hw-address", &sink_hw_address, NULL);
  //  return g_str_equal (self->hw_address, sink_hw_address);
  // 通过内存地址判断meta_sink和sink是否为同一个
  return self->sink == sink;
}

// 以下代码为dbus相关
void
nd_sink_dbus_export (NdDbusSink *self)
{
  g_autofree gchar *sink_hw_address = NULL;
  g_object_get (self->sink, "hw-address", &sink_hw_address, NULL);
  g_autofree gchar *path = g_strdup_printf ("/com/deepin/Cooperation/NetworkDisplay/Sink%s", g_strdelimit (g_strdup (sink_hw_address), ":", '_'));
  D_ND_INFO ("start export nd sink:%s", path);
  self->dbus_path = g_strdup (path);
  static const GDBusInterfaceVTable sink_vtable = {
    handle_sink_method_call,
    handle_sink_get_property,
  };
  g_autoptr (GError) error = NULL;
  guint registration_id = g_dbus_connection_register_object (self->bus,
                                                             path,
                                                             self->network_display_sink_info->interfaces[0],
                                                             &sink_vtable,
                                                             self,
                                                             NULL,
                                                             &error);
  g_assert (registration_id > 0);
  self->registration_id = registration_id;
}

void
nd_sink_dbus_stop_export (NdDbusSink *self)
{
  D_ND_INFO ("stop export nd sink:%s", self->hw_address);
  if (self->registration_id > 0)
    g_dbus_connection_unregister_object (self->bus, self->registration_id);
}

static void
handle_sink_method_call (GDBusConnection *connection,
                         const gchar *sender,
                         const gchar *object_path,
                         const gchar *interface_name,
                         const gchar *method_name,
                         GVariant *parameters,
                         GDBusMethodInvocation *invocation,
                         gpointer user_data)
{
  // g_dbus_method_invocation_return_value(invocation, NULL);
  // g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", response));
  // g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to switch to guest");
  D_ND_INFO ("Nd dbus sink method call %s:", method_name);
  NdDbusSink *self = user_data;
  if (g_strcmp0 (method_name, "Connect") == 0)
    {
      if (stream_sink || self->is_portal_init_running)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 G_DBUS_ERROR,
                                                 G_DBUS_ERROR_LIMITS_EXCEEDED,
                                                 "Don't allow connect multiple sink");
          return;
        }
      self->is_portal_init_running = TRUE;
      init_portal_async (self);
    }
  else if (g_strcmp0 (method_name, "Cancel") == 0)
    {
      if (!stream_sink && !self->is_portal_init_running)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 G_DBUS_ERROR,
                                                 G_DBUS_ERROR_LIMITS_EXCEEDED,
                                                 "Not exist streaming sink");
          return;
        }
      self->is_portal_init_running = FALSE;
      handle_cancel (self);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "Unknown method");
      return;
    }
  g_dbus_method_invocation_return_value (invocation, NULL);
}

static GVariant *
handle_sink_get_property (GDBusConnection *connection,
                          const gchar *sender,
                          const gchar *object_path,
                          const gchar *interface_name,
                          const gchar *property_name,
                          GError **error,
                          gpointer user_data)
{
  NdDbusSink *sink = user_data;
  if (g_strcmp0 (property_name, "Status") == 0)
    {
      return g_variant_new_int32 (sink->status);
    }
  if (g_strcmp0 (property_name, "Name") == 0)
    {
      return g_variant_new_string (sink->name);
    }
  if (g_strcmp0 (property_name, "Strength") == 0)
    {
      return g_variant_new_uint32 (sink->strength);
    }
  if (g_strcmp0 (property_name, "HwAddress") == 0)
    {
      return g_variant_new_string (sink->hw_address);
    }

  return NULL;
}

static void
set_prop_name (NdDbusSink *self, const gchar *name)
{
  self->name = g_strdup (name);
  emit_nd_manager_value_changed (self, "Name", g_variant_new_string (self->name));
}

static void
set_prop_status (NdDbusSink *self, gint32 status)
{
  self->status = status;
  emit_nd_manager_value_changed (self, "Status", g_variant_new_int32 (status));
}

static void
emit_nd_manager_value_changed (const NdDbusSink *self,
                               const gchar *property_name,
                               GVariant *property_value)
{
  if (self->bus)
    emit_object_dbus_value_changed (self->bus,
                                    self->dbus_path,
                                    DEEPIN_ND_SINK_DBUS_INTERFACE,
                                    property_name,
                                    property_value);
}

static void
notify_callback (GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
  g_autoptr (GError) error = NULL;

  // Complete the async call
  g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

  if (error)
    {
      D_ND_WARNING ("Error sending notification: %s", error->message);
    }
}

static void
send_notify (NdDbusSink *self, const gchar *body)
{
  GVariant *notification_params = g_variant_new ("(susssasa{sv}i)",
                                                 "Deepin network display",
                                                 0,
                                                 "dialog-information",
                                                 "network display",
                                                 body,
                                                 NULL,
                                                 NULL,
                                                 0);
  g_dbus_proxy_call (self->notify_proxy,
                     "Notify",
                     notification_params,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     notify_callback,
                     self);
}