// SPDX-FileCopyrightText: 2023 - 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "nd-dbus-manager.h"

#include "nd-dbus-sink.h"
#include "nd-meta-provider.h"
#include "nd-nm-device-registry.h"
#include "nd-sink.h"
#include "wfd/wfd-media-factory.h"

static void handle_provider_signal (NdDbusManager *self);
static void gen_node_info_by_xml (NdDbusManager *self);
static void nd_dbus_pulseaudio_init_async_cb (GObject *source_object,
                                              GAsyncResult *res,
                                              gpointer user_data);
static void on_meta_provider_has_provider_changed_cb (NdDbusManager *self,
                                                      GParamSpec *pspec,
                                                      NdMetaProvider *provider);
static void emit_nd_manager_dbus_value_changed (const NdDbusManager *self,
                                                const gchar *property_name,
                                                GVariant *property_value);
static void set_dbus_prop_discover (NdDbusManager *self,
                                    gboolean enable);
static void set_dbus_prop_sink_list (NdDbusManager *self,
                                     GPtrArray *sink_list);
static void set_dbus_prop_missing_capabilities (NdDbusManager *self,
                                                GPtrArray *missing_capabilities);
static void add_dbus_missing_capabilities (NdDbusManager *self,
                                           const gchar *capability);
static void delete_dbus_missing_capabilities (NdDbusManager *self,
                                              const gchar *capability);
static GVariant *get_sink_list (NdDbusManager *self);
static void set_deepin_audio_auto_switch (gboolean enable);
static gboolean get_deepin_audio_auto_switch ();
static gboolean idle_auto_quit_check (gpointer user_data);
static void handle_manager_quit (NdDbusManager *self);

static const gchar *no_exist_wireless = "NoExistWireless";
static const gchar *not_support_P2P = "NotSupportP2P";
static const gchar *no_video_encoder = "NoVideoEncoder";

static GMainLoop *loop;

#define DEEPIN_ND_DBUS_PATH "/com/deepin/Cooperation/NetworkDisplay"
#define DEEPIN_ND_DBUS_INTERFACE "com.deepin.Cooperation.NetworkDisplay"
#define DEEPIN_ND_DBUS_NAME "com.deepin.Cooperation.NetworkDisplay"

static const uint idle_quit_sec = 180;

// gobject 相关操作
struct _NdDbusManager
{
  GObject parent_instance;
  GDBusNodeInfo *network_display_info; // 内存在dbus不再导出时释放
  GDBusConnection *bus;
  guint reg_id;

  GPtrArray *sink_list;            // nd-dbus-sink对象的数组
  GPtrArray *missing_capabilities; // 字符串数组

  NdMetaProvider *meta_provider;
  NdNMDeviceRegistry *nm_device_registry;
  gboolean discover;
  GCancellable *cancellable;
  GMutex sink_list_mu;

  GTimeVal busy_time; // 每次 dbus method 调用会更新该时间
};
G_DEFINE_TYPE (NdDbusManager, nd_dbus_manager, G_TYPE_OBJECT)

NdDbusManager *
nd_dbus_manager_new (void)
{
  return g_object_new (ND_TYPE_DBUS_MANAGER, NULL);
}

static void
nd_dbus_manager_finalize (GObject *object)
{
  D_ND_INFO ("nd dbus manager finalize");
  G_OBJECT_CLASS (nd_dbus_manager_parent_class)->finalize (object);

  NdDbusManager *self = ND_DBUS_MANAGER (object);

  g_dbus_connection_unregister_object (self->bus, self->reg_id);
  g_dbus_node_info_unref (self->network_display_info);
  g_ptr_array_free (self->sink_list, TRUE);
  self->sink_list = NULL;
  g_ptr_array_free (self->missing_capabilities, TRUE);
  self->missing_capabilities = NULL;
  g_clear_object (&self->meta_provider);
  g_clear_object (&self->nm_device_registry);
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_mutex_clear (&self->sink_list_mu);
}

