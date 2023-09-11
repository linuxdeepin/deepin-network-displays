// SPDX-FileCopyrightText: 2023 - 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <gio/gio.h>
#include <glib-object.h>
G_BEGIN_DECLS

#define ND_TYPE_DBUS_MANAGER (nd_dbus_manager_get_type ())
#define D_ND_INFO(format, ...) g_info ("[%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define D_ND_WARNING(format, ...) g_warning ("[%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define D_ND_DEBUG(format, ...) g_debug (format, ##__VA_ARGS__)
G_DECLARE_FINAL_TYPE (NdDbusManager, nd_dbus_manager, ND, DBUS_MANAGER, GObject)

NdDbusManager *nd_dbus_manager_new (void);
void dbus_export (NdDbusManager *self);
void emit_object_dbus_value_changed (GDBusConnection *bus,
                                     const gchar *path,
                                     const gchar *interface_name,
                                     const gchar *property_name,
                                     GVariant *property_value);
G_END_DECLS
