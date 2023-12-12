/* nd-meta-provider.c
 *
 * Copyright 2018 Benjamin Berg <bberg@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "deepin-network-displays-config.h"
#include "nd-meta-provider.h"
#include "nd-meta-sink.h"

struct _NdMetaProvider
{
  GObject    parent_instance;

  gboolean   discover;

  GPtrArray *sinks;
  GPtrArray *providers;
};

enum {
  PROP_HAS_PROVIDERS = 1,
  PROP_DISCOVER,

  PROP_LAST = PROP_DISCOVER,
};

static GParamSpec * props[PROP_LAST] = { NULL, };

static void nd_meta_provider_provider_iface_init (NdProviderIface *iface);
static GList * nd_meta_provider_provider_get_sinks (NdProvider *provider);

G_DEFINE_TYPE_EXTENDED (NdMetaProvider, nd_meta_provider, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (ND_TYPE_PROVIDER,
                                               nd_meta_provider_provider_iface_init);
                       )


static void
provider_sink_added_cb (NdMetaProvider *meta_provider, NdSink *sink, NdProvider *provider)
{
  g_autoptr(GPtrArray) sink_matches = NULL;
  g_autoptr(GPtrArray) meta_sinks = NULL; // 存放meta_sink的数组 和sink能匹配上的所有已经保存的sink
  NdMetaSink *meta_sink;

  g_object_get (sink, "matches", &sink_matches, NULL);
  g_assert (sink_matches != NULL);

  meta_sinks = g_ptr_array_new ();

  for (gint i = 0; i < meta_provider->sinks->len; i++)
    if (nd_meta_sink_matches_sink (g_ptr_array_index (meta_provider->sinks, i), sink)) // 通过matches字段(实际是数组)的内容判断是否为同一个设备
      g_ptr_array_add (meta_sinks, g_ptr_array_index (meta_provider->sinks, i));

  if (meta_sinks->len > 1)
    g_warning ("MetaProvider: Found two meta sinks that belong to the same sink. This should not happen!\n");

  if (meta_sinks->len > 0)
    {
      g_info("matched sink num == 1");
      meta_sink = g_ptr_array_remove_index (meta_sinks, 0); // g_ptr_array_remove_index返回值是移除的那个元素

      while (meta_sinks->len > 0)
        {
          NdMetaSink *merge_meta;
          NdSink *merge_sink;
          merge_meta = g_ptr_array_remove_index_fast (meta_sinks, 0);
          if (!g_ptr_array_remove (meta_provider->sinks, merge_meta))
            g_warning ("Could not remove sink from internal list!");
          g_signal_emit_by_name (meta_provider, "sink-removed", merge_meta);

          while ((merge_sink = nd_meta_sink_get_sink (merge_meta)))
            {
              nd_meta_sink_remove_sink (merge_meta, merge_sink);
              nd_meta_sink_add_sink (meta_sink, merge_sink);
            }
        }

      // 重复sink无需再次添加 // TODO 上游存在问题:显示空白设备.移除该逻辑会导致peer remove时断言错误.需要重新梳理设备添加和删除逻辑,判断此处是否需要增加设备
       nd_meta_sink_add_sink (meta_sink, sink);
    }
  else
    {
      g_info("matched sink num == 0");
      meta_sink = nd_meta_sink_new (sink);
      g_ptr_array_add (meta_provider->sinks, meta_sink);
      g_signal_emit_by_name (meta_provider, "sink-added", meta_sink);
    }
}

static void
provider_sink_removed_cb (NdMetaProvider *meta_provider, NdSink *sink, NdProvider *provider)
{
  g_autoptr(GPtrArray) sink_matches = NULL;
  g_autoptr(NdMetaSink) meta_sink = NULL;
  guint idx = 0;
  // sink 是 p2p sink
  g_object_get (sink, "matches", &sink_matches, NULL);
  g_assert (sink_matches != NULL);
  g_autofree gchar *t_sink_name = NULL;
  g_object_get (sink, "display-name", &t_sink_name, NULL);
  g_debug ("sink name is %s", t_sink_name);
  /* Search all known meta sinks for the matching one.
   * Note that we really need to search for it rather than doing
   * a faster lookup, as sink that is removed may not be reporting
   * its matches correctly anymore. */
  // 先找到对应的meta_sink
  g_debug ("meta_provider sinks num is %d", meta_provider->sinks->len);
  for (gint i = 0; i < meta_provider->sinks->len; i++)
    {
      NdMetaSink *s = g_ptr_array_index (meta_provider->sinks, i);
      gboolean has = nd_meta_sink_has_sink (s, sink);
      g_debug ("meta_sink has sink %d", has);
      gchar *hw_address = NULL;
      g_object_get (nd_meta_sink_get_sink (s), "hw-address", &hw_address, NULL);
      g_debug ("meta_sink current sink %s", hw_address);
    }
  g_assert (g_ptr_array_find_with_equal_func (meta_provider->sinks,
                                              sink,
                                              (GEqualFunc) nd_meta_sink_has_sink,
                                              &idx));

  meta_sink = g_object_ref (g_ptr_array_index (meta_provider->sinks, idx));
  g_assert (meta_sink != NULL);

  gchar *sink_name = NULL;
  g_object_get (meta_sink, "display-name", &sink_name, NULL);
  gchar *hw_address = NULL;
  g_object_get (meta_sink, "hw-address", &hw_address, NULL);
  g_debug ("provider_sink_removed_cb: sink is %s %s", sink_name, hw_address);

  if (nd_meta_sink_remove_sink (meta_sink, sink)) // meta_sink 中的 current_sink 已经被clear了
    {
      g_debug ("meta sink is empty, remove it and we are done ");
      /* meta sink is empty, remove it and we are done */
      g_ptr_array_remove (meta_provider->sinks, meta_sink);
      g_signal_emit_by_name (meta_provider, "sink-removed", meta_sink);

      return;
    }
  g_debug ("meta sink is not empty");
}