static void
nd_dbus_manager_class_init (NdDbusManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = nd_dbus_manager_finalize;
}

static void
nd_dbus_manager_init (NdDbusManager *self)
{
  self->discover = TRUE;                                                // 默认用TRUE
  self->missing_capabilities = g_ptr_array_new_with_free_func (g_free); // 当使用g_ptr_array_free释放指针数组时,会自动调用g_free释放元素的内存
  self->sink_list = g_ptr_array_new_with_free_func (g_object_unref);
  g_mutex_init (&self->sink_list_mu);
  GStrv missing_video = NULL;
  GStrv missing_audio = NULL;
  gboolean have_basic_codecs = FALSE;
  // 查询是否缺失编码器
  have_basic_codecs = wfd_get_missing_codecs (&missing_video, &missing_audio);
  if (!have_basic_codecs)
    {
      add_dbus_missing_capabilities (self, no_video_encoder);
    }
  // 解析dbus xml
  gen_node_info_by_xml (self);

  self->meta_provider = nd_meta_provider_new ();
  // 监听meta_provider信号,has-providers代表是否有设备可以进行P2P连接
  g_signal_connect_object (self->meta_provider,
                           "notify::has-providers",
                           (GCallback) on_meta_provider_has_provider_changed_cb,
                           self,
                           G_CONNECT_SWAPPED);
  // 监听 nm_client 的事件，并将数据设置给 meta_provider;
  self->nm_device_registry = nd_nm_device_registry_new (self->meta_provider);
  // 主动判断一次,是否有设备可以进行P2P连接
//  on_meta_provider_has_provider_changed_cb (self, NULL, NULL);
  // 开启/关闭扫描
  g_object_set (self->meta_provider, "discover", self->discover, NULL);
  // 监听sink增减,创建对应的dbus-sink
  handle_provider_signal (self);

  g_get_current_time (&self->busy_time);
  g_timeout_add_seconds (idle_quit_sec, idle_auto_quit_check, self);
}

// 处理持有对象的信号和回调
static void
on_meta_provider_has_provider_changed_cb (NdDbusManager *self,
                                          GParamSpec *pspec,
                                          NdMetaProvider *provider)
{
  gboolean has_providers = FALSE;

  g_object_get (self->meta_provider, "has-providers", &has_providers, NULL);
  const gchar *has = has_providers ? "TRUE" : "FALSE";
  D_ND_INFO ("nd manager has providers:%s", has);
  if (!has_providers)
    add_dbus_missing_capabilities (self, not_support_P2P);
  else
    delete_dbus_missing_capabilities (self, not_support_P2P);
}

// 处理 dbus sink 取消时
static void
nd_dbus_sink_cancel_cb (void *user_data)
{
//  set_deepin_audio_auto_switch (TRUE);
}

static void
sink_added_cb (NdDbusManager *self,
               NdSink *sink,
               NdProvider *provider)
{
  g_mutex_lock (&self->sink_list_mu);
  D_ND_INFO ("Find a new sink");
  if (g_ptr_array_find_with_equal_func (
          self->sink_list,
          sink,
          (GEqualFunc) nd_dbus_sink_equal_sink,
          NULL))
    {
      // 是否需要更新sink的数据
      // 正常情况应该是先移除再添加
      D_ND_INFO ("this sink is exist");
      g_mutex_unlock (&self->sink_list_mu);
      return;
    }

  if (!self->discover)
    {
      D_ND_INFO ("dnd is disable,don't need add new sink");
      g_mutex_unlock (&self->sink_list_mu);
      return;
    }
  g_autoptr (NdDbusSink) dbus_sink = nd_dbus_sink_new (self->meta_provider,
                                                       sink,
                                                       self->bus);
  D_ND_INFO ("not exist this sink ,start add a new sink: %s %s", nd_sink_dbus_get_name (dbus_sink), nd_sink_dbus_get_hw_address (dbus_sink));
  nd_sink_dbus_set_cancel_cb (dbus_sink, nd_dbus_sink_cancel_cb, self);
  g_ptr_array_add (self->sink_list, g_object_ref (dbus_sink));
  nd_sink_dbus_export (dbus_sink);
  emit_nd_manager_dbus_value_changed (self, "SinkList", get_sink_list (self));
  g_mutex_unlock (&self->sink_list_mu);
}

