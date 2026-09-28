/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2016-2023 Antenore Gatta, Giovanni Panozzo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *  In addition, as a special exception, the copyright holders give
 *  permission to link the code of portions of this program with the
 *  OpenSSL library under certain conditions as described in each
 *  individual source file, and distribute linked combinations
 *  including the two.
 *  You must obey the GNU General Public License in all respects
 *  for all of the code used other than OpenSSL. *  If you modify
 *  file(s) with this exception, you may extend this exception to your
 *  version of the file(s), but you are not obligated to do so. *  If you
 *  do not wish to do so, delete this exception statement from your
 *  version. *  If you delete this exception statement from all source
 *  files in the program, then also delete it here.
 *
 */

/**
 * @file remmina_theme.c
 * @brief Follow the desktop's light/dark color-scheme preference.
 *
 * Uses the cross-desktop XDG portal interface org.freedesktop.portal.Settings
 * to read the "org.freedesktop.appearance" / "color-scheme" key and to receive
 * the SettingChanged signal when the user toggles light/dark at the OS level.
 * This works on GNOME, KDE and any desktop shipping a settings portal, both
 * inside and outside a sandbox.
 *
 * color-scheme values (freedesktop appearance spec):
 *   0 = No preference
 *   1 = Prefer dark
 *   2 = Prefer light
 */

#include "config.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <gtk/gtk.h>
#include <gio/gio.h>

#include "remmina_pref.h"
#include "remmina_log.h"
#include "remmina_theme.h"
#include "remmina/remmina_trace_calls.h"

#define PORTAL_BUS_NAME		"org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH	"/org/freedesktop/portal/desktop"
#define PORTAL_SETTINGS_IFACE	"org.freedesktop.portal.Settings"
#define APPEARANCE_NAMESPACE	"org.freedesktop.appearance"
#define COLOR_SCHEME_KEY	"color-scheme"

/* Cached desktop preference: TRUE when the OS currently asks for a dark theme. */
static gboolean system_prefers_dark = FALSE;
#ifndef _WIN32
/* Proxy kept alive for the whole session so the SettingChanged signal keeps firing. */
static GDBusProxy *settings_proxy = NULL;
#endif
static gboolean theme_initialized = FALSE;

#ifndef _WIN32
/**
 * The portal returns values wrapped in one or more variant layers depending on
 * the method (Read double-wraps, ReadOne and SettingChanged single-wrap).
 * Peel every G_VARIANT_TYPE_VARIANT layer until we reach the real value.
 */
static GVariant *remmina_theme_unwrap(GVariant *v)
{
	TRACE_CALL(__func__);
	GVariant *cur = g_variant_ref(v);

	while (cur && g_variant_is_of_type(cur, G_VARIANT_TYPE_VARIANT)) {
		GVariant *inner = g_variant_get_variant(cur);
		g_variant_unref(cur);
		cur = inner;
	}
	return cur;
}

/**
 * Interpret a color-scheme variant (uint32) into our cached boolean.
 * @return TRUE if the cached value changed.
 */
static gboolean remmina_theme_store_color_scheme(GVariant *value)
{
	TRACE_CALL(__func__);
	GVariant *inner;
	gboolean prefers_dark;
	gboolean changed;

	if (!value)
		return FALSE;

	inner = remmina_theme_unwrap(value);
	if (!inner || !g_variant_is_of_type(inner, G_VARIANT_TYPE_UINT32)) {
		if (inner)
			g_variant_unref(inner);
		return FALSE;
	}

	/* 1 == "Prefer dark"; 0 (no preference) and 2 (prefer light) mean not-dark. */
	prefers_dark = (g_variant_get_uint32(inner) == 1);
	g_variant_unref(inner);

	changed = (prefers_dark != system_prefers_dark);
	system_prefers_dark = prefers_dark;
	return changed;
}

/**
 * Handler for the portal SettingChanged(namespace, key, value) signal.
 * Re-applies the theme when the color-scheme actually changed and the user
 * has opted into following the system theme.
 */