static void
nd_meta_provider_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  NdMetaProvider *self = ND_META_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_DISCOVER:
      g_value_set_boolean (value, self->discover);
      break;

    case PROP_HAS_PROVIDERS:
      g_value_set_boolean (value, self->providers->len > 0);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
nd_meta_provider_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  NdMetaProvider *self = ND_META_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_DISCOVER:
      self->discover = g_value_get_boolean (value);

      for (gint i = 0; i < self->providers->len; i++)
        g_object_set (g_ptr_array_index (self->providers, i), "discover", self->discover, NULL);

      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
nd_meta_provider_finalize (GObject *object)
{
  NdMetaProvider *meta_provider = ND_META_PROVIDER (object);

  g_ptr_array_free (meta_provider->sinks, TRUE);
  meta_provider->sinks = NULL;
  g_ptr_array_free (meta_provider->providers, TRUE);
  meta_provider->providers = NULL;

  /* Rely on provider signals to be disconnected automatically. */

  G_OBJECT_CLASS (nd_meta_provider_parent_class)->finalize (object);
}

static void
nd_meta_provider_class_init (NdMetaProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = nd_meta_provider_get_property;
  object_class->set_property = nd_meta_provider_set_property;
  object_class->finalize = nd_meta_provider_finalize;

  g_object_class_override_property (object_class, PROP_DISCOVER, "discover");

  props[PROP_HAS_PROVIDERS] =
    g_param_spec_boolean ("has-providers", "Has Providers",
                          "The meta provider has a provider.",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, props);
}

static void
nd_meta_provider_init (NdMetaProvider *meta_provider)
{
  // 默认开启扫描
  meta_provider->discover = TRUE;

  meta_provider->sinks = g_ptr_array_new_with_free_func (g_object_unref);
  meta_provider->providers = g_ptr_array_new_with_free_func (g_object_unref);
}

/******************************************************************
* NdProvider interface implementation
******************************************************************/

static void
nd_meta_provider_provider_iface_init (NdProviderIface *iface)
{
  iface->get_sinks = nd_meta_provider_provider_get_sinks;
}

static GList *
nd_meta_provider_provider_get_sinks (NdProvider *provider)
{
  g_debug("call meta provider get sinks");
  NdMetaProvider *meta_provider = ND_META_PROVIDER (provider);
  GList *res = NULL;

  for (gint i = 0; i < meta_provider->sinks->len; i++)
    res = g_list_prepend (res, meta_provider->sinks->pdata[i]);

  return res;
}

/******************************************************************
* NdMetaProvider public functions
******************************************************************/

/**
 * nd_meta_provider_get_providers
 * @meta_provider: a #NdMetaProvider
 *
 * Retrieve the providers registered with this meta provider.
 *
 * Returns: (transfer container) (element-type NdProvider):
 *    A list of all known providers
 */
GList *
nd_meta_provider_get_providers (NdMetaProvider *meta_provider)
{
  GList *res = NULL;

  for (gint i = 0; i < meta_provider->providers->len; i++)
    res = g_list_prepend (res, meta_provider->providers->pdata[i]);

  return res;
}

/**
 * nd_meta_provider_add_provider
 * @meta_provider: a #NdMetaProvider
 * @provider: a #NdProvider
 *
 * Adds the provider to the list of known providers.
 */
void
nd_meta_provider_add_provider (NdMetaProvider *meta_provider,
                               NdProvider     *provider)
{
  // provider 为 p2p-provider
  g_autoptr(GList) list = NULL;
  GList *item;

  g_assert (provider);
  g_assert (!g_ptr_array_find (meta_provider->providers, provider, NULL));

  g_ptr_array_add (meta_provider->providers, g_object_ref (provider));

  g_signal_connect_object (provider,
                           "sink-added",
                           (GCallback) provider_sink_added_cb, // sink 为 p2p-sink
                           meta_provider,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (provider,
                           "sink-removed",
                           (GCallback) provider_sink_removed_cb,
                           meta_provider,
                           G_CONNECT_SWAPPED);

  /* Sync discovery property */
  g_object_set (provider, "discover", meta_provider->discover, NULL);

  /* Explicitly add all existing sinks */
  list = nd_provider_get_sinks (provider);
  item = list;
  while (item)
    {
      provider_sink_added_cb (meta_provider, ND_SINK (item->data), provider);
      item = item->next;
    }

  g_object_notify_by_pspec (G_OBJECT (meta_provider), props[PROP_HAS_PROVIDERS]);
}

/**
 * nd_meta_provider_remove_provider
 * @meta_provider: a #NdMetaProvider
 * @provider: a #NdProvider
 *
 * Remove the provider from the list of providers.
 */
void
nd_meta_provider_remove_provider (NdMetaProvider *meta_provider,
                                  NdProvider     *provider)
{
  g_autoptr(GList) list = NULL;
  GList *item;

  g_assert (provider);

  g_signal_handlers_disconnect_by_data (provider, meta_provider);

  /* Explicitly remove all existing sinks */
  list = nd_provider_get_sinks (provider);
  item = list;
  while (item)
    {
      provider_sink_removed_cb (meta_provider, ND_SINK (item->data), provider);
      item = item->next;
    }

  g_assert (g_ptr_array_remove (meta_provider->providers, provider));

  g_object_notify_by_pspec (G_OBJECT (meta_provider), props[PROP_HAS_PROVIDERS]);
}

NdMetaProvider *
nd_meta_provider_new (void)
{
  return g_object_new (ND_TYPE_META_PROVIDER,
                       NULL);
}