static void
sink_removed_cb (NdDbusManager *self,
                 NdSink *sink,
                 NdProvider *provider)
{
  // remove 和 add 操作需要加锁
  g_mutex_lock (&self->sink_list_mu);
  D_ND_INFO ("Removing a sink");
  guint index = 0;
  if (g_ptr_array_find_with_equal_func (self->sink_list,
                                        sink,
                                        (GEqualFunc) nd_dbus_sink_equal_sink, // 通过内存地址判断meta_sink和sink是否为同一个
                                        &index))
    {
      NdDbusSink *dbus_sink = g_ptr_array_index (self->sink_list, index);
      D_ND_INFO ("Remove a exist sink: %s %s", nd_sink_dbus_get_name (dbus_sink), nd_sink_dbus_get_hw_address (dbus_sink));
      nd_sink_dbus_stop_export (dbus_sink);
      g_ptr_array_remove_index (self->sink_list, index); // https://docs.gtk.org/glib/type_func.PtrArray.remove_index.html 返回的元素内存可能已经释放
      GVariant *sink_list = get_sink_list (self);
      emit_nd_manager_dbus_value_changed (self, "SinkList", sink_list);
    }
  else
    {
      D_ND_WARNING ("Not exist this sink");
    }
  g_mutex_unlock (&self->sink_list_mu);
}