static void remmina_theme_on_setting_changed(GDBusProxy *proxy, const gchar *sender_name,
					     const gchar *signal_name, GVariant *parameters,
					     gpointer user_data)
{
	TRACE_CALL(__func__);
	const gchar *ns = NULL;
	const gchar *key = NULL;
	GVariant *value = NULL;

	if (g_strcmp0(signal_name, "SettingChanged") != 0)
		return;

	g_variant_get(parameters, "(&s&sv)", &ns, &key, &value);

	if (g_strcmp0(ns, APPEARANCE_NAMESPACE) == 0 && g_strcmp0(key, COLOR_SCHEME_KEY) == 0) {
		if (remmina_theme_store_color_scheme(value)) {
			REMMINA_DEBUG("Desktop color-scheme changed, system now prefers %s theme",
				      system_prefers_dark ? "dark" : "light");
			if (remmina_pref.dark_theme_auto)
				remmina_theme_apply();
		}
	}

	if (value)
		g_variant_unref(value);
}

/**
 * Read the current color-scheme once, synchronously, at startup.
 * Tries ReadOne (portal version >= 2) first and falls back to Read.
 */
static void remmina_theme_read_initial(GDBusProxy *proxy)
{
	TRACE_CALL(__func__);
	GVariant *ret = NULL;
	GError *error = NULL;

	ret = g_dbus_proxy_call_sync(proxy, "ReadOne",
				     g_variant_new("(ss)", APPEARANCE_NAMESPACE, COLOR_SCHEME_KEY),
				     G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	if (!ret) {
		g_clear_error(&error);
		ret = g_dbus_proxy_call_sync(proxy, "Read",
					     g_variant_new("(ss)", APPEARANCE_NAMESPACE, COLOR_SCHEME_KEY),
					     G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	}

	if (ret) {
		GVariant *value = NULL;
		g_variant_get(ret, "(v)", &value);
		remmina_theme_store_color_scheme(value);
		if (value)
			g_variant_unref(value);
		g_variant_unref(ret);
		REMMINA_DEBUG("Desktop reports it prefers a %s theme",
			      system_prefers_dark ? "dark" : "light");
	} else {
		REMMINA_DEBUG("Could not read color-scheme from the desktop portal: %s",
			      error ? error->message : "no settings portal available");
		g_clear_error(&error);
	}
}
#endif

gboolean remmina_theme_system_prefers_dark(void)
{
	TRACE_CALL(__func__);
	return system_prefers_dark;
}

void remmina_theme_apply(void)
{
	TRACE_CALL(__func__);
	GtkSettings *settings = gtk_settings_get_default();
	gboolean dark;

	if (!settings)
		return;

	if (remmina_pref.dark_theme_auto)
		dark = system_prefers_dark;
	else
		dark = remmina_pref.dark_theme;

	g_object_set(settings,
	             "gtk-theme-name", "Adwaita",
	             "gtk-application-prefer-dark-theme", dark,
	             NULL);
}

void remmina_theme_init(void)
{
	TRACE_CALL(__func__);

	if (theme_initialized) {
		remmina_theme_apply();
		return;
	}
	theme_initialized = TRUE;

#ifdef _WIN32
	HKEY hKey;
	DWORD val = 1;
	DWORD sz = sizeof(DWORD);
	if (RegOpenKeyExW(HKEY_CURRENT_USER,
	                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
	                  0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &sz);
		RegCloseKey(hKey);
	}
	system_prefers_dark = (val == 0);
	REMMINA_DEBUG("Windows dark theme detection: AppsUseLightTheme=%lu, prefers_dark=%d",
	              (unsigned long)val, system_prefers_dark);
	remmina_theme_apply();
	return;
#else
	GError *error = NULL;
	settings_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
						       G_DBUS_PROXY_FLAGS_NONE,
						       NULL,
						       PORTAL_BUS_NAME,
						       PORTAL_OBJECT_PATH,
						       PORTAL_SETTINGS_IFACE,
						       NULL,
						       &error);
	if (settings_proxy) {
		remmina_theme_read_initial(settings_proxy);
		g_signal_connect(settings_proxy, "g-signal",
				 G_CALLBACK(remmina_theme_on_setting_changed), NULL);
	} else {
		REMMINA_DEBUG("Settings portal unavailable, automatic dark/light theme disabled: %s",
			      error ? error->message : "unknown error");
		g_clear_error(&error);
	}

	remmina_theme_apply();
#endif
}