static void
handle_provider_signal (NdDbusManager *self)
{
  // 完成 SinkList 的增减;创建对应的 nd-dbus-sink 和导出对应的 nd-dbus-sink;发送属性改变信号
  NdProvider *provider = ND_PROVIDER (self->meta_provider);
  g_signal_connect_object (provider,
                           "sink-added",
                           (GCallback) sink_added_cb,
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (provider,
                           "sink-removed",
                           (GCallback) sink_removed_cb,
                           self,
                           G_CONNECT_SWAPPED);
}

// 检查服务是否可以退出
static gboolean
idle_auto_quit_check (gpointer user_data)
{
  NdDbusManager *self = ND_DBUS_MANAGER (user_data);
  for (gint i = 0; i < self->sink_list->len; i++)
    {
      NdDbusSink *sink = (NdDbusSink *) self->sink_list->pdata[i];
      NdSinkState state = nd_sink_dbus_get_status (sink);
      if (state != ND_SINK_STATE_DISCONNECTED)
        {
          D_ND_INFO ("has busy sink, don't need quit service");
          return G_SOURCE_CONTINUE;
        }
    }
  GTimeVal current_time;
  g_get_current_time (&current_time);
  if ((current_time.tv_sec - self->busy_time.tv_sec) > idle_quit_sec)
    {
      D_ND_INFO ("The service is in idle state and can quit.");
      handle_manager_quit (self);
      return G_SOURCE_REMOVE;
    }
  return G_SOURCE_CONTINUE;
}

/*
 * dbus操作都在以下代码中，包含：
 * dbus导出
 * dbus接口操作
 * dbus属性操作
 * dbus属性设置和属性改变信号
 */
static void
sink_list_free_element (gpointer data, gpointer user_data)
{
  // 释放成员内存的函数，可以根据实际情况来实现
  NdDbusSink *dbus_sink = ND_DBUS_SINK (data);
  D_ND_INFO ("free sink list element");
  nd_sink_dbus_stop_export (dbus_sink);
  g_object_unref (dbus_sink);
}

static void
free_sink_list (NdDbusManager *self)
{
  g_mutex_lock (&self->sink_list_mu);
  // 先遍历所有sink停止导出，再重置GPtrArray的内存;
  g_ptr_array_foreach (self->sink_list,
                       sink_list_free_element,
                       NULL);
  self->sink_list = g_ptr_array_new_full (0, g_object_unref);
  GVariant *sink_list = get_sink_list (self);
  emit_nd_manager_dbus_value_changed (self,
                                      "SinkList",
                                      sink_list);
  g_mutex_unlock (&self->sink_list_mu);
}

static void
manager_real_quit (gpointer user_data)
{
  D_ND_INFO ("disable dnd, start real quit service");
  NdDbusManager *self = ND_DBUS_MANAGER (user_data);
  free_sink_list (self);
  g_main_loop_quit (loop);
}

// 进程退出的处理
static void
handle_manager_quit (NdDbusManager *self)
{
  manager_real_quit (self);
}

static gboolean
get_deepin_audio_auto_switch ()
{
  const gchar *command = "dde-dconfig --get -a org.deepin.dde.daemon -r org.deepin.dde.daemon.audio -k autoSwitchPort";
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  gint exit_status;
  g_autoptr (GError) error = NULL;
  gboolean result = g_spawn_command_line_sync (command, &stdout_text, &stderr_text, &exit_status, &error);
  if (error)
    D_ND_WARNING ("Failed to run '%s': %s", command, error->message);
  else if (exit_status != 0)
    D_ND_WARNING ("Failed to run, '%s' returned %d", command, exit_status);
  else if (result)
    {
      g_autofree gchar *res = g_strstrip (stdout_text);
      return (g_strcmp0 ("true", res) == 0);
    }
}

static void
set_deepin_audio_auto_switch (gboolean enable)
{
  gchar *auto_string = enable ? "true" : "false";
  g_autofree gchar *command = g_strdup_printf ("dde-dconfig --set -a org.deepin.dde.daemon -r org.deepin.dde.daemon.audio -k autoSwitchPort -v %s", auto_string);
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  gint exit_status = 0;
  g_autoptr (GError) error = NULL;
  g_spawn_command_line_sync (command, &stdout_text, &stderr_text, &exit_status, &error);
  if (error)
    D_ND_WARNING ("Failed to run '%s': %s", command, error->message);
  else if (exit_status != 0)
    D_ND_WARNING ("Failed to run, '%s' returned %d", command, exit_status);
}

// dbus method 处理
static void
handle_manager_method_call (GDBusConnection *connection,
                            const gchar *sender,
                            const gchar *object_path,
                            const gchar *interface_name,
                            const gchar *method_name,
                            GVariant *parameters,
                            GDBusMethodInvocation *invocation,
                            gpointer user_data)
{
  // 返回空g_dbus_method_invocation_return_value(invocation, NULL);
  // 返回一个数据g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", response));
  // 返回一个错误g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to xxx");
  D_ND_INFO ("Nd dbus manager method call %s:", method_name);
  NdDbusManager *self = user_data;
  g_get_current_time (&self->busy_time);
  if (g_strcmp0 (method_name, "Refresh") == 0)
    {
      if (self->discover == FALSE)
        {
          // discover 正常情况应该不会是FALSE
          g_dbus_method_invocation_return_error (
              invocation,
              G_DBUS_ERROR,
              G_DBUS_ERROR_FAILED,
              "The device is not enable, need to enable the device first");
          return;
        }
//      g_object_set (self->meta_provider, "discover", TRUE, NULL);
      // 如果是先移除sink再添加sink,那么不需要单独更新sink的属性
    }
  else if (g_strcmp0 (method_name, "Enable") == 0)
    {
      if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(b)")))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid arguments");
          return;
        }
      gboolean enable = FALSE;
      g_variant_get (parameters, "(b)", &enable);
      g_object_set (self->meta_provider, "discover", enable, NULL);
      set_dbus_prop_discover (self, enable);
      // 关闭时的新增sink不添加到 sink_list 中，开启时重新从provider获取一遍设备信息
      if (enable)
        {
          D_ND_INFO ("enable dnd, restart get sinks");
          // 从provider获取一遍设备信息
          g_autoptr (GList) list = NULL;
          GList *item = NULL;
          /* Explicitly add all existing sinks */
          list = nd_provider_get_sinks (ND_PROVIDER (self->meta_provider));
          item = list;
          while (item)
            {
              sink_added_cb (self, ND_SINK (item->data), NULL);
              item = item->next;
            }
        }
      else
        {
          handle_manager_quit (self);
        }
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

// dbus property 相关操作
static GVariant *
get_enabled (NdDbusManager *self)
{
  return g_variant_new_boolean (self->discover);
}

// 调用需要加锁
static GVariant *
get_sink_list (NdDbusManager *self)
{
  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("ao"));
  for (gint i = 0; i < self->sink_list->len; i++)
    {
      NdDbusSink *sink = (NdDbusSink *) self->sink_list->pdata[i];
      g_autofree gchar *path = NULL;
      path = g_strdup_printf ("/com/deepin/Cooperation/NetworkDisplay/Sink%s",
                              g_strdelimit (g_strdup (nd_sink_dbus_get_hw_address (sink)), ":", '_'));
      g_variant_builder_add_value (&builder, g_variant_new_object_path (path));
      D_ND_INFO ("SinkList: get a sink mac: %s", path);
    }
  return g_variant_builder_end (&builder);
}

static GVariant *
get_missing_capabilities (NdDbusManager *self)
{
  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
  for (gint i = 0; i < self->missing_capabilities->len; i++)
    {
      gchar *miss_ability = g_ptr_array_index (self->missing_capabilities, i);
      g_variant_builder_add_value (&builder, g_variant_new_string (miss_ability));
    }
  return g_variant_builder_end (&builder);
}

static void
set_dbus_prop_discover (NdDbusManager *self, gboolean enable)
{
  self->discover = enable;
  emit_nd_manager_dbus_value_changed (self,
                                      "Enabled",
                                      g_variant_new_boolean (enable));
}

static void
set_dbus_prop_sink_list (NdDbusManager *self, GPtrArray *sink_list)
{
  g_mutex_lock (&self->sink_list_mu);
  self->sink_list = sink_list;
  emit_nd_manager_dbus_value_changed (self, "SinkList", get_sink_list (self));
  g_mutex_unlock (&self->sink_list_mu);
}

static void
set_dbus_prop_missing_capabilities (NdDbusManager *self,
                                    GPtrArray *missing_capabilities)
{
  self->missing_capabilities = missing_capabilities;
  emit_nd_manager_dbus_value_changed (self,
                                      "MissingCapabilities",
                                      get_missing_capabilities (self));
}

static void
add_dbus_missing_capabilities (NdDbusManager *self, const gchar *capability)
{
  g_ptr_array_add (self->missing_capabilities, g_strdup (capability));
  emit_nd_manager_dbus_value_changed (self,
                                      "MissingCapabilities",
                                      get_missing_capabilities (self));
}

static void
delete_dbus_missing_capabilities (NdDbusManager *self, const gchar *capability)
{
  for (gint i = 0; i < self->missing_capabilities->len; i++)
    {
      gchar *miss_ability = g_ptr_array_index (self->missing_capabilities, i);

      if (!g_str_equal (capability, miss_ability))
        continue;

      g_ptr_array_remove_index (self->missing_capabilities, i);
      emit_nd_manager_dbus_value_changed (self,
                                          "MissingCapabilities",
                                          get_missing_capabilities (self));
      break;
    }
}

static void
emit_nd_manager_dbus_value_changed (const NdDbusManager *self,
                                    const gchar *property_name,
                                    GVariant *property_value)
{
  if (self->bus)
    emit_object_dbus_value_changed (self->bus,
                                    DEEPIN_ND_DBUS_PATH,
                                    DEEPIN_ND_DBUS_INTERFACE,
                                    property_name,
                                    property_value);
}

void
emit_object_dbus_value_changed (GDBusConnection *bus,
                                const gchar *path,
                                const gchar *interface_name,
                                const gchar *property_name,
                                GVariant *property_value)
{
  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
  g_variant_builder_add (&builder, "{sv}", property_name, property_value);
  D_ND_INFO ("emit property changed:%s", property_name);
  g_autoptr (GError) error = NULL;
  if (!g_dbus_connection_emit_signal (
          bus,
          NULL,
          path,
          "org.freedesktop.DBus.Properties",
          "PropertiesChanged",
          g_variant_new ("(sa{sv}as)", interface_name, &builder, NULL),
          &error))
    {
      D_ND_WARNING ("Failed to emit PropertiesChanged signal: %s", error->message);
    }
}

static GVariant *
handle_manager_get_property (GDBusConnection *connection,
                             const gchar *sender,
                             const gchar *object_path,
                             const gchar *interface_name,
                             const gchar *property_name,
                             GError **error,
                             gpointer user_data)
{
  NdDbusManager *self = user_data;
  if (g_strcmp0 (property_name, "SinkList") == 0)
    {
      g_mutex_lock (&self->sink_list_mu);
      GVariant *ret = get_sink_list (self);
      g_mutex_unlock (&self->sink_list_mu);
      return ret;
    }
  if (g_strcmp0 (property_name, "Enabled") == 0)
    {
      return get_enabled (self);
    }
  if (g_strcmp0 (property_name, "MissingCapabilities") == 0)
    {
      return get_missing_capabilities (self);
    }
  return NULL;
}

// dbus导出操作
static void
gen_node_info_by_xml (NdDbusManager *self)
{
  const gchar *network_display_interface = "<node>"
                                           "  <interface name='com.deepin.Cooperation.NetworkDisplay'>"
                                           "    <property name='SinkList' type='ao' access='read'/>"
                                           "    <property name='Enabled' type='b' access='read'/>"
                                           "    <property name='MissingCapabilities' type='as' access='read'/>"
                                           "    <method name='Refresh'></method>"
                                           "    <method name='Enable'>"
                                           "      <arg name='enable' direction='in' type='b'/>"
                                           "    </method>"
                                           "  </interface>"
                                           "</node>";
  self->network_display_info = g_dbus_node_info_new_for_xml (network_display_interface, NULL);
  g_assert (self->network_display_info != NULL);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar *name,
                 gpointer user_data)
{
  NdDbusManager *self = user_data;
  self->bus = g_object_ref (connection);
  static const GDBusInterfaceVTable network_display_vtable = {
    handle_manager_method_call,
    handle_manager_get_property
  };
  guint registration_id = g_dbus_connection_register_object (
      connection,
      "/com/deepin/Cooperation/NetworkDisplay",
      self->network_display_info->interfaces[0],
      &network_display_vtable,
      user_data, /* user_data */
      NULL,      /* user_data_free_func */
      NULL);     /* GError** */
  g_assert (registration_id > 0);
  self->reg_id = registration_id;
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
  D_ND_WARNING ("com.deepin.Cooperation.NetworkDisplay name lost, need quit this service");
  NdDbusManager *self = ND_DBUS_MANAGER (user_data);
  handle_manager_quit (self);
}

void
dbus_export (NdDbusManager *self)
{
  guint owner_id = g_bus_own_name (
      G_BUS_TYPE_SESSION,
      DEEPIN_ND_DBUS_NAME,
      G_BUS_NAME_OWNER_FLAGS_NONE,
      on_bus_acquired,
      NULL,
      on_name_lost,
      self,
      NULL);
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  g_bus_unown_name (owner_id);
  g_main_loop_unref (loop);
}
