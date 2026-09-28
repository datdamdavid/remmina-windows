/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2009-2011 Vic Lee
 * Copyright (C) 2014-2015 Antenore Gatta, Fabio Castelli, Giovanni Panozzo
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


#include "config.h"

#ifdef GDK_WINDOWING_X11
#include <cairo/cairo-xlib.h>
#else
#include <cairo/cairo.h>
#endif
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <stdlib.h>

#include "remmina.h"
#include "remmina_main.h"
#include "rcw.h"
#include "remmina_applet_menu_item.h"
#include "remmina_applet_menu.h"
#include "remmina_file.h"
#include "remmina_file_editor.h"
#include "remmina_file_manager.h"
#include "remmina_log.h"
#include "remmina_message_panel.h"
#include "remmina_ext_exec.h"
#include "remmina_plugin_manager.h"
#include "remmina_pref.h"
#include "remmina_theme.h"
#include "remmina_protocol_widget.h"
#include "remmina_public.h"
#include "remmina_scrolled_viewport.h"
#include "remmina_unlock.h"
#include "remmina_utils.h"
#include "remmina_widget_pool.h"
#include "rcw_platform.h"
#include "remmina/remmina_trace_calls.h"

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif


#define DEBUG_KB_GRABBING 0
#include "remmina_exec.h"

gchar *remmina_pref_file;
RemminaPref remmina_pref;
static gboolean should_display = FALSE;

G_DEFINE_TYPE(RemminaConnectionWindow, rcw, GTK_TYPE_WINDOW)

#define MOTION_TIME 100

#ifdef GDK_WINDOWING_WAYLAND
#define IS_WAYLAND(widget) (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(widget))))
#else
#define IS_WAYLAND(widget) (FALSE)
#endif

/* default timeout used to hide the floating toolbar when switching profile */
#define TB_HIDE_TIME_TIME 1500

#define FULL_SCREEN_TARGET_MONITOR_UNDEFINED -1

struct _RemminaConnectionWindowPriv {
	GtkNotebook *					notebook;
	GtkWidget *					floating_toolbar_widget;
	GtkWidget *					overlay;
	GtkWidget *					revealer;
	GtkWidget *					overlay_ftb_overlay;
	GtkWidget *					overlay_ftb_fr;
	gint						ftb_move_horiz;
	gint						ftb_move_vert;

	GPtrArray * 				multimon_areas;
	gint					    multimon_main_monitor;

	GtkWidget *					floating_toolbar_label;
	gdouble						floating_toolbar_opacity;

	/* Various delayed and timer event source ids */
	guint						acs_eventsourceid;              // timeout
	guint						spf_eventsourceid;              // idle
	guint						delayed_grab_eventsourceid;
	guint						grab_retry_eventsourceid;       // timeout
	guint						ftb_hide_eventsource;           // timeout
	guint						tar_eventsource;                // timeout
	guint						hidetb_eventsource;             // timeout
	guint						dwp_eventsourceid;              // timeout

	GtkWidget *					toolbar;
	GtkWidget *					grid;

	/* Toolitems that need to be handled */
	GtkToolItem *					toolitem_menu;
	GtkToolItem *					toolitem_autofit;
	GtkToolItem *					toolitem_fullscreen;
	GtkToolItem *					toolitem_switch_page;
	GtkToolItem *					toolitem_dynres;
	GtkToolItem *					toolitem_scale;
	GtkToolItem *					toolitem_viewonly;
	GtkToolItem *					toolitem_grab;
	GtkToolItem *					toolitem_invert_scroll;
	GtkToolItem *					toolitem_multimon;
	GtkToolItem *					toolitem_preferences;
	GtkToolItem *					toolitem_tools;
	GtkToolItem *					toolitem_creds;
	GtkToolItem *					toolitem_new;
	GtkToolItem *					toolitem_duplicate;
	GtkToolItem *					toolitem_screenshot;
	GtkToolItem *					toolitem_edit_pref;
	GtkWidget *					fullscreen_option_button;
	GtkWidget *					fullscreen_scaler_button;
	GtkWidget *					scaler_option_button;

	GtkWidget *					pin_button;
	gboolean					pin_down;

	gboolean					sticky;

	/* Flag to turn off toolbar signal handling when toolbar is
	 * reconfiguring, usually due to a tab switch */
	gboolean					toolbar_is_reconfiguring;

	/* This is the current view mode, i.e. VIEWPORT_FULLSCREEN_MODE,
	 * as saved on the "viwemode" profile preference file */
	gint						view_mode;
	gboolean					multi_mon;

	/* Status variables used when in fullscreen mode. Needed
	 * to restore a fullscreen mode after coming from scrolled */
	gint						fss_view_mode;
	/* Status variables used when in scrolled window mode. Needed
	 * to restore a scrolled window mode after coming from fullscreen */
	gint						ss_width, ss_height;
	gboolean					ss_maximized;

	gint						grab_realize_source_id;
	gboolean					grab_pending_status;
	gboolean					grab_current_status;

	gboolean					hostkey_activated;
	gboolean					hostkey_used;

	RemminaConnectionWindowOnDeleteConfirmMode	on_delete_confirm_mode;
};

typedef struct _RemminaConnectionObject {
	RemminaConnectionWindow *	cnnwin;
	RemminaFile *			remmina_file;

	GtkWidget *			proto;
	GtkWidget *			aspectframe;
	GtkWidget *			viewport;

	GtkWidget *			scrolled_container;

	gboolean			plugin_can_scale;

	gboolean			connected;
	gboolean			dynres_unlocked;

	gulong				deferred_open_size_allocate_handler;
} RemminaConnectionObject;

G_DEFINE_TYPE(MultimonWindow, multimon_window, GTK_TYPE_WINDOW)

enum {
	TOOLBARPLACE_SIGNAL,
	LAST_SIGNAL
};

static guint rcw_signals[LAST_SIGNAL] =
{ 0 };

static RemminaConnectionWindow *rcw_create_scrolled(gint width, gint height, gboolean maximize);
static RemminaConnectionWindow *rcw_create_fullscreen(GtkWindow *old, gint view_mode);
static gboolean rcw_hostkey_func(RemminaProtocolWidget *gp, guint keyval, gboolean release);
static GtkWidget *rco_create_tab_page(RemminaConnectionObject *cnnobj);
static GtkWidget *rco_create_tab_label(RemminaConnectionObject *cnnobj);
static void rco_change_scalemode(RemminaConnectionObject *cnnobj, gboolean bdyn, gboolean bscale);

void rcw_grab_focus(RemminaConnectionWindow *cnnwin);
static GtkWidget *rcw_create_toolbar(RemminaConnectionWindow *cnnwin, gint mode, gboolean is_floating);
static void rcw_place_toolbar(GtkToolbar *toolbar, GtkGrid *grid, GtkWidget *sibling, int toolbar_placement);
static void rco_update_toolbar(RemminaConnectionObject *cnnobj);
static void rcw_keyboard_grab_request(RemminaConnectionObject *cnnobj, gboolean want_grab);
static void rcw_keyboard_grab(RemminaConnectionWindow *cnnwin);
static GtkWidget *rcw_append_new_page(RemminaConnectionWindow *cnnwin, RemminaConnectionObject *cnnobj);

static void rcw_ftb_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data);

static void rcw_create_widget_areas(RemminaConnectionObject*cnnobj);
static GdkWindow* rcw_widget_active_area(RemminaConnectionWindow*cnnwin);
static void rcw_show_widget_areas(RemminaConnectionWindow*priv);
static void rcw_iconify_widget_areas(RemminaConnectionWindow*priv);
static void rcw_restore_widget_areas(RemminaConnectionWindow*priv);
static void rcw_dispose_widget_areas(RemminaConnectionWindowPriv*priv);

static const GtkTargetEntry dnd_targets_ftb[] =
{
	{
		(char *)"text/x-remmina-ftb",
		GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
		0
	},
};

static const GtkTargetEntry dnd_targets_tb[] =
{
	{
		(char *)"text/x-remmina-tb",
		GTK_TARGET_SAME_APP,
		0
	},
};

static void rcw_class_init(RemminaConnectionWindowClass *klass)
{
	TRACE_CALL(__func__);
	GtkCssProvider *provider;

	provider = gtk_css_provider_new();

	/* It’s important to remove padding, border and shadow from GtkViewport or
	 * we will never know its internal area size, because GtkViweport::viewport_get_view_allocation,
	 * which returns the internal size of the GtkViewport, is private and we cannot access it */

#if GTK_CHECK_VERSION(3, 14, 0)
	gtk_css_provider_load_from_data(provider,
					"#remmina-cw-viewport, #remmina-cw-aspectframe {\n"
					"  padding:0;\n"
					"  border:0;\n"
					"  background-color: black;\n"
					"}\n"
					"GtkDrawingArea {\n"
					"}\n"
					"GtkToolbar {\n"
					"  -GtkWidget-window-dragging: 0;\n"
					"}\n"
					"#remmina-connection-window-fullscreen {\n"
					"  border-color: black;\n"
					"  background-color: black;\n"
					"}\n"
					"#remmina-small-button {\n"
					"  outline-offset: 0;\n"
					"  outline-width: 0;\n"
					"  padding: 0;\n"
					"  border: 0;\n"
					"}\n"
					"#remmina-pin-button {\n"
					"  outline-offset: 0;\n"
					"  outline-width: 0;\n"
					"  padding: 2px;\n"
					"  border: 0;\n"
					"}\n"
					"#remmina-tab-page {\n"
					"  background-color: black;\n"
					"}\n"
					"#remmina-scrolled-container {\n"
					"}\n"
					"#remmina-scrolled-container.undershoot {\n"
					"  background: none;\n"
					"}\n"
					"#remmina-tab-page {\n"
					"}\n"
					"#ftbbox-upper {\n"
					"  background-color: white;\n"
					"  color: black;\n"
					"  border-style: none solid solid solid;\n"
					"  border-width: 1px;\n"
					"  border-radius: 4px;\n"
					"  padding: 0px;\n"
					"}\n"
					"#ftbbox-lower {\n"
					"  background-color: white;\n"
					"  color: black;\n"
					"  border-style: solid solid none solid;\n"
					"  border-width: 1px;\n"
					"  border-radius: 4px;\n"
					"  padding: 0px;\n"
					"}\n"
					"#ftb-handle {\n"
					"}\n"
					".message_panel {\n"
					"  border: 0px solid;\n"
					"  padding: 20px 20px 20px 20px;\n"
					"}\n"
					".message_panel entry {\n"
					"  background-image: none;\n"
					"  border-width: 4px;\n"
					"  border-radius: 8px;\n"
					"}\n"
					".message_panel .title_label {\n"
					"  font-size: 2em; \n"
					"}\n"
					, -1, NULL);

#else
	gtk_css_provider_load_from_data(provider,
					"#remmina-cw-viewport, #remmina-cw-aspectframe {\n"
					"  padding:0;\n"
					"  border:0;\n"
					"  background-color: black;\n"
					"}\n"
					"#remmina-cw-message-panel {\n"
					"}\n"
					"GtkDrawingArea {\n"
					"}\n"
					"GtkToolbar {\n"
					"  -GtkWidget-window-dragging: 0;\n"
					"}\n"
					"#remmina-connection-window-fullscreen {\n"
					"  border-color: black;\n"
					"  background-color: black;\n"
					"}\n"
					"#remmina-small-button {\n"
					"  -GtkWidget-focus-padding: 0;\n"
					"  -GtkWidget-focus-line-width: 0;\n"
					"  padding: 0;\n"
					"  border: 0;\n"
					"}\n"
					"#remmina-pin-button {\n"
					"  -GtkWidget-focus-padding: 0;\n"
					"  -GtkWidget-focus-line-width: 0;\n"
					"  padding: 2px;\n"
					"  border: 0;\n"
					"}\n"
					"#remmina-scrolled-container {\n"
					"}\n"
					"#remmina-scrolled-container.undershoot {\n"
					"  background: none\n"
					"}\n"
					"#remmina-tab-page {\n"
					"}\n"
					"#ftbbox-upper {\n"
					"  border-style: none solid solid solid;\n"
					"  border-width: 1px;\n"
					"  border-radius: 4px;\n"
					"  padding: 0px;\n"
					"}\n"
					"#ftbbox-lower {\n"
					"  border-style: solid solid none solid;\n"
					"  border-width: 1px;\n"
					"  border-radius: 4px;\n"
					"  padding: 0px;\n"
					"}\n"
					"#ftb-handle {\n"
					"}\n"

					, -1, NULL);
#endif

	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
						  GTK_STYLE_PROVIDER(provider),
						  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	g_object_unref(provider);

	/* Define a signal used to notify all rcws of toolbar move */
	rcw_signals[TOOLBARPLACE_SIGNAL] = g_signal_new("toolbar-place", G_TYPE_FROM_CLASS(klass),
							G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET(RemminaConnectionWindowClass, toolbar_place), NULL, NULL,
							g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static RemminaConnectionObject *rcw_get_cnnobj_at_page(RemminaConnectionWindow *cnnwin, gint npage)
{
	GtkWidget *po;

	if (!cnnwin->priv->notebook)
		return NULL;
	po = gtk_notebook_get_nth_page(GTK_NOTEBOOK(cnnwin->priv->notebook), npage);
	return g_object_get_data(G_OBJECT(po), "cnnobj");
}

static RemminaConnectionObject *rcw_get_visible_cnnobj(RemminaConnectionWindow *cnnwin)
{
	gint np;

	if (cnnwin != NULL && cnnwin->priv != NULL && cnnwin->priv->notebook != NULL) {
		np = gtk_notebook_get_current_page(GTK_NOTEBOOK(cnnwin->priv->notebook));
		if (np < 0)
			return NULL;
		return rcw_get_cnnobj_at_page(cnnwin, np);
	} else {
		return NULL;
	}
}

static RemminaScaleMode get_current_allowed_scale_mode(RemminaConnectionObject *cnnobj, gboolean *dynres_avail, gboolean *scale_avail)
{
	TRACE_CALL(__func__);
	RemminaScaleMode scalemode;
	gboolean plugin_has_dynres, plugin_can_scale, is_multimon;

	scalemode = remmina_protocol_widget_get_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

	plugin_has_dynres = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									  REMMINA_PROTOCOL_FEATURE_TYPE_DYNRESUPDATE);

	is_multimon = remmina_protocol_widget_get_multimon(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

	plugin_can_scale = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									 REMMINA_PROTOCOL_FEATURE_TYPE_SCALE);

	/* force switch scale mode when auto and unlocked */
	if (remmina_pref.start_dynres && plugin_has_dynres && cnnobj->dynres_unlocked && scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE) {
		rco_change_scalemode(cnnobj, TRUE, FALSE);
		scalemode = remmina_protocol_widget_get_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	}

	if (is_multimon && scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE) {
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES;
	}

	/* Forbid scalemode REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED when not possible */
	if (!plugin_can_scale && scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED)
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE;

	if (scale_avail)
		*scale_avail = plugin_can_scale;
	if (dynres_avail)
		*dynres_avail = (plugin_has_dynres && cnnobj->dynres_unlocked);

	return scalemode;
}

static void rco_disconnect_current_page(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);

	/* Disconnects the connection which is currently in view in the notebook */
	remmina_protocol_widget_close_connection(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
}

static void rcw_kp_ungrab(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	GdkDisplay *display;

#if GTK_CHECK_VERSION(3, 20, 0)
	GdkSeat *seat;
#else
	GdkDeviceManager *manager;
	GdkDevice *keyboard = NULL;
#endif


	if (cnnwin->priv->grab_retry_eventsourceid) {
		g_source_remove(cnnwin->priv->grab_retry_eventsourceid);
		cnnwin->priv->grab_retry_eventsourceid = 0;
	}
	if (cnnwin->priv->delayed_grab_eventsourceid) {
		g_source_remove(cnnwin->priv->delayed_grab_eventsourceid);
		cnnwin->priv->delayed_grab_eventsourceid = 0;
	}

	display = gtk_widget_get_display(GTK_WIDGET(cnnwin));
#if GTK_CHECK_VERSION(3, 20, 0)
	seat = gdk_display_get_default_seat(display);
#else
	manager = gdk_display_get_device_manager(display);
	keyboard = gdk_device_manager_get_client_pointer(manager);
#endif

#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: --- ungrabbing");
#endif

#if GTK_CHECK_VERSION(3, 20, 0)
	/* We can use gtk_seat_grab()/_ungrab() only after GTK 3.24 */
	gdk_seat_ungrab(seat);
#else
	if (keyboard != NULL) {
		if (gdk_device_get_source(keyboard) != GDK_SOURCE_KEYBOARD)
			keyboard = gdk_device_get_associated_device(keyboard);
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		gdk_device_ungrab(keyboard, GDK_CURRENT_TIME);
		G_GNUC_END_IGNORE_DEPRECATIONS
	}
#endif
}

static gboolean rcw_keyboard_grab_retry(RemminaConnectionWindow*cnnwin)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: retry grab");
#endif
	rcw_keyboard_grab(cnnwin);
	cnnwin->priv->grab_retry_eventsourceid = 0;
	return G_SOURCE_REMOVE;
}

static void rcw_keyboard_grab(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	GdkDisplay *display;

#if GTK_CHECK_VERSION(3, 20, 0)
	GdkSeat *seat;
#else
	GdkDeviceManager *manager;
#endif
	GdkGrabStatus ggs;
	GdkDevice *keyboard = NULL;

	display = gtk_widget_get_display(GTK_WIDGET(cnnwin));
#if GTK_CHECK_VERSION(3, 20, 0)
	seat = gdk_display_get_default_seat(display);
	keyboard = gdk_seat_get_pointer(seat);
#else
	manager = gdk_display_get_device_manager(display);
	keyboard = gdk_device_manager_get_client_pointer(manager);
#endif

	if (keyboard != NULL) {
		if (gdk_device_get_source(keyboard) != GDK_SOURCE_KEYBOARD)
			keyboard = gdk_device_get_associated_device(keyboard);

		GdkWindow*grab_window = rcw_widget_active_area(cnnwin);
		if (grab_window == NULL) {
			grab_window = gtk_widget_get_window(gtk_widget_get_toplevel(GTK_WIDGET(cnnwin)));
		}

#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: profile asks for grabbing, let’s try.");
#endif
		/* Up to GTK version 3.20 we can grab the keyboard with gdk_device_grab().
		 * in GTK 3.20 gdk_seat_grab() should be used instead of gdk_device_grab().
		 * There is a bug in GTK up to 3.22: When gdk_device_grab() fails
		 * the widget is hidden:
		 * https://gitlab.gnome.org/GNOME/gtk/commit/726ad5a5ae7c4f167e8dd454cd7c250821c400ab
		 * The bugfix will be released with GTK 3.24.
		 * Also please note that the newer gdk_seat_grab() is still calling gdk_device_grab().
		 *
		 * Warning: gdk_seat_grab() will call XGrabKeyboard() or XIGrabDevice()
		 * which in turn will generate a core X input event FocusOut and FocusIn
		 * but not Xinput2 events.
		 * In some cases, GTK is unable to neutralize FocusIn and FocusOut core
		 * events (ie: i3wm+Plasma with GDK_CORE_DEVICE_EVENTS=1 because detail=NotifyNonlinear
		 * instead of detail=NotifyAncestor/detail=NotifyInferior)
		 * Receiving a FocusOut event for Remmina at this time will cause an infinite loop.
		 * Therefore is important for GTK to use Xinput2 instead of core X events
		 * by unsetting GDK_CORE_DEVICE_EVENTS
		 */
#if GTK_CHECK_VERSION(3, 20, 0)
		ggs = gdk_seat_grab(seat, grab_window,
				    GDK_SEAT_CAPABILITY_KEYBOARD, TRUE, NULL, NULL, NULL, NULL);
#else
		ggs = gdk_device_grab(keyboard, grab_window, GDK_OWNERSHIP_WINDOW,
				      TRUE, GDK_KEY_PRESS | GDK_KEY_RELEASE, NULL, GDK_CURRENT_TIME);
#endif
		// in multimon, need 'insist'
		GdkGrabStatus success = cnnwin->priv->multi_mon ? GDK_GRAB_ALREADY_GRABBED : GDK_GRAB_SUCCESS;
		if (ggs != success) {
#if DEBUG_KB_GRABBING
			if (cnnwin->priv->multi_mon && ggs == GDK_GRAB_SUCCESS)
				REMMINA_DEBUG("DEBUG_KB_GRABBING: grab keyboard ok, %s", cnnwin->priv->grab_retry_eventsourceid == 0 ? "will be retried":"last try");
			else
				REMMINA_DEBUG("DEBUG_KB_GRABBING: grab keyboard failed: %d, will be retried", ggs);
#endif
			/* Reschedule grabbing in half a second if not already done */
			if (cnnwin->priv->grab_retry_eventsourceid == 0) {
				cnnwin->priv->grab_retry_eventsourceid = g_timeout_add(500, (GSourceFunc)rcw_keyboard_grab_retry, cnnwin);
			}
		} else {
#if DEBUG_KB_GRABBING
			REMMINA_DEBUG("DEBUG_KB_GRABBING: keyboard grabbed");
#endif			
			if (cnnwin->priv->grab_retry_eventsourceid != 0) {
				g_source_remove(cnnwin->priv->grab_retry_eventsourceid);
				cnnwin->priv->grab_retry_eventsourceid = 0;
			}
		}
	}
}


static gboolean rcw_keyboard_grab_realize(RemminaConnectionObject* cnnobj) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING");
#endif
	cnnobj->cnnwin->priv->grab_realize_source_id = 0;
	if (!cnnobj->cnnwin->priv->grab_pending_status) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: ungrabing");
#endif
		rcw_kp_ungrab(cnnobj->cnnwin);
		cnnobj->cnnwin->priv->grab_current_status = FALSE;
		return G_SOURCE_REMOVE;
	}
	
	if (!gtk_window_is_active(GTK_WINDOW(cnnobj->cnnwin)) && !rcw_widget_active_area(cnnobj->cnnwin)) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: canceled, no connection window is active for now");
#endif
		return G_SOURCE_REMOVE;
	}
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: call grab");
#endif
	rcw_keyboard_grab(cnnobj->cnnwin);
	cnnobj->cnnwin->priv->grab_current_status = TRUE;
	return G_SOURCE_REMOVE;
}

static void rcw_keyboard_grab_request(RemminaConnectionObject *cnnobj, gboolean want_grab)
{	
	if (!cnnobj || cnnobj->connected == FALSE)
		return;

	/* option must be activated grab */
	if (want_grab && remmina_file_get_int(cnnobj->remmina_file, "keyboard_grab", FALSE) == FALSE)
		return;
	
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: grab_wanted=%d, done=%d", want_grab, cnnobj->cnnwin->priv->grab_current_status);
#endif

	gboolean changed = cnnobj->cnnwin->priv->grab_pending_status != want_grab || cnnobj->cnnwin->priv->grab_pending_status != cnnobj->cnnwin->priv->grab_current_status;
	if (changed && cnnobj->cnnwin->priv->grab_realize_source_id != 0 && !want_grab) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: ignoring ungrab because grab already asked in this session");
#endif
		return;
	}
	cnnobj->cnnwin->priv->grab_pending_status  = want_grab;
	
	if (changed && cnnobj->cnnwin->priv->grab_realize_source_id == 0) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: launch deferred realize");
#endif
		cnnobj->cnnwin->priv->grab_realize_source_id = g_timeout_add(200, (GSourceFunc)rcw_keyboard_grab_realize, cnnobj);
	}
}

static void rcw_close_all_connections(RemminaConnectionWindow *cnnwin)
{
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkNotebook *notebook;
	GtkWidget *w;
	RemminaConnectionObject *cnnobj;
	gint i, n;

	/* connection already closed */
	if (!priv)
		return;

	notebook = GTK_NOTEBOOK(priv->notebook);

	if (GTK_IS_WIDGET(notebook)) {
		n = gtk_notebook_get_n_pages(notebook);
		for (i = n - 1; i >= 0; i--) {
			w = gtk_notebook_get_nth_page(notebook, i);
			cnnobj = (RemminaConnectionObject *)g_object_get_data(G_OBJECT(w), "cnnobj");
			/* Do close the connection on this tab */
			remmina_protocol_widget_close_connection(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
		}
	}
}

gboolean rcw_delete(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkNotebook *notebook;
	GtkWidget *dialog;
	gint i, n, nopen;

	/* connection already closed */
	if (!priv)
		return TRUE;

	notebook = GTK_NOTEBOOK(priv->notebook);

	if (!REMMINA_IS_CONNECTION_WINDOW(cnnwin))
		return TRUE;

	if (cnnwin->priv->on_delete_confirm_mode != RCW_ONDELETE_NOCONFIRM) {
		n = gtk_notebook_get_n_pages(notebook);
		nopen = 0;
		/* count all non-closed connections */
		for(i = 0; i < n; i ++) {
			RemminaConnectionObject *cnnobj = rcw_get_cnnobj_at_page(cnnwin, i);
			if (!remmina_protocol_widget_is_closed((RemminaProtocolWidget *)cnnobj->proto))
				nopen ++;
		}
		if (nopen > 1) {
			dialog = gtk_message_dialog_new(GTK_WINDOW(cnnwin), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
							GTK_BUTTONS_YES_NO,
							_("Are you sure you want to close %i active connections in the current window?"), nopen);
			i = gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
			if (i != GTK_RESPONSE_YES)
				return FALSE;
		}
		else if (nopen == 1) {
			if (remmina_pref.confirm_close) {
				dialog = gtk_message_dialog_new(GTK_WINDOW(cnnwin), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
								GTK_BUTTONS_YES_NO,
								_("Are you sure you want to close this last active connection?"));
				i = gtk_dialog_run(GTK_DIALOG(dialog));
				gtk_widget_destroy(dialog);
				if (i != GTK_RESPONSE_YES)
					return FALSE;
			}
		}
	}
	rcw_close_all_connections(cnnwin);

	return TRUE;
}

static gboolean rcw_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	TRACE_CALL(__func__);
	rcw_delete(RCW(widget));
	return TRUE;
}

static void rcw_destroy(GtkWidget *widget, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionWindow *cnnwin;

	if (!REMMINA_IS_CONNECTION_WINDOW(widget))
		return;

	cnnwin = (RemminaConnectionWindow *)widget;
	priv = cnnwin->priv;

	rcw_kp_ungrab(cnnwin);

	if (priv->acs_eventsourceid) {
		g_source_remove(priv->acs_eventsourceid);
		priv->acs_eventsourceid = 0;
	}
	if (priv->spf_eventsourceid) {
		g_source_remove(priv->spf_eventsourceid);
		priv->spf_eventsourceid = 0;
	}
	if (priv->grab_retry_eventsourceid) {
		g_source_remove(priv->grab_retry_eventsourceid);
		priv->grab_retry_eventsourceid = 0;
	}
	if (priv->grab_realize_source_id) {
		g_source_remove(priv->grab_realize_source_id);
		priv->grab_realize_source_id = 0;
	}
	if (cnnwin->priv->delayed_grab_eventsourceid) {
		g_source_remove(cnnwin->priv->delayed_grab_eventsourceid);
		cnnwin->priv->delayed_grab_eventsourceid = 0;
	}
	if (priv->ftb_hide_eventsource) {
		g_source_remove(priv->ftb_hide_eventsource);
		priv->ftb_hide_eventsource = 0;
	}
	if (priv->tar_eventsource) {
		g_source_remove(priv->tar_eventsource);
		priv->tar_eventsource = 0;
	}
	if (priv->hidetb_eventsource) {
		g_source_remove(priv->hidetb_eventsource);
		priv->hidetb_eventsource = 0;
	}
	if (priv->dwp_eventsourceid) {
		g_source_remove(priv->dwp_eventsourceid);
		priv->dwp_eventsourceid = 0;
	}
	rcw_dispose_widget_areas(priv);

	/* There is no need to destroy priv->floating_toolbar_widget,
	 * because it’s our child and will be destroyed automatically */

	cnnwin->priv = NULL;
	g_free(priv);
}

static gboolean rcw_notify_widget_toolbar_placement(GtkWidget *widget, gpointer data)
{
	TRACE_CALL(__func__);
	GType rcwtype;

	rcwtype = rcw_get_type();
	if (G_TYPE_CHECK_INSTANCE_TYPE(widget, rcwtype)) {
		g_signal_emit_by_name(G_OBJECT(widget), "toolbar-place");
		return TRUE;
	}
	return FALSE;
}

static gboolean rcw_tb_drag_failed(GtkWidget *widget, GdkDragContext *context,
				   GtkDragResult result, gpointer user_data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionWindow *cnnwin;


	cnnwin = (RemminaConnectionWindow *)user_data;
	priv = cnnwin->priv;

	if (priv->toolbar)
		gtk_widget_show(GTK_WIDGET(priv->toolbar));

	return TRUE;
}

static gboolean rcw_tb_drag_drop(GtkWidget *widget, GdkDragContext *context,
				 gint x, gint y, guint time, gpointer user_data)
{
	TRACE_CALL(__func__);
	GtkAllocation wa;
	gint new_toolbar_placement;
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionWindow *cnnwin;

	cnnwin = (RemminaConnectionWindow *)user_data;
	priv = cnnwin->priv;

	gtk_widget_get_allocation(widget, &wa);

	if (wa.width * y >= wa.height * x) {
		if (wa.width * y > wa.height * (wa.width - x))
			new_toolbar_placement = TOOLBAR_PLACEMENT_BOTTOM;
		else
			new_toolbar_placement = TOOLBAR_PLACEMENT_LEFT;
	} else {
		if (wa.width * y > wa.height * (wa.width - x))
			new_toolbar_placement = TOOLBAR_PLACEMENT_RIGHT;
		else
			new_toolbar_placement = TOOLBAR_PLACEMENT_TOP;
	}

	gtk_drag_finish(context, TRUE, TRUE, time);

	if (new_toolbar_placement != remmina_pref.toolbar_placement) {
		/* Save new position */
		remmina_pref.toolbar_placement = new_toolbar_placement;
		remmina_pref_save();

		/* Signal all windows that the toolbar must be moved */
		remmina_widget_pool_foreach(rcw_notify_widget_toolbar_placement, NULL);
	}
	if (priv->toolbar)
		gtk_widget_show(GTK_WIDGET(priv->toolbar));

	return TRUE;
}

static void rcw_tb_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data)
{
	TRACE_CALL(__func__);

	cairo_surface_t *surface;
	cairo_t *cr;
	GtkAllocation wa;
	double dashes[] = { 10 };

	gtk_widget_get_allocation(widget, &wa);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
	cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
	cairo_set_line_width(cr, 4);
	cairo_set_dash(cr, dashes, 1, 0);
	cairo_rectangle(cr, 0, 0, 16, 16);
	cairo_stroke(cr);
	cairo_destroy(cr);

	gtk_widget_hide(widget);

	gtk_drag_set_icon_surface(context, surface);
}

static void rcw_update_toolbar_opacity(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj;

	cnnobj = rcw_get_visible_cnnobj(cnnwin);
	if (!cnnobj) return;

	priv->floating_toolbar_opacity = (1.0 - TOOLBAR_OPACITY_MIN) / ((gdouble)TOOLBAR_OPACITY_LEVEL)
					 * ((gdouble)(TOOLBAR_OPACITY_LEVEL - remmina_file_get_int(cnnobj->remmina_file, "toolbar_opacity", 0)))
					 + TOOLBAR_OPACITY_MIN;
	if (priv->floating_toolbar_widget)
		gtk_widget_set_opacity(GTK_WIDGET(priv->overlay_ftb_overlay), priv->floating_toolbar_opacity);
}

static gboolean rcw_floating_toolbar_make_invisible(gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = (RemminaConnectionWindowPriv *)data;

	gtk_widget_set_opacity(GTK_WIDGET(priv->overlay_ftb_overlay), 0.0);
	priv->ftb_hide_eventsource = 0;
	return G_SOURCE_REMOVE;
}

static void rcw_floating_toolbar_show(RemminaConnectionWindow *cnnwin, gboolean show)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->floating_toolbar_widget == NULL)
		return;

	if (show || priv->pin_down) {
		/* Make the FTB no longer transparent, in case we have an hidden toolbar */
		rcw_update_toolbar_opacity(cnnwin);
		/* Remove outstanding hide events, if not yet active */
		if (priv->ftb_hide_eventsource) {
			g_source_remove(priv->ftb_hide_eventsource);
			priv->ftb_hide_eventsource = 0;
		}
	} else {
		/* If we are hiding and the toolbar must be made invisible, schedule
		 * a later toolbar hide */
		if (remmina_pref.fullscreen_toolbar_visibility == FLOATING_TOOLBAR_VISIBILITY_INVISIBLE)
			if (priv->ftb_hide_eventsource == 0)
				priv->ftb_hide_eventsource = g_timeout_add(1000, rcw_floating_toolbar_make_invisible, priv);
	}

	gtk_revealer_set_reveal_child(GTK_REVEALER(priv->revealer), show || priv->pin_down);
}

static void rco_get_desktop_size(RemminaConnectionObject *cnnobj, gint *width, gint *height)
{
	TRACE_CALL(__func__);
	RemminaProtocolWidget *gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);


	*width = remmina_protocol_widget_get_width(gp);
	*height = remmina_protocol_widget_get_height(gp);
	if (*width == 0) {
		/* Before connecting we do not have real remote width/height,
		 * so we ask profile values */
		*width = remmina_protocol_widget_get_profile_remote_width(gp);
		*height = remmina_protocol_widget_get_profile_remote_height(gp);
	}
}

static void rco_set_scrolled_policy(RemminaScaleMode scalemode, GtkScrolledWindow *scrolled_window)
{
	TRACE_CALL(__func__);

	gtk_scrolled_window_set_policy(scrolled_window,
				       scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC,
				       scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC);
}

static GtkWidget *rco_create_scrolled_container(RemminaScaleMode scalemode, int view_mode)
{
	GtkWidget *scrolled_container;

	if (view_mode == VIEWPORT_FULLSCREEN_MODE) {
		scrolled_container = remmina_scrolled_viewport_new();
	} else {
		scrolled_container = gtk_scrolled_window_new(NULL, NULL);
		rco_set_scrolled_policy(scalemode, GTK_SCROLLED_WINDOW(scrolled_container));
		gtk_container_set_border_width(GTK_CONTAINER(scrolled_container), 0);
		gtk_widget_set_can_focus(scrolled_container, FALSE);
	}

	gtk_widget_set_name(scrolled_container, "remmina-scrolled-container");
	gtk_widget_show(scrolled_container);

	return scrolled_container;
}

static gboolean rcw_toolbar_autofit_restore(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj;
	gint dwidth, dheight;
	GtkAllocation nba, ca, ta;

	cnnwin->priv->tar_eventsource = 0;

	if (priv->toolbar_is_reconfiguring)
		return G_SOURCE_REMOVE;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return FALSE;

	if (cnnobj->connected && GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
		rco_get_desktop_size(cnnobj, &dwidth, &dheight);
		gtk_widget_get_allocation(GTK_WIDGET(priv->notebook), &nba);
		gtk_widget_get_allocation(cnnobj->scrolled_container, &ca);
		gtk_widget_get_allocation(priv->toolbar, &ta);
		if (remmina_pref.toolbar_placement == TOOLBAR_PLACEMENT_LEFT ||
		    remmina_pref.toolbar_placement == TOOLBAR_PLACEMENT_RIGHT)
			gtk_window_resize(GTK_WINDOW(cnnobj->cnnwin), MAX(1, dwidth + ta.width + nba.width - ca.width),
					  MAX(1, dheight + nba.height - ca.height));
		else
			gtk_window_resize(GTK_WINDOW(cnnobj->cnnwin), MAX(1, dwidth + nba.width - ca.width),
					  MAX(1, dheight + ta.height + nba.height - ca.height));
		gtk_container_check_resize(GTK_CONTAINER(cnnobj->cnnwin));
	}
	if (GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
		RemminaScaleMode scalemode = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
		rco_set_scrolled_policy(scalemode, GTK_SCROLLED_WINDOW(cnnobj->scrolled_container));
	}

	return G_SOURCE_REMOVE;
}

static void rcw_toolbar_autofit(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
		if ((gdk_window_get_state(gtk_widget_get_window(GTK_WIDGET(cnnwin))) & GDK_WINDOW_STATE_MAXIMIZED) != 0)
			gtk_window_unmaximize(GTK_WINDOW(cnnwin));

		/* It’s tricky to make the toolbars disappear automatically, while keeping scrollable.
		 * Please tell me if you know a better way to do this */
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container), GTK_POLICY_NEVER, GTK_POLICY_NEVER);

		cnnwin->priv->tar_eventsource = g_timeout_add(200, (GSourceFunc)rcw_toolbar_autofit_restore, cnnwin);
	}
}

void rco_get_monitor_geometry(RemminaConnectionObject *cnnobj, GdkRectangle *sz)
{
	TRACE_CALL(__func__);

	/* Fill sz with the monitor (or workarea) size and position
	 * of the monitor (or workarea) where cnnobj->cnnwin is located */

	GdkRectangle monitor_geometry;

	sz->x = sz->y = sz->width = sz->height = 0;

	if (!cnnobj)
		return;
	if (!cnnobj->cnnwin)
		return;
	if (!gtk_widget_is_visible(GTK_WIDGET(cnnobj->cnnwin)))
		return;

#if GTK_CHECK_VERSION(3, 22, 0)
	GdkDisplay *display;
	GdkMonitor *monitor;
	display = gtk_widget_get_display(GTK_WIDGET(cnnobj->cnnwin));
	monitor = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(GTK_WIDGET(cnnobj->cnnwin)));
#else
	GdkScreen *screen;
	gint monitor;
	screen = gtk_window_get_screen(GTK_WINDOW(cnnobj->cnnwin));
	monitor = gdk_screen_get_monitor_at_window(screen, gtk_widget_get_window(GTK_WIDGET(cnnobj->cnnwin)));
#endif

#if GTK_CHECK_VERSION(3, 22, 0)
	gdk_monitor_get_workarea(monitor, &monitor_geometry);
	/* Under Wayland, GTK 3.22, all values returned by gdk_monitor_get_geometry()
	 * and gdk_monitor_get_workarea() seem to have been divided by the
	 * gdk scale factor, so we need to adjust the returned rect
	 * undoing the division */
#ifdef GDK_WINDOWING_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY(display)) {
		int monitor_scale_factor = gdk_monitor_get_scale_factor(monitor);
		monitor_geometry.width *= monitor_scale_factor;
		monitor_geometry.height *= monitor_scale_factor;
	}
#endif
#elif gdk_screen_get_monitor_workarea
	gdk_screen_get_monitor_workarea(screen, monitor, &monitor_geometry);
#else
	gdk_screen_get_monitor_geometry(screen, monitor, &monitor_geometry);
#endif
	*sz = monitor_geometry;
}

static void rco_check_resize(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	gboolean scroll_required = FALSE;

	GdkRectangle monitor_geometry;
	gint rd_width, rd_height;
	gint bordersz;
	gint scalemode;

	scalemode = remmina_protocol_widget_get_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

	/* Get remote destkop size */
	rco_get_desktop_size(cnnobj, &rd_width, &rd_height);

	/* Get our monitor size */
	rco_get_monitor_geometry(cnnobj, &monitor_geometry);

	if (!remmina_protocol_widget_get_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto)) &&
	    (monitor_geometry.width < rd_width || monitor_geometry.height < rd_height) &&
	    scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE)
		scroll_required = TRUE;

	switch (cnnobj->cnnwin->priv->view_mode) {
	case SCROLLED_FULLSCREEN_MODE:
		gtk_window_resize(GTK_WINDOW(cnnobj->cnnwin), monitor_geometry.width, monitor_geometry.height);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container),
					       (scroll_required ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER),
					       (scroll_required ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER));
		break;

	case VIEWPORT_FULLSCREEN_MODE:
		bordersz = scroll_required ? SCROLL_BORDER_SIZE : 0;
		gtk_window_resize(GTK_WINDOW(cnnobj->cnnwin), monitor_geometry.width, monitor_geometry.height);
		if (REMMINA_IS_SCROLLED_VIEWPORT(cnnobj->scrolled_container))
			/* Put a border around Notebook content (RemminaScrolledViewpord), so we can
			 * move the mouse over the border to scroll */
			gtk_container_set_border_width(GTK_CONTAINER(cnnobj->scrolled_container), bordersz);

		break;

	case SCROLLED_WINDOW_MODE:
		if (remmina_file_get_int(cnnobj->remmina_file, "viewmode", UNDEFINED_MODE) == UNDEFINED_MODE) {
			/* ToDo: is this really needed ? When ? */
			gtk_window_set_default_size(GTK_WINDOW(cnnobj->cnnwin),
						    MIN(rd_width, monitor_geometry.width), MIN(rd_height, monitor_geometry.height));
			if (rd_width >= monitor_geometry.width || rd_height >= monitor_geometry.height) {
				gtk_window_maximize(GTK_WINDOW(cnnobj->cnnwin));
				remmina_file_set_int(cnnobj->remmina_file, "window_maximize", TRUE);
			} else {
				rcw_toolbar_autofit(NULL, cnnobj->cnnwin);
				remmina_file_set_int(cnnobj->remmina_file, "window_maximize", FALSE);
			}
		} else {
			if (remmina_file_get_int(cnnobj->remmina_file, "window_maximize", FALSE))
				gtk_window_maximize(GTK_WINDOW(cnnobj->cnnwin));
		}
		break;

	default:
		break;
	}
}

static void rcw_set_tooltip(GtkWidget *item, const gchar *tip, guint key1, guint key2)
{
	TRACE_CALL(__func__);
	gchar *s1;
	gchar *s2;

	if (remmina_pref.hostkey && key1) {
		if (key2)
			s1 = g_strdup_printf(" (%s + %s,%s)", gdk_keyval_name(remmina_pref.hostkey),
					     gdk_keyval_name(gdk_keyval_to_upper(key1)), gdk_keyval_name(gdk_keyval_to_upper(key2)));
		else if (key1 == remmina_pref.hostkey)
			s1 = g_strdup_printf(" (%s)", gdk_keyval_name(remmina_pref.hostkey));
		else
			s1 = g_strdup_printf(" (%s + %s)", gdk_keyval_name(remmina_pref.hostkey),
					     gdk_keyval_name(gdk_keyval_to_upper(key1)));
	} else {
		s1 = NULL;
	}
	s2 = g_strdup_printf("%s%s", tip, s1 ? s1 : "");
	gtk_widget_set_tooltip_text(item, s2);
	g_free(s2);
	g_free(s1);
}

static void remmina_protocol_widget_update_alignment(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaScaleMode scalemode;
	gboolean scaledexpandedmode;
	int rdwidth, rdheight;
	gfloat aratio;

	if (!cnnobj->plugin_can_scale) {
		/* If we have a plugin that cannot scale,
		 * (i.e. SFTP plugin), then we expand proto */
		gtk_widget_set_halign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
		gtk_widget_set_valign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
	} else {
		/* Plugin can scale */

		scalemode = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
		scaledexpandedmode = remmina_protocol_widget_get_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

		/* Check if we need aspectframe and create/destroy it accordingly */
		if (scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED && !scaledexpandedmode) {
			/* We need an aspectframe as a parent of proto */
			rdwidth = remmina_protocol_widget_get_width(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
			rdheight = remmina_protocol_widget_get_height(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
			aratio = (gfloat)rdwidth / (gfloat)rdheight;
			if (!cnnobj->aspectframe) {
				/* We need a new aspectframe */
				cnnobj->aspectframe = gtk_aspect_frame_new(NULL, 0.5, 0.5, aratio, FALSE);
				gtk_widget_set_name(cnnobj->aspectframe, "remmina-cw-aspectframe");
				gtk_frame_set_shadow_type(GTK_FRAME(cnnobj->aspectframe), GTK_SHADOW_NONE);
				g_object_ref(cnnobj->proto);
				gtk_container_remove(GTK_CONTAINER(cnnobj->viewport), cnnobj->proto);
				gtk_container_add(GTK_CONTAINER(cnnobj->viewport), cnnobj->aspectframe);
				gtk_container_add(GTK_CONTAINER(cnnobj->aspectframe), cnnobj->proto);
				g_object_unref(cnnobj->proto);
				gtk_widget_show(cnnobj->aspectframe);
				if (cnnobj != NULL && cnnobj->cnnwin != NULL && cnnobj->cnnwin->priv->notebook != NULL)
					rcw_grab_focus(cnnobj->cnnwin);
			} else {
				gtk_aspect_frame_set(GTK_ASPECT_FRAME(cnnobj->aspectframe), 0.5, 0.5, aratio, FALSE);
			}
		} else {
			/* We do not need an aspectframe as a parent of proto */
			if (cnnobj->aspectframe) {
				/* We must remove the old aspectframe reparenting proto to viewport */
				g_object_ref(cnnobj->aspectframe);
				g_object_ref(cnnobj->proto);
				gtk_container_remove(GTK_CONTAINER(cnnobj->aspectframe), cnnobj->proto);
				gtk_container_remove(GTK_CONTAINER(cnnobj->viewport), cnnobj->aspectframe);
				g_object_unref(cnnobj->aspectframe);
				cnnobj->aspectframe = NULL;
				gtk_container_add(GTK_CONTAINER(cnnobj->viewport), cnnobj->proto);
				g_object_unref(cnnobj->proto);
				if (cnnobj != NULL && cnnobj->cnnwin != NULL && cnnobj->cnnwin->priv->notebook != NULL)
					rcw_grab_focus(cnnobj->cnnwin);
			}
		}

		if (scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED || scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES) {
			/* We have a plugin that can be scaled, and the scale button
			 * has been pressed. Give it the correct WxH maintaining aspect
			 * ratio of remote destkop size */
			gtk_widget_set_halign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
			gtk_widget_set_valign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
		} else {
			/* Plugin can scale, but no scaling is active. Ensure that we have
			 * aspectframe with a ratio of 1 */
			gtk_widget_set_halign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_CENTER);
			gtk_widget_set_valign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_CENTER);
		}
	}
}

static void nb_set_current_page(GtkNotebook *notebook, GtkWidget *page)
{
	gint np, i;

	np = gtk_notebook_get_n_pages(notebook);
	for (i = 0; i < np; i++) {
		if (gtk_notebook_get_nth_page(notebook, i) == page) {
			gtk_notebook_set_current_page(notebook, i);
			break;
		}
	}
}

static void nb_migrate_message_panels(GtkWidget *frompage, GtkWidget *topage)
{
	/* Migrate a single connection tab from a notebook to another one */
	GList *lst, *l;

	/* Reparent message panels */
	lst = gtk_container_get_children(GTK_CONTAINER(frompage));
	for (l = lst; l != NULL; l = l->next) {
		if (REMMINA_IS_MESSAGE_PANEL(l->data)) {
			g_object_ref(l->data);
			gtk_container_remove(GTK_CONTAINER(frompage), GTK_WIDGET(l->data));
			gtk_container_add(GTK_CONTAINER(topage), GTK_WIDGET(l->data));
			g_object_unref(l->data);
			gtk_box_reorder_child(GTK_BOX(topage), GTK_WIDGET(l->data), 0);
		}
	}
	g_list_free(lst);

}

static void rcw_migrate(RemminaConnectionWindow *from, RemminaConnectionWindow *to)
{
	/* Migrate a complete notebook from a window to another */

	gchar *tag;
	gint cp, np, i;
	GtkNotebook *from_notebook;
	GtkWidget *frompage, *newpage, *old_scrolled_container;
	RemminaConnectionObject *cnnobj;
	RemminaScaleMode scalemode;

	/* Migrate TAG */
	tag = g_strdup((gchar *)g_object_get_data(G_OBJECT(from), "tag"));
	g_object_set_data_full(G_OBJECT(to), "tag", tag, (GDestroyNotify)g_free);

	/* Migrate notebook content */
	from_notebook = from->priv->notebook;
	if (from_notebook && GTK_IS_NOTEBOOK(from_notebook)) {

		cp = gtk_notebook_get_current_page(from_notebook);
		np = gtk_notebook_get_n_pages(from_notebook);
		/* Create pages on dest notebook and migrate
		 * page content */
		for (i = 0; i < np; i++) {
			frompage = gtk_notebook_get_nth_page(from_notebook, i);
			cnnobj = g_object_get_data(G_OBJECT(frompage), "cnnobj");

			/* A scrolled container must be recreated, because it can be different on the new window/page
			  depending on view_mode */
			scalemode = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
			old_scrolled_container = cnnobj->scrolled_container;
			cnnobj->scrolled_container = rco_create_scrolled_container(scalemode, to->priv->view_mode);

			newpage = rcw_append_new_page(to, cnnobj);

			nb_migrate_message_panels(frompage, newpage);

			/* Reparent the viewport (which is inside scrolled_container) to the new page */
			g_object_ref(cnnobj->viewport);
			gtk_container_remove(GTK_CONTAINER(old_scrolled_container), cnnobj->viewport);
			gtk_container_add(GTK_CONTAINER(cnnobj->scrolled_container), cnnobj->viewport);
			g_object_unref(cnnobj->viewport);

			/* Destroy old scrolled_container. Not really needed, it will be destroyed
			 * when removing the page from the notepad */
			gtk_widget_destroy(old_scrolled_container);

		}

		/* Remove all the pages from source notebook */
		for (i = np - 1; i >= 0; i--)
			gtk_notebook_remove_page(from_notebook, i);
		gtk_notebook_set_current_page(to->priv->notebook, cp);

	}
}

static void rcw_switch_viewmode(RemminaConnectionWindow *cnnwin, int newmode)
{
	GdkWindowState s;
	RemminaConnectionWindow *newwin;
	gint old_width, old_height;
	int old_mode;

	old_mode = cnnwin->priv->view_mode;
	if (old_mode == newmode)
		return;

	if (newmode == VIEWPORT_FULLSCREEN_MODE || newmode == SCROLLED_FULLSCREEN_MODE) {
		if (old_mode == SCROLLED_WINDOW_MODE) {
			/* We are leaving SCROLLED_WINDOW_MODE, save W,H, and maximized
			 * status before self destruction of cnnwin */
			gtk_window_get_size(GTK_WINDOW(cnnwin), &old_width, &old_height);
			s = gdk_window_get_state(gtk_widget_get_window(GTK_WIDGET(cnnwin)));
		}
		newwin = rcw_create_fullscreen(GTK_WINDOW(cnnwin), cnnwin->priv->fss_view_mode);
		rcw_migrate(cnnwin, newwin);
		if (old_mode == SCROLLED_WINDOW_MODE) {
			newwin->priv->ss_maximized = (s & GDK_WINDOW_STATE_MAXIMIZED) ? TRUE : FALSE;
			newwin->priv->ss_width = old_width;
			newwin->priv->ss_height = old_height;
		}
	} else {
		newwin = rcw_create_scrolled(cnnwin->priv->ss_width, cnnwin->priv->ss_height,
					     cnnwin->priv->ss_maximized);
		rcw_migrate(cnnwin, newwin);
		if (old_mode == VIEWPORT_FULLSCREEN_MODE || old_mode == SCROLLED_FULLSCREEN_MODE)
			/* We are leaving a FULLSCREEN mode, save some parameters
			 * status before self destruction of cnnwin */
			newwin->priv->fss_view_mode = old_mode;
	}

	/* Prevent unreleased hostkey from old window to be released here */
	newwin->priv->hostkey_used = TRUE;
}


static void rcw_toolbar_fullscreen(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	RemminaProtocolWidget *gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);

	if (remmina_protocol_widget_get_multimon(gp) >= 1) {
		REMMINA_DEBUG("Fullscreen on all monitor");
	} else {
		REMMINA_DEBUG("Fullscreen on one monitor");
	}

	if ((toggle != NULL && toggle == cnnwin->priv->toolitem_fullscreen)) {
		if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle))) {
			if (remmina_protocol_widget_get_multimon(gp) >= 1) {
				gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnwin->priv->toolitem_multimon), TRUE);
				cnnwin->priv->multi_mon = TRUE;
			}
			rcw_switch_viewmode(cnnwin, cnnwin->priv->fss_view_mode);
		} else {
			rcw_switch_viewmode(cnnwin, SCROLLED_WINDOW_MODE);
			remmina_file_set_int(cnnobj->remmina_file, "multimon", 0);
			remmina_file_save(cnnobj->remmina_file);
		}
	} else if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(cnnwin->priv->toolitem_multimon))) {
		rcw_switch_viewmode(cnnwin, cnnwin->priv->fss_view_mode);
	} else {
		rcw_switch_viewmode(cnnwin, SCROLLED_WINDOW_MODE);
	}
}

static void rco_viewport_fullscreen_mode(GtkWidget *widget, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *newwin;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
		return;
	cnnobj->cnnwin->priv->fss_view_mode = VIEWPORT_FULLSCREEN_MODE;
	newwin = rcw_create_fullscreen(GTK_WINDOW(cnnobj->cnnwin), VIEWPORT_FULLSCREEN_MODE);
	rcw_migrate(cnnobj->cnnwin, newwin);
	rcw_create_widget_areas(cnnobj);
}

static void rco_scrolled_fullscreen_mode(GtkWidget *widget, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *newwin;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
		return;
	cnnobj->cnnwin->priv->fss_view_mode = SCROLLED_FULLSCREEN_MODE;
	newwin = rcw_create_fullscreen(GTK_WINDOW(cnnobj->cnnwin), SCROLLED_FULLSCREEN_MODE);
	rcw_migrate(cnnobj->cnnwin, newwin);
}

static void rcw_fullscreen_option_popdown(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	priv->sticky = FALSE;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->fullscreen_option_button), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_toolbar_fullscreen_option(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;
	GtkWidget *menu;
	GtkWidget *menuitem;
	GSList *group;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle)))
		return;

	cnnwin->priv->sticky = TRUE;

	menu = gtk_menu_new();

	menuitem = gtk_radio_menu_item_new_with_label(NULL, _("Viewport fullscreen mode"));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));
	if (cnnwin->priv->view_mode == VIEWPORT_FULLSCREEN_MODE)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);
	g_signal_connect(G_OBJECT(menuitem), "toggled", G_CALLBACK(rco_viewport_fullscreen_mode), cnnobj);

	menuitem = gtk_radio_menu_item_new_with_label(group, _("Scrolled fullscreen"));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	if (cnnwin->priv->view_mode == SCROLLED_FULLSCREEN_MODE)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);
	g_signal_connect(G_OBJECT(menuitem), "toggled", G_CALLBACK(rco_scrolled_fullscreen_mode), cnnobj);

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_fullscreen_option_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, cnnwin->priv->toolitem_fullscreen, 0,
		       gtk_get_current_event_time());
#endif
}


static void rcw_scaler_option_popdown(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->toolbar_is_reconfiguring)
		return;
	priv->sticky = FALSE;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->scaler_option_button), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_scaler_expand(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
		return;
	cnnobj = rcw_get_visible_cnnobj(cnnwin);
	if (!cnnobj)
		return;
	remmina_protocol_widget_set_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), TRUE);
	remmina_file_set_int(cnnobj->remmina_file, "scaler_expand", TRUE);
	remmina_protocol_widget_update_alignment(cnnobj);
}
static void rcw_scaler_keep_aspect(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
		return;
	cnnobj = rcw_get_visible_cnnobj(cnnwin);
	if (!cnnobj)
		return;

	remmina_protocol_widget_set_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), FALSE);
	remmina_file_set_int(cnnobj->remmina_file, "scaler_expand", FALSE);
	remmina_protocol_widget_update_alignment(cnnobj);
}

static void rcw_toolbar_scaler_option(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionObject *cnnobj;
	GtkWidget *menu;
	GtkWidget *menuitem;
	GSList *group;
	gboolean scaler_expand;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	priv = cnnwin->priv;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle)))
		return;

	scaler_expand = remmina_protocol_widget_get_expand(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));

	priv->sticky = TRUE;

	menu = gtk_menu_new();

	menuitem = gtk_radio_menu_item_new_with_label(NULL, _("Keep aspect ratio when scaled"));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));
	if (!scaler_expand)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);
	g_signal_connect(G_OBJECT(menuitem), "toggled", G_CALLBACK(rcw_scaler_keep_aspect), cnnwin);

	menuitem = gtk_radio_menu_item_new_with_label(group, _("Fill client window when scaled"));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	if (scaler_expand)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);
	g_signal_connect(G_OBJECT(menuitem), "toggled", G_CALLBACK(rcw_scaler_expand), cnnwin);

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_scaler_option_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, priv->toolitem_scale, 0,
		       gtk_get_current_event_time());
#endif
}

static void rco_switch_page_activate(GtkMenuItem *menuitem, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	gint page_num;

	page_num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menuitem), "new-page-num"));
	gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook), page_num);
}

static void rcw_toolbar_switch_page_popdown(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	priv->sticky = FALSE;

	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_switch_page), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_toolbar_switch_page(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj;

	GtkWidget *menu;
	GtkWidget *menuitem;
	GtkWidget *image;
	gint i, n;

	if (priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle)))
		return;

	priv->sticky = TRUE;

	menu = gtk_menu_new();

	n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook));
	for (i = 0; i < n; i++) {
		cnnobj = rcw_get_cnnobj_at_page(cnnobj->cnnwin, i);

		menuitem = gtk_menu_item_new_with_label(remmina_file_get_string(cnnobj->remmina_file, "name"));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

		image = gtk_image_new_from_icon_name(remmina_file_get_icon_name(cnnobj->remmina_file), GTK_ICON_SIZE_MENU);
		gtk_widget_show(image);

		g_object_set_data(G_OBJECT(menuitem), "new-page-num", GINT_TO_POINTER(i));
		g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(rco_switch_page_activate), cnnobj);
		if (i == gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook)))
			gtk_widget_set_sensitive(menuitem, FALSE);
	}

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_toolbar_switch_page_popdown),
			 cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, widget, 0, gtk_get_current_event_time());
#endif
}

static void rco_update_toolbar_autofit_button(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	GtkToolItem *toolitem;
	RemminaScaleMode sc;

	toolitem = priv->toolitem_autofit;
	if (toolitem) {
		if (priv->view_mode != SCROLLED_WINDOW_MODE) {
			gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
		} else {
			sc = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
			gtk_widget_set_sensitive(GTK_WIDGET(toolitem), sc == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE);
		}
	}
}

static void rco_change_scalemode(RemminaConnectionObject *cnnobj, gboolean bdyn, gboolean bscale)
{
	RemminaScaleMode scalemode;
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;

	if (bdyn)
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES;
	else if (bscale)
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED;
	else
		scalemode = REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE;

	remmina_protocol_widget_set_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), scalemode);
	remmina_file_set_int(cnnobj->remmina_file, "scale", scalemode);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), scalemode == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED);
	rco_update_toolbar_autofit_button(cnnobj);

	remmina_protocol_widget_call_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
						     REMMINA_PROTOCOL_FEATURE_TYPE_SCALE, 0);

	if (cnnobj->cnnwin->priv->view_mode != SCROLLED_WINDOW_MODE)
		rco_check_resize(cnnobj);
	if (GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
		rco_set_scrolled_policy(scalemode, GTK_SCROLLED_WINDOW(cnnobj->scrolled_container));
	}
}

static void rcw_toolbar_dynres(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	gboolean bdyn, bscale, plugin_has_dynres, plugin_can_scale;
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (cnnobj->connected) {
		bdyn = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));
		bscale = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_scale));

		plugin_has_dynres = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									REMMINA_PROTOCOL_FEATURE_TYPE_DYNRESUPDATE);

		plugin_can_scale = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									REMMINA_PROTOCOL_FEATURE_TYPE_SCALE);

		if (!(plugin_has_dynres && cnnobj->dynres_unlocked))
			bdyn = FALSE;

		if (!plugin_can_scale)
			bscale = FALSE;

		if (bdyn && bscale)
			bscale = FALSE;

		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toggle), bdyn);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_scale), bscale);

		rco_change_scalemode(cnnobj, bdyn, bscale);
	}
}

static void rcw_toolbar_scaled_mode(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	gboolean bdyn, bscale, plugin_has_dynres, plugin_can_scale;
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (cnnobj->connected) {
		bdyn = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_dynres));
		bscale = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));

		plugin_has_dynres = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									REMMINA_PROTOCOL_FEATURE_TYPE_DYNRESUPDATE);

		plugin_can_scale = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
									REMMINA_PROTOCOL_FEATURE_TYPE_SCALE);

		if ((!plugin_has_dynres || !cnnobj->dynres_unlocked))
			bdyn = FALSE;

		if (!plugin_can_scale)
			bscale = FALSE;

		if (bdyn && bscale)
			bdyn = FALSE;

		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_dynres), bdyn);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toggle), bscale);

		rco_change_scalemode(cnnobj, bdyn, bscale);
	}
}

static void rcw_toolbar_viewonly_mode(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	// Add viewonly mode to rcw toolbar
	// This will only be available to plugins that support viewonly
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;
	gboolean bactive = FALSE;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) {
		return;
	}

	// When opening connection, take viewonly value from remmina file
	if (cnnobj->connected) {
		bactive = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_viewonly), bactive);
	}

	// Update remmina file and let the plugin handle the viewonly
	remmina_file_set_int(cnnobj->remmina_file, "viewonly", bactive);
}

static void rcw_toolbar_multi_monitor_mode(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle))) {
		REMMINA_DEBUG("Saving multimon as 1");
		remmina_file_set_int(cnnobj->remmina_file, "multimon", 1);
		remmina_file_save(cnnobj->remmina_file);
		cnnwin->priv->multi_mon = TRUE;
		if (cnnwin->priv->view_mode == VIEWPORT_FULLSCREEN_MODE || cnnwin->priv->view_mode == SCROLLED_FULLSCREEN_MODE) {
			RemminaConnectionWindow* newwin = rcw_create_fullscreen(NULL, cnnwin->priv->view_mode);
			rcw_migrate(cnnwin, newwin);
			cnnwin = newwin;
		}
 		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnwin->priv->toolitem_fullscreen), TRUE);
		
		remmina_protocol_widget_call_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
				REMMINA_PROTOCOL_FEATURE_TYPE_MULTIMON, 0);
		rcw_create_widget_areas(cnnobj);
	} else {
		REMMINA_DEBUG("Saving multimon as 0");
		remmina_file_set_int(cnnobj->remmina_file, "multimon", 0);
		remmina_file_save(cnnobj->remmina_file);
		remmina_protocol_widget_call_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
				REMMINA_PROTOCOL_FEATURE_TYPE_MULTIMON, 0);
		rcw_toolbar_fullscreen(NULL, cnnwin);
	}
}

static void rcw_toolbar_open_main(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	remmina_exec_command(REMMINA_COMMAND_MAIN, NULL);
}

static void rcw_toolbar_preferences_popdown(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	cnnobj->cnnwin->priv->sticky = FALSE;

	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_preferences), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_toolbar_menu_popdown(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->toolbar_is_reconfiguring)
		return;

	priv->sticky = FALSE;

	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_menu), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_toolbar_creds_popdown(GtkMenuShell *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->toolbar_is_reconfiguring)
		return;

	priv->sticky = FALSE;

	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_creds), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rcw_toolbar_tools_popdown(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->toolbar_is_reconfiguring)
		return;

	priv->sticky = FALSE;

	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_tools), FALSE);
	rcw_floating_toolbar_show(cnnwin, FALSE);
}

static void rco_call_protocol_feature_radio(GtkMenuItem *menuitem, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaProtocolFeature *feature;
	gpointer value;

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))) {
		feature = (RemminaProtocolFeature *)g_object_get_data(G_OBJECT(menuitem), "feature-type");
		value = g_object_get_data(G_OBJECT(menuitem), "feature-value");

		remmina_file_set_string(cnnobj->remmina_file, (const gchar *)feature->opt2, (const gchar *)value);
		remmina_protocol_widget_call_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
	}
}

static void rco_call_protocol_feature_check(GtkMenuItem *menuitem, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaProtocolFeature *feature;
	gboolean value;

	feature = (RemminaProtocolFeature *)g_object_get_data(G_OBJECT(menuitem), "feature-type");
	value = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem));
	remmina_file_set_int(cnnobj->remmina_file, (const gchar *)feature->opt2, value);
	remmina_protocol_widget_call_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
}

static void rco_call_protocol_feature_activate(GtkMenuItem *menuitem, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaProtocolFeature *feature;

	feature = (RemminaProtocolFeature *)g_object_get_data(G_OBJECT(menuitem), "feature-type");
	remmina_protocol_widget_call_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
}

static void rcw_toolbar_preferences_radio(RemminaConnectionObject *cnnobj, RemminaFile *remminafile,
				   GtkWidget *menu, const RemminaProtocolFeature *feature, const gchar *domain, gboolean enabled)
{
	TRACE_CALL(__func__);
	GtkWidget *menuitem;
	GSList *group;
	gint i;
	const gchar **list;
	const gchar *value;

	group = NULL;
	value = remmina_file_get_string(remminafile, (const gchar *)feature->opt2);
	list = (const gchar **)feature->opt3;
	for (i = 0; list[i]; i += 2) {
		menuitem = gtk_radio_menu_item_new_with_label(group, g_dgettext(domain, list[i + 1]));
		group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

		if (enabled) {
			g_object_set_data(G_OBJECT(menuitem), "feature-type", (gpointer)feature);
			g_object_set_data(G_OBJECT(menuitem), "feature-value", (gpointer)list[i]);

			if (value && g_strcmp0(list[i], value) == 0)
				gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), TRUE);

			g_signal_connect(G_OBJECT(menuitem), "toggled",
					 G_CALLBACK(rco_call_protocol_feature_radio), cnnobj);
		} else {
			gtk_widget_set_sensitive(menuitem, FALSE);
		}
	}
}

static void rcw_toolbar_preferences_check(RemminaConnectionObject *cnnobj,
				   GtkWidget *menu, const RemminaProtocolFeature *feature,
				   const gchar *domain, gboolean enabled)
{
	TRACE_CALL(__func__);
	GtkWidget *menuitem;

	menuitem = gtk_check_menu_item_new_with_label(g_dgettext(domain, (const gchar *)feature->opt3));
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

	if (enabled) {
		g_object_set_data(G_OBJECT(menuitem), "feature-type", (gpointer)feature);

		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
					       remmina_file_get_int(cnnobj->remmina_file, (const gchar *)feature->opt2, FALSE));

		g_signal_connect(G_OBJECT(menuitem), "toggled",
				 G_CALLBACK(rco_call_protocol_feature_check), cnnobj);
	} else {
		gtk_widget_set_sensitive(menuitem, FALSE);
	}
}

static void rcw_toolbar_preferences(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionObject *cnnobj;
	const RemminaProtocolFeature *feature;
	GtkWidget *menu;
	GtkWidget *menuitem;
	gboolean separator;
	gchar *domain;
	gboolean enabled;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	priv = cnnobj->cnnwin->priv;

	if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle)))
		return;

	priv->sticky = TRUE;

	separator = FALSE;

	domain = remmina_protocol_widget_get_domain(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	menu = gtk_menu_new();
	for (feature = remmina_protocol_widget_get_features(REMMINA_PROTOCOL_WIDGET(cnnobj->proto)); feature && feature->type;
	     feature++) {
		if (feature->type != REMMINA_PROTOCOL_FEATURE_TYPE_PREF)
			continue;

		if (separator) {
			menuitem = gtk_separator_menu_item_new();
			gtk_widget_show(menuitem);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
			separator = FALSE;
		}
		enabled = remmina_protocol_widget_query_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
		switch (GPOINTER_TO_INT(feature->opt1)) {
		case REMMINA_PROTOCOL_FEATURE_PREF_RADIO:
			rcw_toolbar_preferences_radio(cnnobj, cnnobj->remmina_file, menu, feature,
						      domain, enabled);
			separator = TRUE;
			break;
		case REMMINA_PROTOCOL_FEATURE_PREF_CHECK:
			rcw_toolbar_preferences_check(cnnobj, menu, feature,
						      domain, enabled);
			break;
		}
	}

	g_free(domain);

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_toolbar_preferences_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, widget, 0, gtk_get_current_event_time());
#endif
}

static void rcw_toolbar_menu_on_launch_item(RemminaAppletMenu *menu, RemminaAppletMenuItem *menuitem, gpointer data)
{
	TRACE_CALL(__func__);
	gchar *s;

	switch (menuitem->item_type) {
	case REMMINA_APPLET_MENU_ITEM_NEW:
		remmina_exec_command(REMMINA_COMMAND_NEW, NULL);
		break;
	case REMMINA_APPLET_MENU_ITEM_FILE:
		remmina_exec_command(REMMINA_COMMAND_CONNECT, menuitem->filename);
		break;
	case REMMINA_APPLET_MENU_ITEM_DISCOVERED:
		s = g_strdup_printf("%s,%s", menuitem->protocol, menuitem->name);
		remmina_exec_command(REMMINA_COMMAND_NEW, s);
		g_free(s);
		break;
	}
}

static void rcw_toolbar_menu(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionObject *cnnobj;
	GtkWidget *menu;
	GtkWidget *menuitem = NULL;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	priv = cnnobj->cnnwin->priv;

	if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle)))
		return;

	priv->sticky = TRUE;

	menu = remmina_applet_menu_new();
	remmina_applet_menu_set_hide_count(REMMINA_APPLET_MENU(menu), remmina_pref.applet_hide_count);
	remmina_applet_menu_populate(REMMINA_APPLET_MENU(menu));

	g_signal_connect(G_OBJECT(menu), "launch-item", G_CALLBACK(rcw_toolbar_menu_on_launch_item), NULL);
	menuitem = gtk_separator_menu_item_new();
	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, widget, 0, gtk_get_current_event_time());
#endif
	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_toolbar_menu_popdown), cnnwin);
}

static void rcw_toolbar_creds(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;
	GtkWidget *menu;
	GtkWidget *menuitem = NULL;
	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle)))
		return;

	const gchar* username = remmina_file_get_string(cnnobj->remmina_file, "username");
	const gchar* password = remmina_file_get_string(cnnobj->remmina_file, "password");

	if (username == NULL && password == NULL){
		return;
	}


	menu = gtk_menu_new();
	//username
	if (username != NULL){
		menuitem = gtk_menu_item_new_with_label(_("Insert username"));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		g_object_set_data(G_OBJECT(menuitem), "keystrokes", (gpointer)username);
		g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
										G_CALLBACK(remmina_protocol_widget_send_keystrokes),
										REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	}
	
	//password
	if (password != NULL){
			menuitem = gtk_menu_item_new_with_label(_("Insert password"));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		g_object_set_data(G_OBJECT(menuitem), "keystrokes", (gpointer)password);
		g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
										G_CALLBACK(remmina_protocol_widget_send_keystrokes),
										REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	}


	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_toolbar_creds_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, widget, 0, gtk_get_current_event_time());
#endif
}

static void rcw_toolbar_tools(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionObject *cnnobj;
	const RemminaProtocolFeature *feature;
	GtkWidget *menu;
	GtkWidget *menuitem = NULL;
	GtkMenu *submenu_keystrokes;
	const gchar *domain;
	gboolean enabled;
	gchar **keystrokes;
	gchar **keystroke_values;
	gint i;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	priv = cnnobj->cnnwin->priv;

	if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle)))
		return;

	priv->sticky = TRUE;

	domain = remmina_protocol_widget_get_domain(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	menu = gtk_menu_new();
	for (feature = remmina_protocol_widget_get_features(REMMINA_PROTOCOL_WIDGET(cnnobj->proto)); feature && feature->type;
	     feature++) {
		if (feature->type != REMMINA_PROTOCOL_FEATURE_TYPE_TOOL)
			continue;

		if (feature->opt1)
			menuitem = gtk_menu_item_new_with_label(g_dgettext(domain, (const gchar *)feature->opt1));
		if (feature->opt3)
			rcw_set_tooltip(menuitem, "", GPOINTER_TO_UINT(feature->opt3), 0);
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

		enabled = remmina_protocol_widget_query_feature_by_ref(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), feature);
		if (enabled) {
			g_object_set_data(G_OBJECT(menuitem), "feature-type", (gpointer)feature);

			g_signal_connect(G_OBJECT(menuitem), "activate",
					 G_CALLBACK(rco_call_protocol_feature_activate), cnnobj);
		} else {
			gtk_widget_set_sensitive(menuitem, FALSE);
		}
	}

	/* If the plugin accepts keystrokes include the keystrokes menu */
	if (remmina_protocol_widget_plugin_receives_keystrokes(REMMINA_PROTOCOL_WIDGET(cnnobj->proto))) {
		/* Get the registered keystrokes list */
		keystrokes = g_strsplit(remmina_pref.keystrokes, STRING_DELIMITOR, -1);

		/* Add a keystrokes submenu */
		menuitem = gtk_menu_item_new_with_label(_("Keystrokes"));
		submenu_keystrokes = GTK_MENU(gtk_menu_new());
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), GTK_WIDGET(submenu_keystrokes));
		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		/* Add each registered keystroke */
		if (g_strv_length(keystrokes)) {
			for (i = 0; i < g_strv_length(keystrokes); i++) {
				keystroke_values = g_strsplit(keystrokes[i], STRING_DELIMITOR2, -1);
				if (g_strv_length(keystroke_values) > 1) {
					/* Add the keystroke if no description was available */
					menuitem = gtk_menu_item_new_with_label(
						g_strdup(keystroke_values[strlen(keystroke_values[0]) ? 0 : 1]));
					g_object_set_data(G_OBJECT(menuitem), "keystrokes", g_strdup(keystroke_values[1]));
					g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
									G_CALLBACK(remmina_protocol_widget_send_keystrokes),
									REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
					gtk_widget_show(menuitem);
					gtk_menu_shell_append(GTK_MENU_SHELL(submenu_keystrokes), menuitem);
				}
				g_strfreev(keystroke_values);
			}
		}
		menuitem = gtk_menu_item_new_with_label(_("Send clipboard content as keystrokes"));
		static gchar k_tooltip[] =
			N_("CAUTION: Pasted text will be sent as a sequence of key-codes as if typed on your local keyboard.\n"
			"\n"
			"  • For best results use same keyboard settings for both, client and server.\n"
			"\n"
			"  • If client-keyboard is different from server-keyboard the received text can contain wrong or erroneous characters.\n"
			"\n"
			"  • Unicode characters and other special characters that can't be translated to local key-codes won’t be sent to the server.\n"
			"\n");
		gtk_widget_set_tooltip_text(menuitem, k_tooltip);
		gtk_menu_shell_append(GTK_MENU_SHELL(submenu_keystrokes), menuitem);
		g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
						G_CALLBACK(remmina_protocol_widget_send_clipboard),
						REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
		gtk_widget_show(menuitem);

		g_strfreev(keystrokes);
	}

	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(rcw_toolbar_tools_popdown), cnnwin);

#if GTK_CHECK_VERSION(3, 22, 0)
	gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(toggle),
				 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
#else
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, remmina_public_popup_position, widget, 0, gtk_get_current_event_time());
#endif
}

static void rcw_toolbar_duplicate(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	remmina_file_save(cnnobj->remmina_file);

	remmina_exec_command(REMMINA_COMMAND_CONNECT, cnnobj->remmina_file->filename);
}

static void rcw_toolbar_edit_pref(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	RemminaConnectionObject *cnnobj;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))
		|| cnnobj->remmina_file == NULL
		|| cnnobj->remmina_file->filename == NULL) return;
	GtkWidget* widget = remmina_file_editor_new_from_filename(cnnobj->remmina_file->filename);
	
	if (widget) {
		gtk_window_set_transient_for(GTK_WINDOW(widget), GTK_WINDOW(cnnobj->cnnwin));
		gtk_widget_show(widget);
	}
}

static void rcw_toolbar_screenshot(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	GdkPixbuf *screenshot;
	GdkWindow *active_window;
	cairo_t *cr;
	gint width, height;
	GString *pngstr;
	gchar *pngname;
	GtkWidget *dialog;
	RemminaProtocolWidget *gp;
	RemminaPluginScreenshotData rpsd;
	RemminaConnectionObject *cnnobj;
	cairo_surface_t *srcsurface;
	cairo_format_t cairo_format;
	cairo_surface_t *surface;
	int stride;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	GDateTime *date = g_date_time_new_now_utc();

	// We will take a screenshot of the currently displayed RemminaProtocolWidget.
	gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);

	gchar *denyclip = remmina_pref_get_value("deny_screenshot_clipboard");

	REMMINA_DEBUG("deny_screenshot_clipboard is set to %s", denyclip);

	GtkClipboard *c = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

	// Ask the plugin if it can give us a screenshot
	if (remmina_protocol_widget_plugin_screenshot(gp, &rpsd)) {
		// Good, we have a screenshot from the plugin !

		REMMINA_DEBUG("Screenshot from plugin: w=%d h=%d bpp=%d bytespp=%d\n",
			      rpsd.width, rpsd.height, rpsd.bitsPerPixel, rpsd.bytesPerPixel);

		width = rpsd.width;
		height = rpsd.height;

		if (rpsd.bitsPerPixel == 32)
			cairo_format = CAIRO_FORMAT_ARGB32;
		else if (rpsd.bitsPerPixel == 24)
			cairo_format = CAIRO_FORMAT_RGB24;
		else
			cairo_format = CAIRO_FORMAT_RGB16_565;

		stride = cairo_format_stride_for_width(cairo_format, width);

		srcsurface = cairo_image_surface_create_for_data(rpsd.buffer, cairo_format, width, height, stride);
		// Transfer the PixBuf in the main clipboard selection
		if (denyclip && (g_strcmp0(denyclip, "true")))
			gtk_clipboard_set_image(c, gdk_pixbuf_get_from_surface(
							srcsurface, 0, 0, width, height));
		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
		cr = cairo_create(surface);
		cairo_set_source_surface(cr, srcsurface, 0, 0);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(cr);
		cairo_surface_destroy(srcsurface);

		free(rpsd.buffer);
	} else {
		// The plugin is not releasing us a screenshot, just try to catch one via GTK

		/* Warn the user if image is distorted */
		if (cnnobj->plugin_can_scale &&
		    get_current_allowed_scale_mode(cnnobj, NULL, NULL) == REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED) {
			dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
							_("Turn off scaling to avoid screenshot distortion."));
			g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);
			gtk_widget_show(dialog);
		}

		// Get the screenshot.
		active_window = gtk_widget_get_window(GTK_WIDGET(gp));
		width = gdk_window_get_width(active_window);
		height = gdk_window_get_height(active_window);

		screenshot = gdk_pixbuf_get_from_window(active_window, 0, 0, width, height);
		if (screenshot == NULL)
			g_print("gdk_pixbuf_get_from_window failed\n");

		// Transfer the PixBuf in the main clipboard selection
		if (denyclip && (g_strcmp0(denyclip, "true")))
			gtk_clipboard_set_image(c, screenshot);
		// Prepare the destination Cairo surface.
		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
		cr = cairo_create(surface);

		// Copy the source pixbuf to the surface and paint it.
		gdk_cairo_set_source_pixbuf(cr, screenshot, 0, 0);
		cairo_paint(cr);

		// Deallocate screenshot pixbuf
		g_object_unref(screenshot);
	}

	//home/antenore/Pictures/remmina_%p_%h_%Y  %m %d-%H%M%S.png pngname
	//home/antenore/Pictures/remmina_st_  _2018 9 24-151958.240374.png

	pngstr = g_string_new(g_strdup_printf("%s/%s.png",
					      remmina_pref.screenshot_path,
					      remmina_pref.screenshot_name));
	remmina_utils_string_replace_all(pngstr, "%p",
					 remmina_file_get_string(cnnobj->remmina_file, "name"));
	remmina_utils_string_replace_all(pngstr, "%h",
					 remmina_file_get_string(cnnobj->remmina_file, "server"));
	remmina_utils_string_replace_all(pngstr, "%Y",
					 g_strdup_printf("%d", g_date_time_get_year(date)));
	remmina_utils_string_replace_all(pngstr, "%m", g_strdup_printf("%02d",
								       g_date_time_get_month(date)));
	remmina_utils_string_replace_all(pngstr, "%d",
					 g_strdup_printf("%02d", g_date_time_get_day_of_month(date)));
	remmina_utils_string_replace_all(pngstr, "%H",
					 g_strdup_printf("%02d", g_date_time_get_hour(date)));
	remmina_utils_string_replace_all(pngstr, "%M",
					 g_strdup_printf("%02d", g_date_time_get_minute(date)));
	remmina_utils_string_replace_all(pngstr, "%S",
					 g_strdup_printf("%02d", g_date_time_get_second(date)));
	g_date_time_unref(date);
	pngname = g_string_free(pngstr, FALSE);

	cairo_surface_write_to_png(surface, pngname);

	/* send a desktop notification */
	if (g_file_test(pngname, G_FILE_TEST_EXISTS))
		remmina_public_send_notification("remmina-screenshot-is-ready-id", _("Screenshot taken"), pngname);

	//Clean up and return.
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

static void rcw_toolbar_minimize(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;

	rcw_floating_toolbar_show(cnnwin, FALSE);
	gtk_window_iconify(GTK_WINDOW(cnnwin));
	rcw_iconify_widget_areas(cnnwin);
}

static void rcw_toolbar_disconnect(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	rco_disconnect_current_page(cnnobj);
}

static void rcw_toolbar_reconnect(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;
	if (cnnobj->connected){
		cnnobj->remmina_file= remmina_file_load(cnnobj->remmina_file->filename);
		rcw_toolbar_disconnect(toggle, cnnwin);
	}
	rcw_open_from_file(cnnobj->remmina_file);
}

static void rcw_toolbar_grab(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	gboolean capture;
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring)
		return;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	capture = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));

	if (cnnobj->connected){
		remmina_file_set_int(cnnobj->remmina_file, "keyboard_grab", capture);
	}

	rcw_keyboard_grab_request(cnnobj, capture && cnnobj->connected);

	rco_update_toolbar(cnnobj);
}

static void rcw_toolbar_invert_scroll(GtkToolItem *toggle, RemminaConnectionWindow *cnnwin)
{
	gboolean capture;
	RemminaConnectionObject *cnnobj;

	if (cnnwin->priv->toolbar_is_reconfiguring){
		return;
	}

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) {
		return;
	}

	capture = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(toggle));
	
	if (cnnobj->connected && cnnobj->proto) {
		remmina_file_set_int(cnnobj->remmina_file, "invert_scroll", capture);
	}
}

static void rcw_update_pin(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	if (cnnwin->priv->pin_down)
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(cnnwin->priv->pin_button), "org.remmina.Remmina-pin-down-symbolic");
	else
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(cnnwin->priv->pin_button), "org.remmina.Remmina-pin-up-symbolic");
}

static void rcw_toolbar_pin(GtkWidget *widget, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	remmina_pref.toolbar_pin_down = cnnwin->priv->pin_down = !cnnwin->priv->pin_down;
	remmina_pref_save();
	rcw_update_pin(cnnwin);
}

static GtkWidget *
rcw_create_toolbar(RemminaConnectionWindow *cnnwin, gint mode, gboolean is_floating)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj;
	GtkWidget *toolbar;
	GtkToolItem *toolitem;
	GtkWidget *widget;
	GtkWidget *arrow;

	GdkDisplay *display;
	gint n_monitors;

	display = gdk_display_get_default();
	n_monitors = gdk_display_get_n_monitors(display);

	cnnobj = rcw_get_visible_cnnobj(cnnwin);

	priv->toolbar_is_reconfiguring = TRUE;

	toolbar = gtk_toolbar_new();
	gtk_widget_show(toolbar);
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);

	/* Main actions */

	if (is_floating){
			/* The pin button */
		GtkToolItem* pinbutton = gtk_toggle_tool_button_new();
		gtk_widget_show(GTK_WIDGET(pinbutton));
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), pinbutton, -1);
	#if GTK_CHECK_VERSION(3, 20, 0)
		gtk_widget_set_focus_on_click(GTK_WIDGET(pinbutton), FALSE);
	#else
		gtk_button_set_focus_on_click(GTK_BUTTON(pinbutton), FALSE);
	#endif
		gtk_widget_set_name(GTK_WIDGET(pinbutton), "remmina-pin-button");
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(pinbutton), remmina_pref.toolbar_pin_down);
		g_signal_connect(G_OBJECT(pinbutton), "clicked", G_CALLBACK(rcw_toolbar_pin), cnnwin);
		priv->pin_button = GTK_WIDGET(pinbutton);
		priv->pin_down = remmina_pref.toolbar_pin_down;
		rcw_update_pin(cnnwin);
	}

	/* Menu */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "view-more-symbolic");
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(toolitem), _("_Menu"));
	gtk_tool_item_set_tooltip_text(toolitem, _("Menu"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_menu), cnnwin);
	priv->toolitem_menu = toolitem;

	/* Open Main window */
	toolitem = gtk_tool_button_new(NULL, "Open Remmina Main window");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-go-home-symbolic");
	gtk_tool_item_set_tooltip_text(toolitem, _("Open the Remmina main window"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_open_main), cnnwin);

	priv->toolitem_new = toolitem;

	/* Duplicate session */
	toolitem = gtk_tool_button_new(NULL, "Duplicate connection");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-duplicate-symbolic");
	gtk_tool_item_set_tooltip_text(toolitem, _("Duplicate current connection"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_duplicate), cnnwin);
	if (!cnnobj)
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);

	priv->toolitem_duplicate = toolitem;

	/* Separator */
	toolitem = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));

	/* Auto-Fit */
	toolitem = gtk_tool_button_new(NULL, NULL);
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-fit-window-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Resize the window to fit in remote resolution"),
			remmina_pref.shortcutkey_autofit, 0);
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_autofit), cnnwin);
	priv->toolitem_autofit = toolitem;
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));


	/* Fullscreen toggle */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-fullscreen-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Toggle fullscreen mode"),
			remmina_pref.shortcutkey_fullscreen, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	priv->toolitem_fullscreen = toolitem;
	if (kioskmode) {
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem), FALSE);
	} else {
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem), mode != SCROLLED_WINDOW_MODE);
		g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_fullscreen), cnnwin);
	}

	/* Fullscreen drop-down options */
	toolitem = gtk_tool_item_new();
	gtk_widget_show(GTK_WIDGET(toolitem));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	widget = gtk_toggle_button_new();
	gtk_widget_show(widget);
	gtk_container_set_border_width(GTK_CONTAINER(widget), 0);
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
#if GTK_CHECK_VERSION(3, 20, 0)
	gtk_widget_set_focus_on_click(GTK_WIDGET(widget), FALSE);
	if (remmina_pref.small_toolbutton)
		gtk_widget_set_name(widget, "remmina-small-button");

#else
	gtk_button_set_focus_on_click(GTK_BUTTON(widget), FALSE);
#endif
	gtk_container_add(GTK_CONTAINER(toolitem), widget);

#if GTK_CHECK_VERSION(3, 14, 0)
	arrow = gtk_image_new_from_icon_name("org.remmina.Remmina-pan-down-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
#else
	arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_NONE);
#endif
	gtk_widget_show(arrow);
	gtk_container_add(GTK_CONTAINER(widget), arrow);
	g_signal_connect(G_OBJECT(widget), "toggled", G_CALLBACK(rcw_toolbar_fullscreen_option), cnnwin);
	priv->fullscreen_option_button = widget;
	if (mode == SCROLLED_WINDOW_MODE)
		gtk_widget_set_sensitive(GTK_WIDGET(widget), FALSE);

	/* Multi monitor */
	if (n_monitors > 1) {
		toolitem = gtk_toggle_tool_button_new();
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-multi-monitor-symbolic");
		rcw_set_tooltip(GTK_WIDGET(toolitem), _("Multi monitor"),
				remmina_pref.shortcutkey_multimon, 0);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
		gtk_widget_show(GTK_WIDGET(toolitem));
		g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_multi_monitor_mode), cnnwin);
		priv->toolitem_multimon = toolitem;
		if (!cnnobj)
			gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
		else
			gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem),
							  remmina_file_get_int(cnnobj->remmina_file, "multimon", FALSE));
	}

	/* Dynamic Resolution Update */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-dynres-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Toggle dynamic resolution update"),
			remmina_pref.shortcutkey_dynres, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_dynres), cnnwin);
	priv->toolitem_dynres = toolitem;

	/* Scaler button */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-scale-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Toggle scaled mode"), remmina_pref.shortcutkey_scale, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_scaled_mode), cnnwin);
	priv->toolitem_scale = toolitem;

	/* View only mode */
	// Widget set_sensitive setting is determined in rco_update_toolbar by the plugin protocol
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-password-reveal-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("View only mode"), remmina_pref.shortcutkey_viewonly, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_set_name(GTK_WIDGET(toolitem), "viewonly");
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_viewonly_mode), cnnwin);
	priv->toolitem_viewonly = toolitem;

	/* Scaler aspect ratio dropdown menu */
	toolitem = gtk_tool_item_new();
	gtk_widget_show(GTK_WIDGET(toolitem));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	widget = gtk_toggle_button_new();
	gtk_widget_show(widget);
	gtk_container_set_border_width(GTK_CONTAINER(widget), 0);
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
#if GTK_CHECK_VERSION(3, 20, 0)
	gtk_widget_set_focus_on_click(GTK_WIDGET(widget), FALSE);
#else
	gtk_button_set_focus_on_click(GTK_BUTTON(widget), FALSE);
#endif
	if (remmina_pref.small_toolbutton)
		gtk_widget_set_name(widget, "remmina-small-button");
	gtk_container_add(GTK_CONTAINER(toolitem), widget);
#if GTK_CHECK_VERSION(3, 14, 0)
	arrow = gtk_image_new_from_icon_name("org.remmina.Remmina-pan-down-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
#else
	arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_NONE);
#endif
	gtk_widget_show(arrow);
	gtk_container_add(GTK_CONTAINER(widget), arrow);
	g_signal_connect(G_OBJECT(widget), "toggled", G_CALLBACK(rcw_toolbar_scaler_option), cnnwin);
	priv->scaler_option_button = widget;

	/* Separator */
	toolitem = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));

	/* Switch tabs */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-switch-page-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Switch tab pages"), remmina_pref.shortcutkey_prevtab,
			remmina_pref.shortcutkey_nexttab);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_switch_page), cnnwin);
	priv->toolitem_switch_page = toolitem;

	/* Grab keyboard button */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-keyboard-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Grab all keyboard events"),
			remmina_pref.shortcutkey_grab, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_grab), cnnwin);
	priv->toolitem_grab = toolitem;

	if (!cnnobj)
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
	else {
		const gchar *protocol = remmina_file_get_string(cnnobj->remmina_file, "protocol");
		if (g_strcmp0(protocol, "SFTP") == 0 || g_strcmp0(protocol, "SSH") == 0)
			gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
	}

	/* Invert Scroll button */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "input-mouse-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Invert scroll direction"),
			remmina_pref.shortcutkey_invert_scroll, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_invert_scroll), cnnwin);
	priv->toolitem_invert_scroll = toolitem;
	if (!cnnobj){
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
	}

	/* Preferences */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-preferences-system-symbolic");
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(toolitem), _("_Preferences"));
	gtk_tool_item_set_tooltip_text(toolitem, _("Preferences"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_preferences), cnnwin);
	priv->toolitem_preferences = toolitem;

	/* Tools */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-system-run-symbolic");
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(toolitem), _("_Tools"));
	gtk_tool_item_set_tooltip_text(toolitem, _("Tools"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_tools), cnnwin);
	priv->toolitem_tools = toolitem;

	/* Credentials */
	toolitem = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-insert-object-symbolic");
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(toolitem), _("_Insert"));
	gtk_tool_item_set_tooltip_text(toolitem, _("Insert"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "toggled", G_CALLBACK(rcw_toolbar_creds), cnnwin);
	priv->toolitem_creds = toolitem;

	/* Separator */
	toolitem = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));

	toolitem = gtk_tool_button_new(NULL, "_Screenshot");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-camera-photo-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Screenshot"), remmina_pref.shortcutkey_screenshot, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_screenshot), cnnwin);
	priv->toolitem_screenshot = toolitem;


	toolitem = gtk_tool_button_new(NULL, "_Edit_pref");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-document-properties-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Edit Preferences"), remmina_pref.shortcutkey_screenshot, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_edit_pref), cnnwin);
	priv->toolitem_edit_pref = toolitem;

	/* Separator */
	toolitem = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));

	/* Minimize */
	toolitem = gtk_tool_button_new(NULL, "_Bottom");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-go-bottom-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Minimize window"), remmina_pref.shortcutkey_minimize, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_minimize), cnnwin);
	if (kioskmode)
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);

	/* Reconnect */
	toolitem = gtk_tool_button_new(NULL, "_Reconnect");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-reconnect-symbolic");
	gtk_widget_set_tooltip_text(GTK_WIDGET(toolitem), _("Reconnect"));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_reconnect), cnnwin);

	/* Disconnect */
	toolitem = gtk_tool_button_new(NULL, "_Disconnect");
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(toolitem), "org.remmina.Remmina-disconnect-symbolic");
	rcw_set_tooltip(GTK_WIDGET(toolitem), _("Disconnect"), remmina_pref.shortcutkey_disconnect, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolitem, -1);
	gtk_widget_show(GTK_WIDGET(toolitem));
	g_signal_connect(G_OBJECT(toolitem), "clicked", G_CALLBACK(rcw_toolbar_disconnect), cnnwin);

	priv->toolbar_is_reconfiguring = FALSE;
	return toolbar;
}

static void rcw_place_toolbar(GtkToolbar *toolbar, GtkGrid *grid, GtkWidget *sibling, int toolbar_placement)
{
	/* Place the toolbar inside the grid and set its orientation */

	if (toolbar_placement == TOOLBAR_PLACEMENT_LEFT || toolbar_placement == TOOLBAR_PLACEMENT_RIGHT)
		gtk_orientable_set_orientation(GTK_ORIENTABLE(toolbar), GTK_ORIENTATION_VERTICAL);
	else
		gtk_orientable_set_orientation(GTK_ORIENTABLE(toolbar), GTK_ORIENTATION_HORIZONTAL);


	switch (toolbar_placement) {
	case TOOLBAR_PLACEMENT_TOP:
		gtk_widget_set_hexpand(GTK_WIDGET(toolbar), TRUE);
		gtk_widget_set_vexpand(GTK_WIDGET(toolbar), FALSE);
		gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(toolbar), sibling, GTK_POS_TOP, 1, 1);
		break;
	case TOOLBAR_PLACEMENT_RIGHT:
		gtk_widget_set_vexpand(GTK_WIDGET(toolbar), TRUE);
		gtk_widget_set_hexpand(GTK_WIDGET(toolbar), FALSE);
		gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(toolbar), sibling, GTK_POS_RIGHT, 1, 1);
		break;
	case TOOLBAR_PLACEMENT_BOTTOM:
		gtk_widget_set_hexpand(GTK_WIDGET(toolbar), TRUE);
		gtk_widget_set_vexpand(GTK_WIDGET(toolbar), FALSE);
		gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(toolbar), sibling, GTK_POS_BOTTOM, 1, 1);
		break;
	case TOOLBAR_PLACEMENT_LEFT:
		gtk_widget_set_vexpand(GTK_WIDGET(toolbar), TRUE);
		gtk_widget_set_hexpand(GTK_WIDGET(toolbar), FALSE);
		gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(toolbar), sibling, GTK_POS_LEFT, 1, 1);
		break;
	}
}

static void rco_update_toolbar(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	GtkToolItem *toolitem;
	gboolean bval, dynres_avail, scale_avail, viewonly_avail;
	gboolean test_floating_toolbar;
	RemminaScaleMode scalemode;

	priv->toolbar_is_reconfiguring = TRUE;

	rco_update_toolbar_autofit_button(cnnobj);

	toolitem = priv->toolitem_switch_page;
	if (kioskmode)
		bval = FALSE;
	else
		bval = (gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook)) > 1);
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), bval);

	if (cnnobj->remmina_file->filename)
		gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_duplicate), TRUE);
	else
		gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_duplicate), FALSE);

	scalemode = get_current_allowed_scale_mode(cnnobj, &dynres_avail, &scale_avail);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_dynres), dynres_avail && cnnobj->connected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_scale), scale_avail && cnnobj->connected);

	viewonly_avail = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), REMMINA_PROTOCOL_FEATURE_TYPE_VIEWONLY);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_viewonly), viewonly_avail && cnnobj->connected);

	switch (scalemode) {
	case REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE:
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_dynres), FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_scale), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), FALSE);
		break;
	case REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED:
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_dynres), FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_scale), TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), TRUE && cnnobj->connected);
		break;
	case REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES:
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_dynres), TRUE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_scale), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), FALSE);
		break;
	}

	/* REMMINA_PROTOCOL_FEATURE_TYPE_MULTIMON */
	toolitem = priv->toolitem_multimon;
	if (toolitem) {
		gint hasmultimon = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
										 REMMINA_PROTOCOL_FEATURE_TYPE_MULTIMON);
		gboolean is_multimon = remmina_file_get_int(cnnobj->remmina_file, "multimon", FALSE) && hasmultimon;

		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), cnnobj->connected);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem), is_multimon);
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), hasmultimon);
		if (is_multimon) {
			gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_scale), FALSE);
			gtk_widget_set_sensitive(GTK_WIDGET(priv->scaler_option_button), FALSE);
		}
	}

	toolitem = priv->toolitem_grab;
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), cnnobj->connected);
	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem),
					  remmina_file_get_int(cnnobj->remmina_file, "keyboard_grab", FALSE));

	toolitem = priv->toolitem_invert_scroll;
	if (toolitem) {
		gint hasinvertscroll = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
										 REMMINA_PROTOCOL_FEATURE_TYPE_INVERT_SCROLL);
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), hasinvertscroll && cnnobj->connected);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem),
						  remmina_file_get_int(cnnobj->remmina_file, "invert_scroll", FALSE));
	}
	const gchar *protocol = remmina_file_get_string(cnnobj->remmina_file, "protocol");
	if (g_strcmp0(protocol, "SFTP") == 0 || g_strcmp0(protocol, "SSH") == 0) {
		gtk_widget_set_sensitive(GTK_WIDGET(toolitem), FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(toolitem), FALSE);
		remmina_file_set_int(cnnobj->remmina_file, "keyboard_grab", FALSE);
	}

	toolitem = priv->toolitem_preferences;
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), cnnobj->connected);
	bval = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
							     REMMINA_PROTOCOL_FEATURE_TYPE_PREF);
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), bval && cnnobj->connected);

	toolitem = priv->toolitem_tools;
	bval = remmina_protocol_widget_query_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
							     REMMINA_PROTOCOL_FEATURE_TYPE_TOOL);
	gtk_widget_set_sensitive(GTK_WIDGET(toolitem), bval && cnnobj->connected);

	gtk_widget_set_sensitive(GTK_WIDGET(priv->toolitem_screenshot), cnnobj->connected);

	if (cnnobj->remmina_file != NULL && cnnobj->remmina_file->filename != NULL) {
		gtk_widget_show(GTK_WIDGET(cnnobj->cnnwin->priv->toolitem_edit_pref));
	}

	gtk_window_set_title(GTK_WINDOW(cnnobj->cnnwin), remmina_file_get_string(cnnobj->remmina_file, "name"));

	test_floating_toolbar = (priv->floating_toolbar_widget != NULL);

	if (test_floating_toolbar) {
		const gchar *str = remmina_file_get_string(cnnobj->remmina_file, "name");
		const gchar *format;
		GdkRGBA rgba;
		gchar *bg;

		bg = g_strdup(remmina_pref.grab_color);
		if (!gdk_rgba_parse(&rgba, bg)) {
			REMMINA_DEBUG("%s cannot be parsed as a color", bg);
			g_free(bg);
			bg = g_strdup("#00FF00");
		} else {
			REMMINA_DEBUG("Using %s as background color", bg);
		}

// gtk_widget_override_background_color is deprecated, and this will be handled with GTK4 migration
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		if (remmina_file_get_int(cnnobj->remmina_file, "keyboard_grab", FALSE)) {
			if (remmina_pref_get_boolean("grab_color_switch")) {
				gtk_widget_override_background_color(priv->overlay_ftb_fr, GTK_STATE_FLAG_NORMAL, &rgba);
				format = g_strconcat("<span bgcolor=\"", bg, "\" size=\"large\"><b>(G: ON) - \%s</b></span>", NULL);
			} else {
				gtk_widget_override_background_color(priv->overlay_ftb_fr, GTK_STATE_FLAG_NORMAL, NULL);
				format = "<big><b>(G: ON) - \%s</b></big>";
			}
		} else {
			gtk_widget_override_background_color(priv->overlay_ftb_fr, GTK_STATE_FLAG_NORMAL, NULL);
			format = "<big><b>(G:OFF) - \%s</b></big>";
		}
		gchar *markup;
#pragma clang diagnostic pop

		markup = g_markup_printf_escaped(format, str);
		gtk_label_set_markup(GTK_LABEL(priv->floating_toolbar_label), markup);
		g_free(markup);
		g_free(bg);
	}

	priv->toolbar_is_reconfiguring = FALSE;
}

static void rcw_set_toolbar_visibility(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	if (priv->view_mode == SCROLLED_WINDOW_MODE) {
		if (remmina_pref.hide_connection_toolbar)
			gtk_widget_hide(priv->toolbar);
		else
			gtk_widget_show(priv->toolbar);
	}
}

#if DEBUG_KB_GRABBING
static void print_crossing_event(GdkEventCrossing *event) {
	const char*event_name;
	
	switch (event->detail) {
	case GDK_NOTIFY_ANCESTOR: event_name="GDK_NOTIFY_ANCESTOR"; break;
	case GDK_NOTIFY_VIRTUAL: event_name="GDK_NOTIFY_VIRTUAL"; break;
	case GDK_NOTIFY_NONLINEAR: event_name="GDK_NOTIFY_NONLINEAR"; break;
	case GDK_NOTIFY_NONLINEAR_VIRTUAL: event_name="GDK_NOTIFY_NONLINEAR_VIRTUAL"; break;
	case GDK_NOTIFY_UNKNOWN: event_name="GDK_NOTIFY_UNKNOWN"; break;
	case GDK_NOTIFY_INFERIOR: event_name="GDK_NOTIFY_INFERIOR"; break;
	default: event_name="unknown";
	}
	REMMINA_DEBUG("DEBUG_KB_GRABBING: --- Crossing event detail: %s", event_name);
	switch (event->mode) {
	case GDK_CROSSING_NORMAL: event_name="GDK_CROSSING_NORMAL"; break;
	case GDK_CROSSING_GRAB: event_name="GDK_CROSSING_GRAB"; break;
	case GDK_CROSSING_UNGRAB: event_name="GDK_CROSSING_UNGRAB"; break;
	case GDK_CROSSING_GTK_GRAB: event_name="GDK_CROSSING_GTK_GRAB"; break;
	case GDK_CROSSING_GTK_UNGRAB: event_name="GDK_CROSSING_GTK_UNGRAB"; break;
	case GDK_CROSSING_STATE_CHANGED: event_name="GDK_CROSSING_STATE_CHANGED"; break;
	case GDK_CROSSING_TOUCH_BEGIN: event_name="GDK_CROSSING_TOUCH_BEGIN"; break;
	case GDK_CROSSING_TOUCH_END: event_name="GDK_CROSSING_TOUCH_END"; break;
	case GDK_CROSSING_DEVICE_SWITCH: event_name="GDK_CROSSING_DEVICE_SWITCH"; break;
	default: event_name="unknown";
	}
	REMMINA_DEBUG("DEBUG_KB_GRABBING: --- Crossing event mode=%s", event_name);
}
#endif

static gboolean toolbar_display(gpointer user_data){
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow*) user_data;
	if (should_display){
		rcw_floating_toolbar_show(cnnwin, should_display);
	}
	return G_SOURCE_REMOVE;

}

static gboolean rcw_floating_toolbar_on_enter(GtkWidget *widget, GdkEventCrossing *event,
					      RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	GtkStyleContext *style_context = gtk_widget_get_style_context(cnnwin->priv->overlay_ftb_fr);
	GtkBorder margin;
	gtk_style_context_get_margin(style_context, gtk_style_context_get_state(style_context), &margin);
	if (cnnwin->priv->ftb_move_horiz >= 0 && event->x < cnnwin->priv->ftb_move_horiz + margin.left) {
		// the toolbar must not pop (the mouse pointer is not over the visible area, but on the left)
		return TRUE;
	}
	if (remmina_pref.fullscreen_toolbar_delay > 0){
		should_display = TRUE;
		g_timeout_add(remmina_pref.fullscreen_toolbar_delay * 1000, G_SOURCE_FUNC(toolbar_display), cnnwin);
	}
	else{
		rcw_floating_toolbar_show(cnnwin, TRUE);
	}



	return TRUE;
}

static gboolean rcw_floating_toolbar_on_leave(GtkWidget *widget, GdkEventCrossing *event,
					      RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	if (event->detail != GDK_NOTIFY_INFERIOR){
		rcw_floating_toolbar_show(cnnwin, FALSE);
		should_display = FALSE;
	}

	return TRUE;
}


static gboolean rcw_on_enter_notify_event(GtkWidget *widget, GdkEventCrossing *event,
					  gpointer user_data)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: enter-notify-event on rcw received");
	print_crossing_event(event);
#endif
	return FALSE;
}



static gboolean rcw_on_leave_notify_event(GtkWidget *widget, GdkEventCrossing *event,
					  gpointer user_data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)widget;

#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: leave-notify-event on rcw received");
	print_crossing_event(event);
#endif

	if (event->mode != GDK_CROSSING_NORMAL && event->mode != GDK_CROSSING_UNGRAB) {
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: ignored because mode is not GDK_CROSSING_NORMAL GDK_CROSSING_UNGRAB");
#endif
		return FALSE;
	}

	if (cnnwin->priv->delayed_grab_eventsourceid) {
		g_source_remove(cnnwin->priv->delayed_grab_eventsourceid);
		cnnwin->priv->delayed_grab_eventsourceid = 0;
	}

	RemminaConnectionObject* cnnobj = rcw_get_visible_cnnobj(cnnwin);
	rcw_keyboard_grab_request(cnnobj, event->mode == GDK_CROSSING_UNGRAB);

	return FALSE;
}


static gboolean rco_leave_protocol_widget(GtkWidget *widget, GdkEventCrossing *event,
					  RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);

#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: received leave event on RCO.");
	print_crossing_event(event);
#endif

	if (cnnobj->cnnwin->priv->delayed_grab_eventsourceid) {
		g_source_remove(cnnobj->cnnwin->priv->delayed_grab_eventsourceid);
		cnnobj->cnnwin->priv->delayed_grab_eventsourceid = 0;
	}

	/* Ungrab only if the leave is due to normal mouse motion and not to an inferior */
	if (event->mode == GDK_CROSSING_NORMAL && event->detail != GDK_NOTIFY_INFERIOR)
		rcw_keyboard_grab_request(cnnobj, FALSE);
	if (event->mode == GDK_CROSSING_UNGRAB)
		rcw_keyboard_grab_request(cnnobj, FALSE);
		
	return FALSE;
}


static gboolean rco_enter_protocol_widget(GtkWidget *widget, GdkEventCrossing *event,
				   RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);

#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: enter on protocol widget event received");
	print_crossing_event(event);
#endif

	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	if (!priv->sticky && event->mode == GDK_CROSSING_NORMAL)
		rcw_floating_toolbar_show(cnnobj->cnnwin, FALSE);
	
	if (!cnnobj->connected) {
#if DEBUG_KB_GRABBING
		REMMINA_DEBUG("DEBUG_KB_GRABBING: Not connected yet, abort");
#endif
		return FALSE;
	}
	rcw_keyboard_grab_request(cnnobj, event->mode != GDK_CROSSING_UNGRAB);

	return FALSE;
}

static void rcw_focus_in(RemminaConnectionWindow *cnnwin)
{
	/* This function is the default signal handler for focus-in-event,
	 * but can also be called after a window focus state change event
	 * from rcw_state_event(). So expect to be called twice
	 * when cnnwin gains the focus */

	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	rcw_keyboard_grab_request(cnnobj, TRUE);
}

static void rcw_focus_out(RemminaConnectionWindow *cnnwin)
{
	/* This function is the default signal handler for focus-out-event,
	 * but can also be called after a window focus state change event
	 * from rcw_state_event(). So expect to be called twice
	 * when cnnwin loses the focus */

	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	rcw_keyboard_grab_request(cnnobj, FALSE);

	cnnwin->priv->hostkey_activated = FALSE;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (REMMINA_IS_SCROLLED_VIEWPORT(cnnobj->scrolled_container))
		remmina_scrolled_viewport_remove_motion(REMMINA_SCROLLED_VIEWPORT(cnnobj->scrolled_container));

	if (cnnobj->proto && cnnobj->scrolled_container)
		remmina_protocol_widget_call_feature_by_type(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
							     REMMINA_PROTOCOL_FEATURE_TYPE_UNFOCUS, 0);
}

static gboolean
rcw_floating_toolbar_hide(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;

	priv->hidetb_eventsource = 0;
	rcw_floating_toolbar_show(cnnwin, FALSE);
	return G_SOURCE_REMOVE;
}

static gboolean rcw_floating_toolbar_on_scroll(GtkWidget *widget, GdkEventScroll *event,
					       RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	int opacity;

	cnnobj = rcw_get_visible_cnnobj(cnnwin);
	if (!cnnobj)
		return TRUE;

	opacity = remmina_file_get_int(cnnobj->remmina_file, "toolbar_opacity", 0);
	switch (event->direction) {
	case GDK_SCROLL_UP:
		if (opacity > 0) {
			remmina_file_set_int(cnnobj->remmina_file, "toolbar_opacity", opacity - 1);
			rcw_update_toolbar_opacity(cnnwin);
			return TRUE;
		}
		break;
	case GDK_SCROLL_DOWN:
		if (opacity < TOOLBAR_OPACITY_LEVEL) {
			remmina_file_set_int(cnnobj->remmina_file, "toolbar_opacity", opacity + 1);
			rcw_update_toolbar_opacity(cnnwin);
			return TRUE;
		}
		break;
#if GTK_CHECK_VERSION(3, 4, 0)
	case GDK_SCROLL_SMOOTH:
		if (event->delta_y < 0 && opacity > 0) {
			remmina_file_set_int(cnnobj->remmina_file, "toolbar_opacity", opacity - 1);
			rcw_update_toolbar_opacity(cnnwin);
			return TRUE;
		}
		if (event->delta_y > 0 && opacity < TOOLBAR_OPACITY_LEVEL) {
			remmina_file_set_int(cnnobj->remmina_file, "toolbar_opacity", opacity + 1);
			rcw_update_toolbar_opacity(cnnwin);
			return TRUE;
		}
		break;
#endif
	default:
		break;
	}
	return TRUE;
}

static gboolean rcw_after_configure_scrolled(gpointer user_data)
{
	TRACE_CALL(__func__);
	gint width, height;
	GdkWindowState s;
	gint ipg, npages;
	RemminaConnectionWindow *cnnwin;

	cnnwin = (RemminaConnectionWindow *)user_data;

	if (!cnnwin || !cnnwin->priv)
		return FALSE;

	s = gdk_window_get_state(gtk_widget_get_window(GTK_WIDGET(cnnwin)));


	/* Changed window_maximize, window_width and window_height for all
	 * connections inside the notebook */
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(cnnwin->priv->notebook));
	for (ipg = 0; ipg < npages; ipg++) {
		RemminaConnectionObject *cnnobj;
		cnnobj = g_object_get_data(
			G_OBJECT(gtk_notebook_get_nth_page(GTK_NOTEBOOK(cnnwin->priv->notebook), ipg)),
			"cnnobj");
		if (s & GDK_WINDOW_STATE_MAXIMIZED) {
			remmina_file_set_int(cnnobj->remmina_file, "window_maximize", TRUE);
		} else {
			gtk_window_get_size(GTK_WINDOW(cnnobj->cnnwin), &width, &height);
			remmina_file_set_int(cnnobj->remmina_file, "window_width", width);
			remmina_file_set_int(cnnobj->remmina_file, "window_height", height);
			remmina_file_set_int(cnnobj->remmina_file, "window_maximize", FALSE);
		}
	}
	cnnwin->priv->acs_eventsourceid = 0;
	return FALSE;
}

static gboolean rcw_on_configure(GtkWidget *widget, GdkEventConfigure *event,
				 gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;
	RemminaConnectionObject *cnnobj;

	if (!REMMINA_IS_CONNECTION_WINDOW(widget))
		return FALSE;

	cnnwin = (RemminaConnectionWindow *)widget;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return FALSE;

	if (cnnwin->priv->acs_eventsourceid) {
		g_source_remove(cnnwin->priv->acs_eventsourceid);
		cnnwin->priv->acs_eventsourceid = 0;
	}

	if (gtk_widget_get_window(GTK_WIDGET(cnnwin))
	    && cnnwin->priv->view_mode == SCROLLED_WINDOW_MODE)
		/* Under GNOME Shell we receive this configure_event BEFORE a window
		 * is really unmaximized, so we must read its new state and dimensions
		 * later, not now */
		cnnwin->priv->acs_eventsourceid = g_timeout_add(500, rcw_after_configure_scrolled, cnnwin);

	if (cnnwin->priv->view_mode != SCROLLED_WINDOW_MODE)
		/* Notify window of change so that scroll border can be hidden or shown if needed */
		rco_check_resize(cnnobj);
	return FALSE;
}

static void rcw_create_floating_toolbar(RemminaConnectionWindow *cnnwin, gint mode)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	GtkWidget *ftb_widget;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *tb;


	/* A widget to be used for GtkOverlay for GTK >= 3.10 */
	ftb_widget = gtk_event_box_new();

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(vbox);

	gtk_container_add(GTK_CONTAINER(ftb_widget), vbox);

	tb = rcw_create_toolbar(cnnwin, mode, TRUE);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_show(hbox);

	label = gtk_label_new("");
	gtk_label_set_max_width_chars(GTK_LABEL(label), 50);
	gtk_widget_show(label);

	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

	priv->floating_toolbar_label = label;

	if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT) {
			if (remmina_pref.fullscreen_toolbar_visibility != FLOATING_TOOLBAR_VISIBILITY_MINIMAL){
				gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
				gtk_box_pack_start(GTK_BOX(vbox), tb, FALSE, FALSE, 0);
			}
			else{
				gtk_box_pack_start(GTK_BOX(vbox), tb, FALSE, FALSE, 0);
			}
	} else {
		if (remmina_pref.fullscreen_toolbar_visibility != FLOATING_TOOLBAR_VISIBILITY_MINIMAL){
			gtk_box_pack_start(GTK_BOX(vbox), tb, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
		}
		else{
			gtk_box_pack_start(GTK_BOX(vbox), tb, FALSE, FALSE, 0);
		}
	}

	priv->floating_toolbar_widget = ftb_widget;
	gtk_widget_show(ftb_widget);
}

static void rcw_toolbar_place_signal(RemminaConnectionWindow *cnnwin, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;

	priv = cnnwin->priv;
	/* Detach old toolbar widget and reattach in new position in the grid */
	if (priv->toolbar && priv->grid) {
		g_object_ref(priv->toolbar);
		gtk_container_remove(GTK_CONTAINER(priv->grid), priv->toolbar);
		rcw_place_toolbar(GTK_TOOLBAR(priv->toolbar), GTK_GRID(priv->grid), GTK_WIDGET(priv->notebook), remmina_pref.toolbar_placement);
		g_object_unref(priv->toolbar);
	}
}


static void rcw_init(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;

	priv = g_new0(RemminaConnectionWindowPriv, 1);
	cnnwin->priv = priv;

	priv->view_mode = SCROLLED_WINDOW_MODE;
	if (kioskmode && kioskmode == TRUE)
		priv->view_mode = VIEWPORT_FULLSCREEN_MODE;

	priv->multimon_areas = NULL;
	priv->multimon_main_monitor = -1;

	priv->floating_toolbar_opacity = 1.0;
	priv->grab_pending_status = FALSE;
	priv->fss_view_mode = VIEWPORT_FULLSCREEN_MODE;
	priv->ss_width = 640;
	priv->ss_height = 480;
	priv->ss_maximized = FALSE;

	priv->ftb_move_horiz = 0;
	priv->ftb_move_vert = 0;

	remmina_widget_pool_register(GTK_WIDGET(cnnwin));
}

static gboolean rcw_focus_in_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: RCW focus-in-event received");
#endif
	rcw_focus_in((RemminaConnectionWindow *)widget);
	return FALSE;
}

static gboolean rcw_focus_out_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: RCW focus-out-event received");
#endif
	rcw_focus_out((RemminaConnectionWindow *)widget);
	return FALSE;
}

#if DEBUG_KB_GRABBING
static void print_gdk_window_state_event(const char*message, GdkWindowState state)
{
	static const struct {
    GdkWindowState flag;
    const char *name;
	} gdk_window_state_enum[] = {
		{ GDK_WINDOW_STATE_WITHDRAWN, "GDK_WINDOW_STATE_WITHDRAWN" },
		{ GDK_WINDOW_STATE_ICONIFIED, "GDK_WINDOW_STATE_ICONIFIED" },
		{ GDK_WINDOW_STATE_MAXIMIZED, "GDK_WINDOW_STATE_MAXIMIZED" },
		{ GDK_WINDOW_STATE_STICKY,    "GDK_WINDOW_STATE_STICKY" },
		{ GDK_WINDOW_STATE_FULLSCREEN,"GDK_WINDOW_STATE_FULLSCREEN" },
		{ GDK_WINDOW_STATE_ABOVE,     "GDK_WINDOW_STATE_ABOVE" },
		{ GDK_WINDOW_STATE_BELOW,     "GDK_WINDOW_STATE_BELOW" },
		{ GDK_WINDOW_STATE_FOCUSED,   "GDK_WINDOW_STATE_FOCUSED" },
		{ GDK_WINDOW_STATE_TILED,     "GDK_WINDOW_STATE_TILED" },
		{ GDK_WINDOW_STATE_TOP_TILED, "GDK_WINDOW_STATE_TOP_TILED" },
		{ GDK_WINDOW_STATE_TOP_RESIZABLE,"GDK_WINDOW_STATE_TOP_RESIZABLE" },
		{ GDK_WINDOW_STATE_RIGHT_TILED,"GDK_WINDOW_STATE_RIGHT_TILED" },
		{ GDK_WINDOW_STATE_RIGHT_RESIZABLE,"GDK_WINDOW_STATE_RIGHT_RESIZABLE" },
		{ GDK_WINDOW_STATE_BOTTOM_TILED,"GDK_WINDOW_STATE_BOTTOM_TILED" },
		{ GDK_WINDOW_STATE_BOTTOM_RESIZABLE,"GDK_WINDOW_STATE_BOTTOM_RESIZABLE" },
		{ GDK_WINDOW_STATE_LEFT_TILED,"GDK_WINDOW_STATE_LEFT_TILED" },
		{ GDK_WINDOW_STATE_LEFT_RESIZABLE,"GDK_WINDOW_STATE_LEFT_RESIZABLE" }
	};
    int first = 1;
    size_t table_size = sizeof(gdk_window_state_enum) / sizeof(gdk_window_state_enum[0]);
	GString*state_str = g_string_new("");

    for (size_t i = 0; i < table_size; ++i) {
        if (state & gdk_window_state_enum[i].flag) {
            if (!first) state_str = g_string_append(state_str, "|");
            state_str = g_string_append(state_str, gdk_window_state_enum[i].name);
            first = 0;
        }
    }
    if (first) {
        state_str = g_string_append(state_str, "GDK_WINDOW_STATE_NONE");
    }
	REMMINA_DEBUG(message, state_str->str);
	g_string_free(state_str, TRUE);
}
#endif

static gboolean rcw_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)widget;

	if (!REMMINA_IS_CONNECTION_WINDOW(widget))
		return FALSE;

#if DEBUG_KB_GRABBING
	print_gdk_window_state_event("DEBUG_KB_GRABBING: changed_mask=%s", event->changed_mask);
	print_gdk_window_state_event("DEBUG_KB_GRABBING: new_window_state=%s", event->new_window_state);
#endif

	// This will trigger iconify/deiconify on other multimonitor windows
	if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED &&
		event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN &&
		event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) {
		rcw_iconify_widget_areas(cnnwin);
	} else if (event->changed_mask & GDK_WINDOW_STATE_FOCUSED &&
		       event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN &&
			   event->new_window_state & GDK_WINDOW_STATE_FOCUSED) {
 		// This is an ugly trick to keep the main window above even after lose focus every time (otherwise this works only one)
		gtk_window_set_keep_above(GTK_WINDOW(cnnwin), FALSE);
	    gtk_window_set_keep_above(GTK_WINDOW(cnnwin), TRUE);
		// restore other multimonitor windows if any
		rcw_restore_widget_areas(cnnwin);
	}
	if (event->changed_mask & GDK_WINDOW_STATE_FOCUSED) {
		if (event->new_window_state & GDK_WINDOW_STATE_FOCUSED)
			rcw_focus_in((RemminaConnectionWindow *)widget);
		else
			rcw_focus_out((RemminaConnectionWindow *)widget);
	}

	return FALSE;
}

static gboolean rcw_map_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	TRACE_CALL(__func__);



	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)widget;
	RemminaConnectionObject *cnnobj;
	RemminaProtocolWidget *gp;

	if (cnnwin->priv->toolbar_is_reconfiguring) return FALSE;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return FALSE;

	gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);
	REMMINA_DEBUG("Mapping: %s", gtk_widget_get_name(widget));
	if (remmina_protocol_widget_map_event(gp))
		REMMINA_DEBUG("Called plugin mapping function");
	return FALSE;
}

static gboolean rcw_unmap_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	TRACE_CALL(__func__);

	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)widget;
	RemminaConnectionObject *cnnobj;
	RemminaProtocolWidget *gp;

	if (cnnwin->priv->toolbar_is_reconfiguring) return FALSE;
	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return FALSE;

	gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);
	REMMINA_DEBUG("Unmapping: %s", gtk_widget_get_name(widget));
	if (remmina_protocol_widget_unmap_event(gp))
		REMMINA_DEBUG("Called plugin mapping function");
	return FALSE;
}

static gboolean rcw_map_event_fullscreen(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;
	gint target_monitor;

	REMMINA_DEBUG("Mapping: %s", gtk_widget_get_name(widget));

	if (!REMMINA_IS_CONNECTION_WINDOW(widget)) {
		REMMINA_DEBUG("Remmina Connection Window undefined, cannot go fullscreen");
		return FALSE;
	}

	cnnobj = rcw_get_visible_cnnobj((RemminaConnectionWindow *)widget);
	if (!cnnobj) {
		REMMINA_DEBUG("Remmina Connection Object undefined, cannot go fullscreen");
		return FALSE;
	}

	RemminaProtocolWidget *gp = REMMINA_PROTOCOL_WIDGET(cnnobj->proto);

	if (!gp)
		REMMINA_DEBUG("Remmina Protocol Widget undefined, cannot go fullscreen");

	if (remmina_protocol_widget_get_multimon(gp) >= 1) {
		REMMINA_DEBUG("Fullscreen on main monitor");
		gtk_window_fullscreen_on_monitor(GTK_WINDOW(widget), gtk_window_get_screen(GTK_WINDOW(widget)), cnnobj->cnnwin->priv->multimon_main_monitor);
		rcw_show_widget_areas(cnnobj->cnnwin);
	} else {
		REMMINA_DEBUG("Fullscreen on one monitor");

		target_monitor = GPOINTER_TO_INT(data);

#if GTK_CHECK_VERSION(3, 18, 0)
		if (remmina_pref.fullscreen_on_auto) {
			if (target_monitor == FULL_SCREEN_TARGET_MONITOR_UNDEFINED)
				gtk_window_fullscreen(GTK_WINDOW(widget));
			else
				gtk_window_fullscreen_on_monitor(GTK_WINDOW(widget), gtk_window_get_screen(GTK_WINDOW(widget)),
								target_monitor);
		} else {
			REMMINA_DEBUG("Fullscreen managed by WM or by the user, as per settings");
			gtk_window_fullscreen(GTK_WINDOW(widget));
		}
#else
		REMMINA_DEBUG("Cannot fullscreen on a specific monitor, feature available from GTK 3.18");
		gtk_window_fullscreen(GTK_WINDOW(widget));
#endif
	}

	if (remmina_protocol_widget_map_event(gp))
		REMMINA_DEBUG("Called plugin mapping function");

	return FALSE;
}

static RemminaConnectionWindow *
rcw_new(gboolean fullscreen, int full_screen_target_monitor)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;

	cnnwin = RCW(g_object_new(REMMINA_TYPE_CONNECTION_WINDOW, NULL));
	cnnwin->priv->on_delete_confirm_mode = RCW_ONDELETE_CONFIRM_IF_2_OR_MORE;
	cnnwin->priv->multimon_main_monitor = full_screen_target_monitor;

	if (fullscreen)
		/* Put the window in fullscreen after it is mapped to have it appear on the same monitor */
		g_signal_connect(G_OBJECT(cnnwin), "map-event", G_CALLBACK(rcw_map_event_fullscreen), GINT_TO_POINTER(full_screen_target_monitor));
	else
		g_signal_connect(G_OBJECT(cnnwin), "map-event", G_CALLBACK(rcw_map_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "unmap-event", G_CALLBACK(rcw_unmap_event), NULL);

	gtk_container_set_border_width(GTK_CONTAINER(cnnwin), 0);
	g_signal_connect(G_OBJECT(cnnwin), "toolbar-place", G_CALLBACK(rcw_toolbar_place_signal), NULL);

	g_signal_connect(G_OBJECT(cnnwin), "delete-event", G_CALLBACK(rcw_delete_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "destroy", G_CALLBACK(rcw_destroy), NULL);

	/* Under Xorg focus-in-event and focus-out-event don’t work when keyboard is grabbed
	 * via gdk_device_grab. So we listen for window-state-event to detect focus in and focus out.
	 * But we must also listen focus-in-event and focus-out-event because some
	 * window managers missing _NET_WM_STATE_FOCUSED hint, does not update the window state
	 * in case of focus change */
	g_signal_connect(G_OBJECT(cnnwin), "window-state-event", G_CALLBACK(rcw_state_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "focus-in-event", G_CALLBACK(rcw_focus_in_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "focus-out-event", G_CALLBACK(rcw_focus_out_event), NULL);

	g_signal_connect(G_OBJECT(cnnwin), "enter-notify-event", G_CALLBACK(rcw_on_enter_notify_event), NULL);
	g_signal_connect(G_OBJECT(cnnwin), "leave-notify-event", G_CALLBACK(rcw_on_leave_notify_event), NULL);


	g_signal_connect(G_OBJECT(cnnwin), "configure_event", G_CALLBACK(rcw_on_configure), NULL);

	return cnnwin;
}

/* This function will be called for the first connection. A tag is set to the window so that
 * other connections can determine if whether a new tab should be append to the same window
 */
static void rcw_update_tag(RemminaConnectionWindow *cnnwin, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	gchar *tag;

	switch (remmina_pref.tab_mode) {
	case REMMINA_TAB_BY_GROUP:
		tag = g_strdup(remmina_file_get_string(cnnobj->remmina_file, "group"));
		break;
	case REMMINA_TAB_BY_PROTOCOL:
		tag = g_strdup(remmina_file_get_string(cnnobj->remmina_file, "protocol"));
		break;
	default:
		tag = NULL;
		break;
	}
	g_object_set_data_full(G_OBJECT(cnnwin), "tag", tag, (GDestroyNotify)g_free);
}

void rcw_grab_focus(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	if (!(cnnobj = rcw_get_visible_cnnobj(cnnwin))) return;

	if (GTK_IS_WIDGET(cnnobj->proto))
		remmina_protocol_widget_grab_focus(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
}

static GtkWidget *nb_find_page_by_cnnobj(GtkNotebook *notebook, RemminaConnectionObject *cnnobj)
{
	gint i, np;
	GtkWidget *found_page, *pg;

	if (cnnobj == NULL || cnnobj->cnnwin == NULL || cnnobj->cnnwin->priv == NULL)
		return NULL;
	found_page = NULL;
	np = gtk_notebook_get_n_pages(cnnobj->cnnwin->priv->notebook);
	for (i = 0; i < np; i++) {
		pg = gtk_notebook_get_nth_page(cnnobj->cnnwin->priv->notebook, i);
		if (g_object_get_data(G_OBJECT(pg), "cnnobj") == cnnobj) {
			found_page = pg;
			break;
		}
	}

	return found_page;
}


static void rco_closewin(RemminaProtocolWidget *gp)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;
	GtkWidget *page_to_remove;
	should_display = FALSE;

	if (!cnnobj || !cnnobj->cnnwin || !G_IS_OBJECT(cnnobj->cnnwin)) {
	    REMMINA_WARNING("Invalid cnnobj or cnnwin in rco_closewin");
	    return;
	}

	if (cnnobj && cnnobj->scrolled_container && REMMINA_IS_SCROLLED_VIEWPORT(cnnobj->scrolled_container)) {
		REMMINA_DEBUG("deleting motion");
		remmina_scrolled_viewport_remove_motion(REMMINA_SCROLLED_VIEWPORT(cnnobj->scrolled_container));
	}

	if (cnnobj && cnnobj->cnnwin) {
		page_to_remove = nb_find_page_by_cnnobj(cnnobj->cnnwin->priv->notebook, cnnobj);
		if (page_to_remove) {
			gtk_notebook_remove_page(
				cnnobj->cnnwin->priv->notebook,
				gtk_notebook_page_num(cnnobj->cnnwin->priv->notebook, page_to_remove));
			/* Invalidate pointers to objects destroyed by page removal */
			cnnobj->aspectframe = NULL;
			cnnobj->viewport = NULL;
			cnnobj->scrolled_container = NULL;
			/* we cannot invalidate cnnobj->proto, because it can be already been
			 * detached from the widget hierarchy in rco_on_disconnect() */
		}
	}
	if (cnnobj) {
		cnnobj->remmina_file = NULL;
		g_free(cnnobj);
		gp->cnnobj = NULL;
	}

	remmina_application_condexit(REMMINA_CONDEXIT_ONDISCONNECT);
}

static void rco_on_close_button_clicked(GtkButton *button, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	if (REMMINA_IS_PROTOCOL_WIDGET(cnnobj->proto)) {
		if (!remmina_protocol_widget_is_closed((RemminaProtocolWidget *)cnnobj->proto))
			remmina_protocol_widget_close_connection(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
		else
			rco_closewin((RemminaProtocolWidget *)cnnobj->proto);
	}
}

static void rco_on_detach_tab_activate(GtkMenuItem *menuitem, RemminaConnectionObject *cnnobj)
{
	RemminaConnectionWindow *newcnnwin;
	GtkNotebook *old_notebook, *new_notebook;
	gint current_page_index;
	GtkWidget *tab_content;

	old_notebook = GTK_NOTEBOOK(cnnobj->cnnwin->priv->notebook);
	tab_content = nb_find_page_by_cnnobj(old_notebook, cnnobj);
	if (!tab_content){
		return;
	} 
	current_page_index = gtk_notebook_page_num(old_notebook, tab_content);

	g_object_ref(tab_content);
	gtk_notebook_remove_page(old_notebook, current_page_index);

	newcnnwin = rcw_create_scrolled(0, 0, FALSE);
	new_notebook = GTK_NOTEBOOK(newcnnwin->priv->notebook);

	GtkWidget *label = rco_create_tab_label(cnnobj);
	gtk_notebook_append_page(new_notebook, tab_content, label);
	gtk_notebook_set_tab_reorderable(new_notebook, tab_content, TRUE);
	gtk_notebook_set_tab_detachable(new_notebook, tab_content, TRUE);
	gtk_widget_set_can_focus(gtk_widget_get_parent(label), FALSE);
	g_object_unref(tab_content);

	cnnobj->cnnwin = newcnnwin;
	gtk_widget_show_all(GTK_WIDGET(newcnnwin));
}

static gboolean rco_on_tab_label_button_press(GtkWidget *widget, GdkEventButton *event, RemminaConnectionObject *cnnobj)
{
	GtkWidget *menu, *menuitem;

	if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY) {
		menu = gtk_menu_new();
		menuitem = gtk_menu_item_new_with_label(_("Detach Tab"));
		g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(rco_on_detach_tab_activate), cnnobj);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		gtk_widget_show_all(menu);
		gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
		return TRUE;
	}
	return FALSE;
}

static GtkWidget *rco_create_tab_label(RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	GtkWidget *hbox;
	GtkWidget *widget;
	GtkWidget *button;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_show(hbox);

	widget = gtk_image_new_from_icon_name(remmina_file_get_icon_name(cnnobj->remmina_file), GTK_ICON_SIZE_MENU);
	gtk_widget_show(widget);
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);

	gchar* label = g_strdup(remmina_file_get_string(cnnobj->remmina_file, "name"));
	if (strlen(label) > 100){
		label[99] = 0;
	}
	widget = gtk_label_new(label);
	g_free(label);

	gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
	gtk_widget_set_halign(widget, GTK_ALIGN_CENTER);

	gtk_widget_show(widget);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	button = gtk_button_new();      // The "x" to close the tab
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
#if GTK_CHECK_VERSION(3, 20, 0)
	gtk_widget_set_focus_on_click(GTK_WIDGET(widget), FALSE);
#else
	gtk_button_set_focus_on_click(GTK_BUTTON(button), FALSE);
#endif
	gtk_widget_set_name(button, "remmina-small-button");
	gtk_widget_show(button);

	widget = gtk_image_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
	gtk_widget_show(widget);
	gtk_container_add(GTK_CONTAINER(button), widget);

	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);

	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(rco_on_close_button_clicked), cnnobj);

	GtkWidget *event_box = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), FALSE);
	gtk_container_add(GTK_CONTAINER(event_box), hbox);
	gtk_widget_show(event_box);
	g_signal_connect(G_OBJECT(event_box), "button-press-event", G_CALLBACK(rco_on_tab_label_button_press), cnnobj);

	return event_box;
}

static GtkWidget *rco_create_tab_page(RemminaConnectionObject *cnnobj)
{
	GtkWidget *page;

	page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(page, "remmina-tab-page");


	return page;
}

static GtkWidget *rcw_append_new_page(RemminaConnectionWindow *cnnwin, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);
	GtkWidget *page, *label;
	GtkNotebook *notebook;

	notebook = cnnwin->priv->notebook;

	page = rco_create_tab_page(cnnobj);
	g_object_set_data(G_OBJECT(page), "cnnobj", cnnobj);
	label = rco_create_tab_label(cnnobj);

	cnnobj->cnnwin = cnnwin;

	gtk_notebook_append_page(notebook, page, label);
	gtk_notebook_set_tab_reorderable(notebook, page, TRUE);
	gtk_notebook_set_tab_detachable(notebook, page, TRUE);
	/* This trick prevents the tab label from being focused */
	gtk_widget_set_can_focus(gtk_widget_get_parent(label), FALSE);

	if (gtk_widget_get_parent(cnnobj->scrolled_container) != NULL)
		printf("REMMINA WARNING in %s: scrolled_container already has a parent\n", __func__);
	gtk_box_pack_start(GTK_BOX(page), cnnobj->scrolled_container, TRUE, TRUE, 0);

	gtk_widget_show(page);

	return page;
}


static void rcw_update_notebook(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	GtkNotebook *notebook;
	gint n;

	notebook = GTK_NOTEBOOK(cnnwin->priv->notebook);

	switch (cnnwin->priv->view_mode) {
	case SCROLLED_WINDOW_MODE:
		n = gtk_notebook_get_n_pages(notebook);
		gtk_notebook_set_show_tabs(notebook, remmina_pref.always_show_tab ? TRUE : n > 1);
		gtk_notebook_set_show_border(notebook, remmina_pref.always_show_tab ? TRUE : n > 1);
		break;
	default:
		gtk_notebook_set_show_tabs(notebook, FALSE);
		gtk_notebook_set_show_border(notebook, FALSE);
		break;
	}
}

static gboolean rcw_on_switch_page_finalsel(gpointer user_data)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv;
	RemminaConnectionObject *cnnobj;

	if (!user_data)
		return FALSE;

	cnnobj = (RemminaConnectionObject *)user_data;
	if (!cnnobj->cnnwin)
		return FALSE;

	priv = cnnobj->cnnwin->priv;

	if (GTK_IS_WIDGET(cnnobj->cnnwin)) {
		rcw_floating_toolbar_show(cnnobj->cnnwin, TRUE);
		if (!priv->hidetb_eventsource)
			priv->hidetb_eventsource = g_timeout_add(TB_HIDE_TIME_TIME, (GSourceFunc)
								 rcw_floating_toolbar_hide, cnnobj->cnnwin);
		rco_update_toolbar(cnnobj);
		rcw_grab_focus(cnnobj->cnnwin);
		if (priv->view_mode != SCROLLED_WINDOW_MODE)
			rco_check_resize(cnnobj);
	}
	priv->spf_eventsourceid = 0;
	return FALSE;
}

static void rcw_on_switch_page(GtkNotebook *notebook, GtkWidget *newpage, guint page_num,
			       RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindowPriv *priv = cnnwin->priv;
	RemminaConnectionObject *cnnobj_newpage;

	cnnobj_newpage = g_object_get_data(G_OBJECT(newpage), "cnnobj");
	if (priv->spf_eventsourceid)
		g_source_remove(priv->spf_eventsourceid);
	priv->spf_eventsourceid = g_idle_add(rcw_on_switch_page_finalsel, cnnobj_newpage);
}

static void rcw_on_page_added(GtkNotebook *notebook, GtkWidget *child, guint page_num,
			      RemminaConnectionWindow *cnnwin)
{
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(cnnwin->priv->notebook)) > 0)
		rcw_update_notebook(cnnwin);
}

static void rcw_on_page_removed(GtkNotebook *notebook, GtkWidget *child, guint page_num,
				RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(cnnwin->priv->notebook)) <= 0)  {
		rcw_dispose_widget_areas(cnnwin->priv);
		gtk_widget_destroy(GTK_WIDGET(cnnwin));
	}
}

static GtkNotebook *
rcw_on_notebook_create_window(GtkNotebook *notebook, GtkWidget *page, gint x, gint y, gpointer data)
{
	/* This signal callback is called by GTK when a detachable tab is dropped on the root window
	 * or in an existing window */

	TRACE_CALL(__func__);
	RemminaConnectionWindow *srccnnwin;
	RemminaConnectionWindow *dstcnnwin;
	RemminaConnectionObject *cnnobj;
	GdkWindow *window;
	gchar *srctag;
	gint width, height;

#if GTK_CHECK_VERSION(3, 20, 0)
	GdkSeat *seat;
#else
	GdkDeviceManager *manager;
#endif
	GdkDevice *device = NULL;

#if GTK_CHECK_VERSION(3, 20, 0)
	seat = gdk_display_get_default_seat(gdk_display_get_default());
	device = gdk_seat_get_pointer(seat);
#else
	manager = gdk_display_get_device_manager(gdk_display_get_default());
	device = gdk_device_manager_get_client_pointer(manager);
#endif

	window = gdk_device_get_window_at_position(device, &x, &y);
	srccnnwin = RCW(gtk_widget_get_toplevel(GTK_WIDGET(notebook)));
	dstcnnwin = RCW(remmina_widget_pool_find_by_window(REMMINA_TYPE_CONNECTION_WINDOW, window));

	if (srccnnwin == dstcnnwin)
		return NULL;

	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(srccnnwin->priv->notebook)) == 1 && !dstcnnwin)
		return NULL;

	cnnobj = (RemminaConnectionObject *)g_object_get_data(G_OBJECT(page), "cnnobj");

	if (!dstcnnwin) {
		/* Drop is directed to a new rcw: create a new scrolled window to accommodate
		 * the dropped connectionand move our cnnobj there. Width and
		 * height of the new window are cloned from the current window */
		srctag = (gchar *)g_object_get_data(G_OBJECT(srccnnwin), "tag");
		gtk_window_get_size(GTK_WINDOW(srccnnwin), &width, &height);
		dstcnnwin = rcw_create_scrolled(width, height, FALSE);  // New dropped window is never maximized
		g_object_set_data_full(G_OBJECT(dstcnnwin), "tag", g_strdup(srctag), (GDestroyNotify)g_free);
		/* when returning, GTK will move the whole tab to the new notebook.
		 * Prepare cnnobj to be hosted in the new cnnwin */
		cnnobj->cnnwin = dstcnnwin;
	} else {
		cnnobj->cnnwin = dstcnnwin;
	}

	remmina_protocol_widget_set_hostkey_func(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
						 (RemminaHostkeyFunc)rcw_hostkey_func);

	return GTK_NOTEBOOK(cnnobj->cnnwin->priv->notebook);
}

static GtkNotebook *
rcw_create_notebook(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	GtkNotebook *notebook;

	notebook = GTK_NOTEBOOK(gtk_notebook_new());
	gtk_notebook_set_group_name(notebook, "remmina-rcw-group");

	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_widget_show(GTK_WIDGET(notebook));

	g_signal_connect(G_OBJECT(notebook), "create-window", G_CALLBACK(rcw_on_notebook_create_window), NULL);
	g_signal_connect(G_OBJECT(notebook), "switch-page", G_CALLBACK(rcw_on_switch_page), cnnwin);
	g_signal_connect(G_OBJECT(notebook), "page-added", G_CALLBACK(rcw_on_page_added), cnnwin);
	g_signal_connect(G_OBJECT(notebook), "page-removed", G_CALLBACK(rcw_on_page_removed), cnnwin);
	gtk_widget_set_can_focus(GTK_WIDGET(notebook), FALSE);

	return notebook;
}

/* Create a scrolled toplevel window */
static RemminaConnectionWindow *rcw_create_scrolled(gint width, gint height, gboolean maximize)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;
	GtkWidget *grid;
	GtkWidget *toolbar;
	GtkNotebook *notebook;

	cnnwin = rcw_new(FALSE, 0);
	gtk_widget_set_name(GTK_WIDGET(cnnwin), "remmina-connection-window-scrolled");
	gtk_widget_realize(GTK_WIDGET(cnnwin));

	gtk_window_set_default_size(GTK_WINDOW(cnnwin), width, height);
	remmina_theme_apply();

	/* Create the toolbar */
	toolbar = rcw_create_toolbar(cnnwin, SCROLLED_WINDOW_MODE, FALSE);

	/* Create the notebook */
	notebook = rcw_create_notebook(cnnwin);

	/* Create the grid container for toolbars+notebook and populate it */
	grid = gtk_grid_new();


	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(notebook), 0, 0, 1, 1);

	gtk_widget_set_hexpand(GTK_WIDGET(notebook), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(notebook), TRUE);

	rcw_place_toolbar(GTK_TOOLBAR(toolbar), GTK_GRID(grid), GTK_WIDGET(notebook), remmina_pref.toolbar_placement);

	gtk_container_add(GTK_CONTAINER(cnnwin), grid);

	/* Add drag capabilities to the toolbar */
	gtk_drag_source_set(GTK_WIDGET(toolbar), GDK_BUTTON1_MASK,
			    dnd_targets_tb, sizeof dnd_targets_tb / sizeof *dnd_targets_tb, GDK_ACTION_MOVE);
	g_signal_connect_after(GTK_WIDGET(toolbar), "drag-begin", G_CALLBACK(rcw_tb_drag_begin), NULL);
	g_signal_connect(GTK_WIDGET(toolbar), "drag-failed", G_CALLBACK(rcw_tb_drag_failed), cnnwin);

	/* Add drop capabilities to the drop/dest target for the toolbar (the notebook) */
	gtk_drag_dest_set(GTK_WIDGET(notebook), GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
			  dnd_targets_tb, sizeof dnd_targets_tb / sizeof *dnd_targets_tb, GDK_ACTION_MOVE);
	gtk_drag_dest_set_track_motion(GTK_WIDGET(notebook), TRUE);
	g_signal_connect(GTK_WIDGET(notebook), "drag-drop", G_CALLBACK(rcw_tb_drag_drop), cnnwin);

	cnnwin->priv->view_mode = SCROLLED_WINDOW_MODE;
	cnnwin->priv->toolbar = toolbar;
	cnnwin->priv->grid = grid;
	cnnwin->priv->notebook = notebook;
	cnnwin->priv->ss_width = width;
	cnnwin->priv->ss_height = height;
	cnnwin->priv->ss_maximized = maximize;

	/* The notebook and all its child must be realized now, or a reparent will
	 * call unrealize() and will destroy a GtkSocket */
	gtk_widget_show(grid);
	gtk_widget_show(GTK_WIDGET(cnnwin));
	GtkWindowGroup *wingrp = gtk_window_group_new();

	gtk_window_group_add_window(wingrp, GTK_WINDOW(cnnwin));
	gtk_window_set_transient_for(GTK_WINDOW(cnnwin), NULL);

	if (maximize)
		gtk_window_maximize(GTK_WINDOW(cnnwin));


	rcw_set_toolbar_visibility(cnnwin);

	return cnnwin;
}

static gint find_monitor_at_point  (GtkWidget *widget, gint x, gint y) {
	GdkDisplay* d = gtk_widget_get_display(widget);
	GdkMonitor* m = gdk_display_get_monitor_at_point(d, x, y);

	if (m == NULL)
		m = gdk_display_get_primary_monitor(d);

	return remmina_get_monitor_num(d, m);
}

static void rcw_create_overlay_ftb_overlay(RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);

	GtkWidget *revealer;
	RemminaConnectionWindowPriv *priv;

	priv = cnnwin->priv;


	if (priv->overlay_ftb_fr != NULL) {
		gtk_widget_destroy(priv->overlay_ftb_fr);
		priv->overlay_ftb_fr = NULL;
	}
	if (priv->overlay_ftb_overlay != NULL) {
		gtk_widget_destroy(priv->overlay_ftb_overlay);
		priv->overlay_ftb_overlay = NULL;
		priv->revealer = NULL;
	}


	rcw_create_floating_toolbar(cnnwin, priv->fss_view_mode);

	priv->overlay_ftb_overlay = gtk_event_box_new();

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	gtk_container_set_border_width(GTK_CONTAINER(vbox), 0);

	GtkWidget *handle = gtk_drawing_area_new();

	gtk_widget_set_size_request(handle, 4, 4);
	gtk_widget_set_name(handle, "ftb-handle");

	revealer = gtk_revealer_new();

	if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_TOP_LEFT) {
		gtk_widget_set_halign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_START);
	}
	else if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_TOP_RIGHT) {
		gtk_widget_set_halign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_END);
	}
	else{
		gtk_widget_set_halign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_CENTER);
	}

	if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT) {
		gtk_box_pack_start(GTK_BOX(vbox), handle, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), revealer, FALSE, FALSE, 0);
		gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
		gtk_widget_set_valign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_END);
	} else {
		gtk_box_pack_start(GTK_BOX(vbox), revealer, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), handle, FALSE, FALSE, 0);
		gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
		gtk_widget_set_valign(GTK_WIDGET(priv->overlay_ftb_overlay), GTK_ALIGN_START);
	}


	gtk_container_add(GTK_CONTAINER(revealer), priv->floating_toolbar_widget);
	gtk_widget_set_halign(GTK_WIDGET(revealer), GTK_ALIGN_CENTER);
	gtk_widget_set_valign(GTK_WIDGET(revealer), GTK_ALIGN_START);

	priv->revealer = revealer;

	GtkWidget *fr;

	fr = gtk_frame_new(NULL);
	priv->overlay_ftb_fr = fr;
	gtk_container_add(GTK_CONTAINER(priv->overlay_ftb_overlay), fr);
	gtk_container_add(GTK_CONTAINER(fr), vbox);

	gtk_widget_show(vbox);
	gtk_widget_show(revealer);
	gtk_widget_show(handle);
	gtk_widget_show(priv->overlay_ftb_overlay);
	gtk_widget_show(fr);

	if (remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT || 
		remmina_pref.floating_toolbar_placement == FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT)
		gtk_widget_set_name(fr, "ftbbox-lower");
	else
		gtk_widget_set_name(fr, "ftbbox-upper");

	gtk_overlay_add_overlay(GTK_OVERLAY(priv->overlay), priv->overlay_ftb_overlay);

	rcw_floating_toolbar_show(cnnwin, TRUE);

	g_signal_connect(G_OBJECT(priv->overlay_ftb_overlay), "enter-notify-event", G_CALLBACK(rcw_floating_toolbar_on_enter), cnnwin);
	g_signal_connect(G_OBJECT(priv->overlay_ftb_overlay), "leave-notify-event", G_CALLBACK(rcw_floating_toolbar_on_leave), cnnwin);
	g_signal_connect(G_OBJECT(priv->overlay_ftb_overlay), "scroll-event", G_CALLBACK(rcw_floating_toolbar_on_scroll), cnnwin);
	gtk_widget_add_events(
		GTK_WIDGET(priv->overlay_ftb_overlay),
		GDK_SCROLL_MASK
#if GTK_CHECK_VERSION(3, 4, 0)
		| GDK_SMOOTH_SCROLL_MASK
#endif
		);

	/* Add drag and drop capabilities to the source */
	gtk_drag_source_set(GTK_WIDGET(priv->overlay_ftb_overlay), GDK_BUTTON1_MASK,
			    dnd_targets_ftb, sizeof dnd_targets_ftb / sizeof *dnd_targets_ftb, GDK_ACTION_MOVE);
	g_signal_connect_after(GTK_WIDGET(priv->overlay_ftb_overlay), "drag-begin", G_CALLBACK(rcw_ftb_drag_begin), cnnwin);

	if (remmina_pref.fullscreen_toolbar_visibility == FLOATING_TOOLBAR_VISIBILITY_DISABLE)
		/* toolbar in fullscreenmode disabled, hide everything */
		gtk_widget_hide(fr);
}


static gboolean rcw_ftb_drag_drop(GtkWidget *widget, GdkDragContext *context,
				  gint x, gint y, guint time, RemminaConnectionWindow *cnnwin)
{
	TRACE_CALL(__func__);
	gint width, height;
	gint new_floating_toolbar_placement;
	RemminaConnectionObject *cnnobj;

	gtk_window_get_size(GTK_WINDOW(cnnwin), &width, &height);

	if (y >= height / 2 && x < width / 3)
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_BOTTOM_LEFT;
	else if (y >= height / 2 && x > (2*(width / 3)))
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_BOTTOM_RIGHT;
	else if (y < height / 2 && x > (2*(width / 3)))
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_TOP_RIGHT;
	else if (y < height / 2 && x  < width / 3)
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_TOP_LEFT;
	else if (y >= height /2)
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_BOTTOM;
	else
		new_floating_toolbar_placement = FLOATING_TOOLBAR_PLACEMENT_TOP;

	gtk_drag_finish(context, TRUE, TRUE, time);

	if (new_floating_toolbar_placement != remmina_pref.floating_toolbar_placement) {
		/* Destroy and recreate the FTB */
		remmina_pref.floating_toolbar_placement = new_floating_toolbar_placement;
		remmina_pref_save();
		rcw_create_overlay_ftb_overlay(cnnwin);
		cnnobj = rcw_get_visible_cnnobj(cnnwin);
		if (cnnobj) {
			rco_update_toolbar(cnnobj);
		}
	}
	return TRUE;
}

static void rcw_ftb_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data)
{
	TRACE_CALL(__func__);

	cairo_surface_t *surface;
	cairo_t *cr;
	GtkAllocation wa;
	double dashes[] = { 10 };

	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow*)user_data;
	guint hmargin = abs(cnnwin->priv->ftb_move_horiz);
	guint vmargin = abs(cnnwin->priv->ftb_move_vert);

	gtk_widget_get_allocation(widget, &wa);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, wa.width - hmargin, wa.height - vmargin);
	cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
	cairo_set_line_width(cr, 2);
	cairo_set_dash(cr, dashes, 1, 0);
	cairo_rectangle(cr, 0, 0, wa.width, wa.height);
	cairo_stroke(cr);
	cairo_destroy(cr);

	gtk_drag_set_icon_surface(context, surface);
}

RemminaConnectionWindow *rcw_create_fullscreen(GtkWindow *old, gint view_mode)
{
	TRACE_CALL(__func__);
	RemminaConnectionWindow *cnnwin;
	GtkNotebook *notebook;

#if GTK_CHECK_VERSION(3, 22, 0)
	gint n_monitors;
	gint i;
	GdkMonitor *old_monitor;
	GdkDisplay *old_display;
	GdkWindow *old_window;
#endif
	gint full_screen_target_monitor;

	full_screen_target_monitor = FULL_SCREEN_TARGET_MONITOR_UNDEFINED;
	if (old) {
#if GTK_CHECK_VERSION(3, 22, 0)
		old_window = gtk_widget_get_window(GTK_WIDGET(old));
		old_display = gdk_window_get_display(old_window);
		old_monitor = gdk_display_get_monitor_at_window(old_display, old_window);
		n_monitors = gdk_display_get_n_monitors(old_display);
		for (i = 0; i < n_monitors; ++i) {
			if (gdk_display_get_monitor(old_display, i) == old_monitor) {
				full_screen_target_monitor = i;
				break;
			}
		}
#else
		full_screen_target_monitor = gdk_screen_get_monitor_at_window(
			gdk_screen_get_default(),
			gtk_widget_get_window(GTK_WIDGET(old)));
#endif
	}

	cnnwin = rcw_new(TRUE, full_screen_target_monitor);

	gtk_widget_set_name(GTK_WIDGET(cnnwin), "remmina-connection-window-fullscreen");
	gtk_widget_realize(GTK_WIDGET(cnnwin));

	if (!view_mode)
		view_mode = VIEWPORT_FULLSCREEN_MODE;

	notebook = rcw_create_notebook(cnnwin);

	cnnwin->priv->overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(cnnwin), cnnwin->priv->overlay);
	gtk_container_add(GTK_CONTAINER(cnnwin->priv->overlay), GTK_WIDGET(notebook));
	gtk_widget_show(GTK_WIDGET(cnnwin->priv->overlay));

	cnnwin->priv->notebook = notebook;
	cnnwin->priv->view_mode = view_mode;
	cnnwin->priv->fss_view_mode = view_mode;

	/* Create the floating toolbar */
	rcw_create_overlay_ftb_overlay(cnnwin);
	/* Add drag and drop capabilities to the drop/dest target for floating toolbar */
	gtk_drag_dest_set(GTK_WIDGET(cnnwin->priv->overlay), GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
			  dnd_targets_ftb, sizeof dnd_targets_ftb / sizeof *dnd_targets_ftb, GDK_ACTION_MOVE);
	gtk_drag_dest_set_track_motion(GTK_WIDGET(cnnwin->priv->notebook), TRUE);
	g_signal_connect(GTK_WIDGET(cnnwin->priv->overlay), "drag-drop", G_CALLBACK(rcw_ftb_drag_drop), cnnwin);

	gtk_widget_show(GTK_WIDGET(cnnwin));
	GtkWindowGroup *wingrp = gtk_window_group_new();
	gtk_window_group_add_window(wingrp, GTK_WINDOW(cnnwin));
	gtk_window_set_transient_for(GTK_WINDOW(cnnwin), NULL);

	return cnnwin;
}

static gboolean rcw_hostkey_func(RemminaProtocolWidget *gp, guint keyval, gboolean release)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	const RemminaProtocolFeature *feature;
	gint i;

	if (release) {
		if (remmina_pref.hostkey && keyval == remmina_pref.hostkey) {
			priv->hostkey_activated = FALSE;
			if (priv->hostkey_used)
				/* hostkey pressed + something else */
				return TRUE;
		}
		/* If hostkey is released without pressing other keys, we should execute the
		 * shortcut key which is the same as hostkey. Be default, this is grab/ungrab
		 * keyboard */
		else if (priv->hostkey_activated) {
			/* Trap all key releases when hostkey is pressed */
			/* hostkey pressed + something else */
			return TRUE;
		} else {
			/* Any key pressed, no hostkey */
			return FALSE;
		}
	} else if (remmina_pref.hostkey && keyval == remmina_pref.hostkey) {
		/** @todo Add callback for hostname transparent overlay #832 */
		priv->hostkey_activated = TRUE;
		priv->hostkey_used = FALSE;
		return TRUE;
	} else if (!priv->hostkey_activated) {
		/* Any key pressed, no hostkey */
		return FALSE;
	}

	priv->hostkey_used = TRUE;
	keyval = gdk_keyval_to_lower(keyval);
	if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down
	    || keyval == GDK_KEY_Left || keyval == GDK_KEY_Right) {
		GtkAdjustment *adjust;
		int pos;

		if (cnnobj->connected && GTK_IS_SCROLLED_WINDOW(cnnobj->scrolled_container)) {
			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down)
				adjust = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container));
			else
				adjust = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container));

			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Left)
				pos = 0;
			else
				pos = gtk_adjustment_get_upper(adjust);

			gtk_adjustment_set_value(adjust, pos);
			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down)
				gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container), adjust);
			else
				gtk_scrolled_window_set_hadjustment(GTK_SCROLLED_WINDOW(cnnobj->scrolled_container), adjust);
		} else if (REMMINA_IS_SCROLLED_VIEWPORT(cnnobj->scrolled_container)) {
			RemminaScrolledViewport *gsv;
			GtkWidget *child;
			GdkWindow *gsvwin;
			gint sz;
			GtkAdjustment *adj;
			gdouble value;

			if (!GTK_IS_BIN(cnnobj->scrolled_container))
				return FALSE;

			gsv = REMMINA_SCROLLED_VIEWPORT(cnnobj->scrolled_container);
			child = gtk_bin_get_child(GTK_BIN(gsv));
			if (!GTK_IS_VIEWPORT(child))
				return FALSE;

			gsvwin = gtk_widget_get_window(GTK_WIDGET(gsv));
			if (!gsv)
				return FALSE;

			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down) {
				sz = gdk_window_get_height(gsvwin) + 2; // Add 2px of black scroll border
				adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(child));
			} else {
				sz = gdk_window_get_width(gsvwin) + 2; // Add 2px of black scroll border
				adj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(child));
			}

			if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Left)
				value = 0;
			else
				value = gtk_adjustment_get_upper(GTK_ADJUSTMENT(adj)) - (gdouble)sz + 2.0;

			gtk_adjustment_set_value(GTK_ADJUSTMENT(adj), value);
		}
	}

	if (keyval == remmina_pref.shortcutkey_fullscreen && !extrahardening) {
		switch (priv->view_mode) {
		case SCROLLED_WINDOW_MODE:
			rcw_switch_viewmode(cnnobj->cnnwin, priv->fss_view_mode);
			break;
		case SCROLLED_FULLSCREEN_MODE:
		case VIEWPORT_FULLSCREEN_MODE:
			rcw_switch_viewmode(cnnobj->cnnwin, SCROLLED_WINDOW_MODE);
			rcw_create_widget_areas(cnnobj);
			break;
		default:
			break;
		}
	} else if (keyval == remmina_pref.shortcutkey_autofit && !extrahardening) {
		if (priv->toolitem_autofit && gtk_widget_is_sensitive(GTK_WIDGET(priv->toolitem_autofit)))
			rcw_toolbar_autofit(GTK_TOOL_ITEM(gp), cnnobj->cnnwin);
	} else if (keyval == remmina_pref.shortcutkey_nexttab && !extrahardening) {
		i = gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook)) + 1;
		if (i >= gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook)))
			i = 0;
		gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook), i);
	} else if (keyval == remmina_pref.shortcutkey_prevtab && !extrahardening) {
		i = gtk_notebook_get_current_page(GTK_NOTEBOOK(priv->notebook)) - 1;
		if (i < 0)
			i = gtk_notebook_get_n_pages(GTK_NOTEBOOK(priv->notebook)) - 1;
		gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook), i);
	} else if (keyval == remmina_pref.shortcutkey_clipboard && !extrahardening) {
		if (remmina_protocol_widget_plugin_receives_keystrokes(REMMINA_PROTOCOL_WIDGET(cnnobj->proto))) {
			remmina_protocol_widget_send_clipboard((RemminaProtocolWidget*)cnnobj->proto, G_OBJECT(cnnobj->proto));
		}
	} else if (keyval == remmina_pref.shortcutkey_scale && !extrahardening) {
		if (gtk_widget_is_sensitive(GTK_WIDGET(priv->toolitem_scale))) {
			gtk_toggle_tool_button_set_active(
				GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_scale),
				!gtk_toggle_tool_button_get_active(
					GTK_TOGGLE_TOOL_BUTTON(
						priv->toolitem_scale)));
		}
	} else if (keyval == remmina_pref.shortcutkey_grab && !extrahardening) {
		gtk_toggle_tool_button_set_active(
			GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_grab),
			!gtk_toggle_tool_button_get_active(
				GTK_TOGGLE_TOOL_BUTTON(
					priv->toolitem_grab)));
	} else if (keyval == remmina_pref.shortcutkey_invert_scroll && !extrahardening) {
		if (priv->toolitem_invert_scroll && gtk_widget_is_sensitive(GTK_WIDGET(priv->toolitem_invert_scroll))) {
			gtk_toggle_tool_button_set_active(
				GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_invert_scroll),
				!gtk_toggle_tool_button_get_active(
					GTK_TOGGLE_TOOL_BUTTON(
						priv->toolitem_invert_scroll)));
		}
	} else if (keyval == remmina_pref.shortcutkey_minimize && !extrahardening) {
		rcw_toolbar_minimize(GTK_TOOL_ITEM(gp),
				     cnnobj->cnnwin);
	} else if (keyval == remmina_pref.shortcutkey_viewonly && !extrahardening) {
		remmina_file_set_int(cnnobj->remmina_file, "viewonly",
				     (remmina_file_get_int(cnnobj->remmina_file, "viewonly", 0)
				      == 0) ? 1 : 0);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(cnnobj->cnnwin->priv->toolitem_viewonly),
					remmina_file_get_int(cnnobj->remmina_file, "viewonly", 0));
	} else if (keyval == remmina_pref.shortcutkey_screenshot && !extrahardening) {
		rcw_toolbar_screenshot(GTK_TOOL_ITEM(gp),
				       cnnobj->cnnwin);
	} else if (keyval == remmina_pref.shortcutkey_disconnect && !extrahardening) {
		rco_disconnect_current_page(cnnobj);
	} else if (keyval == remmina_pref.shortcutkey_toolbar && !extrahardening) {
		if (priv->view_mode == SCROLLED_WINDOW_MODE) {
			remmina_pref.hide_connection_toolbar =
				!remmina_pref.hide_connection_toolbar;
			rcw_set_toolbar_visibility(cnnobj->cnnwin);
		}
	} else {
		for (feature =
			     remmina_protocol_widget_get_features(
				     REMMINA_PROTOCOL_WIDGET(
					     cnnobj->proto));
		     feature && feature->type;
		     feature++) {
			if (feature->type
			    == REMMINA_PROTOCOL_FEATURE_TYPE_TOOL
			    && GPOINTER_TO_UINT(
				    feature->opt3)
			    == keyval) {
				remmina_protocol_widget_call_feature_by_ref(
					REMMINA_PROTOCOL_WIDGET(
						cnnobj->proto),
					feature);
				break;
			}
		}
	}
	/* If a keypress makes the current cnnobj to move to another window,
	 * priv is now invalid. So we can no longer use priv here */
	cnnobj->cnnwin->priv->hostkey_activated = FALSE;

	/* Trap all keypresses when hostkey is pressed */
	return TRUE;
}

static RemminaConnectionWindow *rcw_find(RemminaFile *remminafile)
{
	TRACE_CALL(__func__);
	const gchar *tag;

	switch (remmina_pref.tab_mode) {
	case REMMINA_TAB_BY_GROUP:
		tag = remmina_file_get_string(remminafile, "group");
		break;
	case REMMINA_TAB_BY_PROTOCOL:
		tag = remmina_file_get_string(remminafile, "protocol");
		break;
	case REMMINA_TAB_ALL:
		tag = NULL;
		break;
	case REMMINA_TAB_NONE:
	default:
		return NULL;
	}
	return RCW(remmina_widget_pool_find(REMMINA_TYPE_CONNECTION_WINDOW, tag));
}

static gboolean rcw_delayed_window_present(gpointer user_data)
{
	RemminaConnectionWindow *cnnwin = (RemminaConnectionWindow *)user_data;

	if (cnnwin) {
		gtk_window_present_with_time(GTK_WINDOW(cnnwin), (guint32)(g_get_monotonic_time() / 1000));
		rcw_grab_focus(cnnwin);
	}
	cnnwin->priv->dwp_eventsourceid = 0;
	return G_SOURCE_REMOVE;
}

static void rco_on_connect(RemminaProtocolWidget *gp, RemminaConnectionObject *cnnobj)
{
	TRACE_CALL(__func__);

	REMMINA_DEBUG("Connect signal emitted");

	/* This signal handler is called by a plugin when it’s correctly connected
	 * (and authenticated) */

	cnnobj->connected = TRUE;

	remmina_protocol_widget_set_hostkey_func(REMMINA_PROTOCOL_WIDGET(cnnobj->proto),
						 (RemminaHostkeyFunc)rcw_hostkey_func);

	/** Remember recent list for quick connect, and save the current date
	 * in the last_used field.
	 */
	if (remmina_file_get_filename(cnnobj->remmina_file) == NULL)
		remmina_pref_add_recent(remmina_file_get_string(cnnobj->remmina_file, "protocol"),
					remmina_file_get_string(cnnobj->remmina_file, "server"));
	REMMINA_DEBUG("We save the last successful connection date");
	remmina_file_state_last_success(cnnobj->remmina_file);

	REMMINA_DEBUG("Saving credentials");
	/* Save credentials */
	remmina_file_save(cnnobj->remmina_file);

	if (cnnobj->cnnwin->priv->floating_toolbar_widget)
		gtk_widget_show(cnnobj->cnnwin->priv->floating_toolbar_widget);

	rco_update_toolbar(cnnobj);

	rcw_keyboard_grab_request(cnnobj, TRUE);

	REMMINA_DEBUG("Trying to present the window");
	/* Try to present window */
	cnnobj->cnnwin->priv->dwp_eventsourceid = g_timeout_add(200, rcw_delayed_window_present, (gpointer)cnnobj->cnnwin);
	rcw_show_widget_areas(cnnobj->cnnwin);
}

static void cb_lasterror_confirmed(void *cbdata, int btn)
{
	TRACE_CALL(__func__);
	rco_closewin((RemminaProtocolWidget *)cbdata);
}

static void rco_on_disconnect(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;
	RemminaConnectionWindowPriv *priv = cnnobj->cnnwin->priv;
	GtkWidget *pparent;

	REMMINA_DEBUG("Disconnect signal received on RemminaProtocolWidget");
	/* Detach the protocol widget from the notebook now, or we risk that a
	 * window delete will destroy cnnobj->proto before we complete disconnection.
	 */
	pparent = gtk_widget_get_parent(cnnobj->proto);
	if (pparent != NULL) {
		g_object_ref(cnnobj->proto);
		gtk_container_remove(GTK_CONTAINER(pparent), cnnobj->proto);
	}

	rcw_kp_ungrab(cnnobj->cnnwin);
	
	cnnobj->connected = FALSE;

	if (remmina_pref.save_view_mode) {
		if (cnnobj->cnnwin)
			remmina_file_set_int(cnnobj->remmina_file, "viewmode", cnnobj->cnnwin->priv->view_mode);
		remmina_file_save(cnnobj->remmina_file);
	}

	rcw_kp_ungrab(cnnobj->cnnwin);
	gtk_toggle_tool_button_set_active(
		GTK_TOGGLE_TOOL_BUTTON(priv->toolitem_grab),
		FALSE);

	if (remmina_protocol_widget_has_error(gp)) {
		/* We cannot close window immediately, but we must show a message panel */
		RemminaMessagePanel *mp;
		/* Destroy scrolled_container (and viewport) and all its children the plugin created
		 * on it, so they will not receive GUI signals */
		if (cnnobj->scrolled_container) {
			gtk_widget_destroy(cnnobj->scrolled_container);
			cnnobj->scrolled_container = NULL;
		}
		cnnobj->viewport = NULL;
		mp = remmina_message_panel_new();
		remmina_message_panel_setup_message(mp, remmina_protocol_widget_get_error_message(gp), cb_lasterror_confirmed, gp);
		rco_show_message_panel(gp->cnnobj, mp);
		REMMINA_DEBUG("Could not disconnect");
	} else {
		rco_closewin(gp);
		REMMINA_DEBUG("Disconnected");
	}
}

static void rco_on_desktop_resize(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;

	if (cnnobj && cnnobj->cnnwin && cnnobj->cnnwin->priv->view_mode != SCROLLED_WINDOW_MODE)
		rco_check_resize(cnnobj);
}

static void rco_on_update_align(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;

	remmina_protocol_widget_update_alignment(cnnobj);
}

static void rco_on_lock_dynres(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;

	cnnobj->dynres_unlocked = FALSE;
	rco_update_toolbar(cnnobj);
}

static void rco_on_unlock_dynres(RemminaProtocolWidget *gp, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj = gp->cnnobj;

	cnnobj->dynres_unlocked = TRUE;
	rco_update_toolbar(cnnobj);
}

gboolean rcw_open_from_filename(const gchar *filename)
{
	TRACE_CALL(__func__);
	RemminaFile *remminafile;
	GtkWidget *dialog;

	remminafile = remmina_file_manager_load_file(filename);
	if (remminafile) {
		if (remmina_file_get_int (remminafile, "profile-lock", FALSE)
				&& remmina_unlock_new(remmina_main_get_window()) == 0)
			return FALSE;
		rcw_open_from_file(remminafile);
		return TRUE;
	} else {
		dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
						_("The file “%s” is corrupted, unreadable, or could not be found."), filename);
		g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show(dialog);
		remmina_widget_pool_register(dialog);
		return FALSE;
	}
}

static gboolean open_connection_last_stage(gpointer user_data)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;

	/* Now we have an allocated size for our RemminaProtocolWidget. We can proceed with the connection */
	remmina_protocol_widget_update_remote_resolution(gp);
	remmina_protocol_widget_open_connection(gp);
	rco_check_resize(gp->cnnobj);

	return FALSE;
}

static void rpw_size_allocated_on_connection(GtkWidget *w, GdkRectangle *allocation, gpointer user_data)
{
	RemminaProtocolWidget *gp = (RemminaProtocolWidget *)w;

	/* Disconnect signal handler to avoid to be called again after a normal resize */
	g_signal_handler_disconnect(w, gp->cnnobj->deferred_open_size_allocate_handler);

	/* Allow extra 100 ms for size allocation (do we really need it?) */
	g_timeout_add(100, open_connection_last_stage, gp);

	return;
}

void rcw_open_from_file(RemminaFile *remminafile)
{
	TRACE_CALL(__func__);
	rcw_open_from_file_full(remminafile, NULL, NULL, NULL);
}

static void set_label_selectable(gpointer data, gpointer user_data)
{
	TRACE_CALL(__func__);
	GtkWidget *widget = GTK_WIDGET(data);

	if (GTK_IS_LABEL(widget))
		gtk_label_set_selectable(GTK_LABEL(widget), TRUE);
}

/**
 * @brief These define the response id's of the
 *	  gtksocket-is-not-available-warning-dialog buttons.
 */
enum GTKSOCKET_NOT_AVAIL_RESPONSE_TYPE {
	GTKSOCKET_NOT_AVAIL_RESPONSE_OPEN_BROWSER = 0,
	GTKSOCKET_NOT_AVAIL_RESPONSE_NUM
};

/**
 * @brief Gets called if the user interacts with the
 *	  gtksocket-is-not-available-warning-dialog
 */
static void rcw_gtksocket_not_available_dialog_response(GtkDialog *			self,
							gint				response_id,
							RemminaConnectionObject *	cnnobj)
{
	TRACE_CALL(__func__);

	GError *error = NULL;

	if (response_id == GTKSOCKET_NOT_AVAIL_RESPONSE_OPEN_BROWSER) {
		gtk_show_uri_on_window(
			NULL,
			// TRANSLATORS: This should be a link to the Remmina wiki page:
			// TRANSLATORS: 'GtkSocket feature is not available'.
			_("https://gitlab.com/Remmina/Remmina/-/wikis/GtkSocket-feature-is-not-available-in-a-Wayland-session"),
			GDK_CURRENT_TIME, &error
			);
	}

	// Close the current page since it's useless without GtkSocket.
	// The user would need to manually click the close button.
	if (cnnobj) rco_disconnect_current_page(cnnobj);

	gtk_widget_destroy(GTK_WIDGET(self));
}

GtkWidget *rcw_open_from_file_full(RemminaFile *remminafile, GCallback disconnect_cb, gpointer data, guint *handler)
{
	TRACE_CALL(__func__);
	RemminaConnectionObject *cnnobj;

	gint ret;
	GtkWidget *dialog;
	GtkWidget *newpage;
	gint width, height;
	gboolean maximize;
	gint view_mode;
	const gchar *msg;
	RemminaScaleMode scalemode;

	if (disconnect_cb) {
		g_print("disconnect_cb is deprecated inside rcw_open_from_file_full() and should be null\n");
		return NULL;
	}


	/* Create the RemminaConnectionObject */
	cnnobj = g_new0(RemminaConnectionObject, 1);
	cnnobj->remmina_file = remminafile;

	/* Create the RemminaProtocolWidget */
	cnnobj->proto = remmina_protocol_widget_new();
	remmina_protocol_widget_setup((RemminaProtocolWidget *)cnnobj->proto, remminafile, cnnobj);
	if (remmina_protocol_widget_has_error((RemminaProtocolWidget *)cnnobj->proto)) {
		GtkWindow *wparent;
		wparent = remmina_main_get_window();
		msg = remmina_protocol_widget_get_error_message((RemminaProtocolWidget *)cnnobj->proto);
		dialog = gtk_message_dialog_new(wparent, GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", msg);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		/* We should destroy cnnobj->proto and cnnobj now… TODO: Fix this leak */
		return NULL;
	}



	/* Set a name for the widget, for CSS selector */
	gtk_widget_set_name(GTK_WIDGET(cnnobj->proto), "remmina-protocol-widget");

	gtk_widget_set_halign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);
	gtk_widget_set_valign(GTK_WIDGET(cnnobj->proto), GTK_ALIGN_FILL);

	if (data)
		g_object_set_data(G_OBJECT(cnnobj->proto), "user-data", data);

	view_mode = remmina_file_get_int(cnnobj->remmina_file, "viewmode", 0);
	if (kioskmode)
		view_mode = VIEWPORT_FULLSCREEN_MODE;
	gint ismultimon = remmina_file_get_int(cnnobj->remmina_file, "multimon", 0);

	if (ismultimon)
		view_mode = VIEWPORT_FULLSCREEN_MODE;

	if (fullscreen)
		view_mode = VIEWPORT_FULLSCREEN_MODE;

	if (remmina_pref.start_fullscreen){
		view_mode = VIEWPORT_FULLSCREEN_MODE;
	}

	/* Create the viewport to make the RemminaProtocolWidget scrollable */
	cnnobj->viewport = gtk_viewport_new(NULL, NULL);
	gtk_widget_set_name(cnnobj->viewport, "remmina-cw-viewport");
	gtk_widget_show(cnnobj->viewport);
	gtk_container_set_border_width(GTK_CONTAINER(cnnobj->viewport), 0);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(cnnobj->viewport), GTK_SHADOW_NONE);

	/* Create the scrolled container */
	scalemode = get_current_allowed_scale_mode(cnnobj, NULL, NULL);
	cnnobj->scrolled_container = rco_create_scrolled_container(scalemode, view_mode);
    remmina_protocol_widget_set_current_scale_mode(REMMINA_PROTOCOL_WIDGET(cnnobj->proto), scalemode);

	gtk_container_add(GTK_CONTAINER(cnnobj->scrolled_container), cnnobj->viewport);

	/* Determine whether the plugin can scale or not. If the plugin can scale and we do
	 * not want to expand, then we add a GtkAspectFrame to maintain aspect ratio during scaling */
	cnnobj->plugin_can_scale = remmina_plugin_manager_query_feature_by_type(REMMINA_PLUGIN_TYPE_PROTOCOL,
										remmina_file_get_string(remminafile, "protocol"),
										REMMINA_PROTOCOL_FEATURE_TYPE_SCALE);

	cnnobj->aspectframe = NULL;
	gtk_container_add(GTK_CONTAINER(cnnobj->viewport), cnnobj->proto);

	/* Determine whether this connection will be put on a new window
	 * or in an existing one */
	cnnobj->cnnwin = rcw_find(remminafile);
	if (!cnnobj->cnnwin) {
		/* Connection goes on a new toplevel window */
		switch (view_mode) {
		case SCROLLED_FULLSCREEN_MODE:
		case VIEWPORT_FULLSCREEN_MODE:
			cnnobj->cnnwin = rcw_create_fullscreen(NULL, view_mode);
			break;
		case SCROLLED_WINDOW_MODE:
		default:
			width = remmina_file_get_int(cnnobj->remmina_file, "window_width", 640);
			height = remmina_file_get_int(cnnobj->remmina_file, "window_height", 480);
			maximize = remmina_file_get_int(cnnobj->remmina_file, "window_maximize", FALSE) ? TRUE : FALSE;
			cnnobj->cnnwin = rcw_create_scrolled(width, height, maximize);
			break;
		}
		rcw_update_tag(cnnobj->cnnwin, cnnobj);
		rcw_append_new_page(cnnobj->cnnwin, cnnobj);
	} else {
		newpage = rcw_append_new_page(cnnobj->cnnwin, cnnobj);
		gtk_window_present(GTK_WINDOW(cnnobj->cnnwin));
		nb_set_current_page(cnnobj->cnnwin->priv->notebook, newpage);
	}
	if (view_mode == VIEWPORT_FULLSCREEN_MODE && ismultimon) {
	        rcw_create_widget_areas(cnnobj);
	}

	// Do not call remmina_protocol_widget_update_alignment(cnnobj); here or cnnobj->proto will not fill its parent size
	// and remmina_protocol_widget_update_remote_resolution() cannot autodetect available space

	gtk_widget_show(cnnobj->proto);
	g_signal_connect(G_OBJECT(cnnobj->proto), "connect", G_CALLBACK(rco_on_connect), cnnobj);
	g_signal_connect(G_OBJECT(cnnobj->proto), "disconnect", G_CALLBACK(rco_on_disconnect), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "desktop-resize", G_CALLBACK(rco_on_desktop_resize), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "update-align", G_CALLBACK(rco_on_update_align), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "lock-dynres", G_CALLBACK(rco_on_lock_dynres), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "unlock-dynres", G_CALLBACK(rco_on_unlock_dynres), NULL);
	g_signal_connect(G_OBJECT(cnnobj->proto), "enter-notify-event", G_CALLBACK(rco_enter_protocol_widget), cnnobj);
	g_signal_connect(G_OBJECT(cnnobj->proto), "leave-notify-event", G_CALLBACK(rco_leave_protocol_widget), cnnobj);

	if (!remmina_pref.save_view_mode)
		remmina_file_set_int(cnnobj->remmina_file, "viewmode", remmina_pref.default_mode);


	/* If it is a GtkSocket plugin and X11 is not available, we inform the
	 * user and close the connection */
	ret = remmina_plugin_manager_query_feature_by_type(REMMINA_PLUGIN_TYPE_PROTOCOL,
							   remmina_file_get_string(remminafile, "protocol"),
							   REMMINA_PROTOCOL_FEATURE_TYPE_GTKSOCKET);
	if (ret && !remmina_gtksocket_available()) {
		gchar *title = _("Warning: This plugin requires GtkSocket, but this "
				 "feature is unavailable in a Wayland session.");

		gchar *err_msg =
			// TRANSLATORS: This should be a link to the Remmina wiki page:
			// 'GtkSocket feature is not available'.
			_("Plugins relying on GtkSocket can't run in a "
			  "Wayland session.\nFor more info and a possible "
			  "workaround, please visit the Remmina wiki at:\n\n"
			  "https://gitlab.com/Remmina/Remmina/-/wikis/GtkSocket-feature-is-not-available-in-a-Wayland-session");

		dialog = gtk_message_dialog_new(
			GTK_WINDOW(cnnobj->cnnwin),
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_WARNING,
			GTK_BUTTONS_OK,
			"%s", title);

		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
							 err_msg);
		gtk_dialog_add_button(GTK_DIALOG(dialog), _("Open in web browser"),
				      GTKSOCKET_NOT_AVAIL_RESPONSE_OPEN_BROWSER);

		REMMINA_CRITICAL(g_strdup_printf("%s\n%s", title, err_msg));

		g_signal_connect(G_OBJECT(dialog),
				 "response",
				 G_CALLBACK(rcw_gtksocket_not_available_dialog_response),
				 cnnobj);

		// Make Text selectable. Usefull because of the link in the text.
		GtkWidget *area = gtk_message_dialog_get_message_area(
			GTK_MESSAGE_DIALOG(dialog));
		GtkContainer *box = (GtkContainer *)area;

		GList *children = gtk_container_get_children(box);
		g_list_foreach(children, set_label_selectable, NULL);
		g_list_free(children);

		gtk_widget_show(dialog);

		return NULL;    /* Should we destroy something before returning? */
	}

	if (cnnobj->cnnwin->priv->floating_toolbar_widget)
		gtk_widget_show(cnnobj->cnnwin->priv->floating_toolbar_widget);

	if (remmina_protocol_widget_has_error((RemminaProtocolWidget *)cnnobj->proto)) {
		printf("OK, an error occurred in initializing the protocol plugin before connecting. The error is %s.\n"
		       "TODO: Put this string as an error to show", remmina_protocol_widget_get_error_message((RemminaProtocolWidget *)cnnobj->proto));
		return cnnobj->proto;
	}


	/* GTK window setup is done here, and we are almost ready to call remmina_protocol_widget_open_connection().
	 * But size has not yet been allocated by GTK
	 * to the widgets. If we are in RES_USE_INITIAL_WINDOW_SIZE resolution mode or scale is REMMINA_PROTOCOL_WIDGET_SCALE_MODE_DYNRES,
	 * we should wait for a size allocation from GTK for cnnobj->proto
	 * before connecting */

	cnnobj->deferred_open_size_allocate_handler = g_signal_connect(G_OBJECT(cnnobj->proto), "size-allocate", G_CALLBACK(rpw_size_allocated_on_connection), NULL);

	return cnnobj->proto;
}

GtkWindow *rcw_get_gtkwindow(RemminaConnectionObject *cnnobj)
{
	return &cnnobj->cnnwin->window;
}
GtkWidget *rcw_get_gtkviewport(RemminaConnectionObject *cnnobj)
{
	return cnnobj->viewport;
}

void rcw_set_delete_confirm_mode(RemminaConnectionWindow *cnnwin, RemminaConnectionWindowOnDeleteConfirmMode mode)
{
	TRACE_CALL(__func__);
	cnnwin->priv->on_delete_confirm_mode = mode;
}

/**
 * Deletes a RemminaMessagePanel from the current cnnobj
 * and if it was visible, make visible the last remaining one.
 */
void rco_destroy_message_panel(RemminaConnectionObject *cnnobj, RemminaMessagePanel *mp)
{
	TRACE_CALL(__func__);
	GList *childs, *cc;
	RemminaMessagePanel *lastPanel;
	gboolean was_visible;
	GtkWidget *page;

	page = nb_find_page_by_cnnobj(cnnobj->cnnwin->priv->notebook, cnnobj);
	childs = gtk_container_get_children(GTK_CONTAINER(page));
	cc = g_list_first(childs);
	while (cc != NULL) {
		if ((RemminaMessagePanel *)cc->data == mp)
			break;
		cc = g_list_next(cc);
	}
	g_list_free(childs);

	if (cc == NULL) {
		printf("Remmina: Warning. There was a request to destroy a RemminaMessagePanel that is not on the page\n");
		return;
	}
	was_visible = gtk_widget_is_visible(GTK_WIDGET(mp));
	gtk_widget_destroy(GTK_WIDGET(mp));

	/* And now, show the last remaining message panel, if needed */
	if (was_visible) {
		childs = gtk_container_get_children(GTK_CONTAINER(page));
		cc = g_list_first(childs);
		lastPanel = NULL;
		while (cc != NULL) {
			if (G_TYPE_CHECK_INSTANCE_TYPE(cc->data, REMMINA_TYPE_MESSAGE_PANEL))
				lastPanel = (RemminaMessagePanel *)cc->data;
			cc = g_list_next(cc);
		}
		g_list_free(childs);
		if (lastPanel)
			gtk_widget_show(GTK_WIDGET(lastPanel));
		else if (cnnobj->connected && cnnobj->proto)
			remmina_protocol_widget_grab_focus(REMMINA_PROTOCOL_WIDGET(cnnobj->proto));
	}
}

/**
 * Each cnnobj->page can have more than one RemminaMessagePanel, but 0 or 1 are visible.
 *
 * This function adds a RemminaMessagePanel to cnnobj->page, move it to top,
 * and makes it the only visible one.
 */
void rco_show_message_panel(RemminaConnectionObject *cnnobj, RemminaMessagePanel *mp)
{
	TRACE_CALL(__func__);
	GList *childs, *cc;
	GtkWidget *page;

	/* Hides all RemminaMessagePanels childs of cnnobj->page */
	page = nb_find_page_by_cnnobj(cnnobj->cnnwin->priv->notebook, cnnobj);
	childs = gtk_container_get_children(GTK_CONTAINER(page));
	cc = g_list_first(childs);
	while (cc != NULL) {
		if (G_TYPE_CHECK_INSTANCE_TYPE(cc->data, REMMINA_TYPE_MESSAGE_PANEL))
			gtk_widget_hide(GTK_WIDGET(cc->data));
		cc = g_list_next(cc);
	}
	g_list_free(childs);

	if (remmina_pref.mp_left){
		gtk_widget_set_halign(GTK_WIDGET(mp), GTK_ALIGN_START);
	}

	/* Add the new message panel at the top of cnnobj->page */
	gtk_box_pack_start(GTK_BOX(page), GTK_WIDGET(mp), FALSE, FALSE, 0);
	gtk_box_reorder_child(GTK_BOX(page), GTK_WIDGET(mp), 0);

	/* Show the message panel */
	gtk_widget_show_all(GTK_WIDGET(mp));

	/* Focus the correct field of the RemminaMessagePanel */
	remmina_message_panel_focus_auth_entry(mp);
}

static gboolean rcw_map_event_fullscreen_monitor(GtkWidget *widget, GdkEvent *event,
	                                     gpointer data) {
	TRACE_CALL(__func__);
	MultimonWindow* w = RCW_MULTIMON(widget);
	GdkWindow*me = gtk_widget_get_window(GTK_WIDGET(widget));
	if (me == NULL || w->priv == NULL)
		return TRUE;
	gdk_window_set_fullscreen_mode(me,
									GDK_FULLSCREEN_ON_CURRENT_MONITOR);
	gdk_window_fullscreen_on_monitor(me, w->priv->monitor);
	gdk_window_set_keep_above(me, TRUE);
	gdk_window_set_accept_focus(me, TRUE);

	return TRUE;
}

/*
 *  Multimonitor extra windows management
 */


/* Focusing management */
static gboolean rcw_other_fs_enter(MultimonWindow*w, GdkEventCrossing *event, RemminaConnectionObject* cnnobj) {
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING");
	print_crossing_event(event);
#endif
	if (event->mode == GDK_CROSSING_UNGRAB) {
		// Someone steal our grab, take note and do not attempt to regrab
		rcw_keyboard_grab_request(cnnobj, FALSE);
		return FALSE;
	}
	rcw_keyboard_grab_request(cnnobj, TRUE);
	return TRUE;
}

static gboolean rcw_other_fs_leave(MultimonWindow*w, GdkEventCrossing *event, RemminaConnectionObject* cnnobj) {
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING");
	print_crossing_event(event);
#endif
	rcw_keyboard_grab_request(cnnobj, FALSE);
	return TRUE;
}

static gboolean rcw_other_fs_focus_in(MultimonWindow*w, GdkEventFocus *event, RemminaConnectionObject* cnnobj)
{
	TRACE_CALL(__func__);
#if DEBUG_KB_GRABBING
	REMMINA_DEBUG("DEBUG_KB_GRABBING: focus=%d %d", event->in, event->send_event);
#endif
	if (IS_WAYLAND(w)) {
		rcw_restore_widget_areas(cnnobj->cnnwin);
	}
	rcw_keyboard_grab_request(cnnobj, TRUE);
	return TRUE;
}

/* RCW_MULTIMON object */
static void remmina_fullscreen_window_dispose(GObject *object) {
	MultimonWindow *fswin = RCW_MULTIMON(object);

	if (fswin->priv != NULL && fswin->priv->drawing_area != NULL) {
	    gtk_widget_destroy(fswin->priv->drawing_area);
	fswin->priv->drawing_area = NULL;
	}
	g_signal_handlers_disconnect_by_func(G_OBJECT(fswin), G_CALLBACK(rcw_map_event_fullscreen_monitor), NULL);
	g_signal_handlers_disconnect_by_func(GTK_WINDOW(fswin), "focus-in-event", G_CALLBACK(rcw_other_fs_focus_in));
	g_signal_handlers_disconnect_by_func(GTK_WINDOW(fswin), "enter-notify-event", G_CALLBACK(rcw_other_fs_enter));
	g_signal_handlers_disconnect_by_func(GTK_WINDOW(fswin), "leave-notify-event", G_CALLBACK(rcw_other_fs_leave));

	// Chaîner la méthode parent
	G_OBJECT_CLASS(multimon_window_parent_class)->dispose(object);
}

static void remmina_fullscreen_window_finalize(GObject *object) {
	MultimonWindow *fswin = RCW_MULTIMON(object);

	if (fswin->priv != NULL) {
	    g_free(fswin->priv);
	    fswin->priv = NULL;
	}
	G_OBJECT_CLASS(multimon_window_parent_class)->finalize(object);
}

static void multimon_window_class_init(MultimonWindowClass *klass) {
	TRACE_CALL(__func__);
	G_OBJECT_CLASS(klass)->dispose = remmina_fullscreen_window_dispose;
	G_OBJECT_CLASS(klass)->finalize = remmina_fullscreen_window_finalize;
}

static void multimon_window_init(MultimonWindow *fswin) {
	TRACE_CALL(__func__);
	fswin->priv = g_new0(MultimonWindowPriv, 1);
	fswin->priv->drawing_area = NULL;

	g_signal_connect(G_OBJECT(fswin), "map-event", G_CALLBACK(rcw_map_event_fullscreen_monitor), 0);
}

static void rcw_normalize_monitors(RemminaConnectionWindowPriv*priv, int multimon_main_monitor) {
	// calculation to support translate monitors to 0,0. TODO: same thing for gaps between monitors?
	gboolean hasPrimary = FALSE;
	gint min_x = INT_MAX, min_y = INT_MAX;
	gint mostleft_monitor_y = 0, mosttop_monitor_x = 0;
	gint origin_shift_x = 0;
	gint origin_shift_y = 0;
	for (gint i = 0; i < priv->multimon_areas->len; ++i) {
		MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, i));		
		if (monwin->priv->isPrimary)
			hasPrimary = TRUE;
		if (monwin->priv->geometry.x < min_x) {
			min_x = monwin->priv->geometry.x;
			mostleft_monitor_y = monwin->priv->geometry.y;
		}
		if (monwin->priv->geometry.y < min_y) {
			min_y = monwin->priv->geometry.y;
			mosttop_monitor_x = monwin->priv->geometry.x;
		}
	}

	if (abs(mostleft_monitor_y-min_x)<abs(mosttop_monitor_x-min_y)) {
		origin_shift_x = min_x;
		origin_shift_y = mostleft_monitor_y;
	} else {
		origin_shift_x = mosttop_monitor_x;
		origin_shift_y = min_y;
	}
	for (gint i = 0; i < priv->multimon_areas->len; ++i) {
		MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, i));		
		if (!hasPrimary && multimon_main_monitor == i) {
			monwin->priv->isPrimary = TRUE;
		}
		monwin->priv->geometry.x -= origin_shift_x;
		monwin->priv->geometry.y -= origin_shift_y;
	}
	priv->multi_mon = priv->multimon_areas->len > 1;
}

static gint rcw_probe_monitors(RemminaConnectionObject *cnnobj, const gchar*monitorids_str) {
	GdkDisplay *display = gdk_display_get_default();
	RemminaConnectionWindowPriv*priv = cnnobj->cnnwin->priv;
	gint multimon_main_monitor = -1;
	gint n_monitors = gdk_display_get_n_monitors(display);
	gint first_monitor = -1;
	gint first_filtered_monitor = INT_MAX;
	gchar **monitorids = NULL;

	if (monitorids_str != NULL) {
		monitorids = g_strsplit(monitorids_str, ",", -1);
	}

	const GdkRectangle primary_monitor_geometry = rcw_first_monitor_geometry();

	priv->multi_mon = 0;
	if (priv->multimon_areas != NULL)
		g_ptr_array_free(priv->multimon_areas, TRUE);
	
	priv->multimon_areas = g_ptr_array_new();
	REMMINA_DEBUG("number of monitors: %d", n_monitors);
	for (gint i = 0; i < n_monitors; ++i) {
		GdkMonitor *monitor = gdk_display_get_monitor(display, i);
	    if (monitor == NULL) {
	        g_warning("monitor %d is not accessible", i);
	        continue;
		}

		const gchar*model = gdk_monitor_get_model(monitor);
		const gchar*manufacturer = gdk_monitor_get_manufacturer(monitor);

		REMMINA_DEBUG("monitor#%d: model=%s manufacturer=%s", i, model, manufacturer);

		if (monitorids != NULL) {
			gboolean found = FALSE;
			for (gint j = 0; monitorids[j] != NULL; j++) {
				gint64 number;
				gboolean is_number;
				is_number = g_ascii_string_to_signed(monitorids[j], 10, G_MININT, G_MAXINT, &number, NULL);
				if ( (is_number &&  number == i) ||
						g_strcmp0(monitorids[j], manufacturer) == 0 ||
						g_strcmp0(monitorids[j], model) == 0) {
						found = TRUE;
						if (j < first_filtered_monitor) {
							first_filtered_monitor = j;
							first_monitor = i;
						}
						break;
				}
			}
			if (!found) {
				REMMINA_DEBUG("monitor %d ignored (from configuration)", i);
				continue;
			}
		}

		if (first_monitor == -1) first_monitor = i;
		MultimonWindow *fullscreenWindow = RCW_MULTIMON(g_object_new(REMMINA_TYPE_MULTIMON_WINDOW, NULL));
		MultimonWindowPriv *wpriv = fullscreenWindow->priv;

		g_signal_connect(GTK_WINDOW(fullscreenWindow), "focus-in-event", G_CALLBACK(rcw_other_fs_focus_in), cnnobj);
		g_signal_connect(GTK_WINDOW(fullscreenWindow), "enter-notify-event", G_CALLBACK(rcw_other_fs_enter), cnnobj);
		g_signal_connect(GTK_WINDOW(fullscreenWindow), "leave-notify-event", G_CALLBACK(rcw_other_fs_leave), cnnobj);

		wpriv->scale = gdk_monitor_get_scale_factor(monitor);
		gdk_monitor_get_geometry(monitor, &wpriv->geometry);
		wpriv->physicalHeight = gdk_monitor_get_height_mm(monitor);
		wpriv->physicalWidth = gdk_monitor_get_width_mm(monitor);
		wpriv->isPrimary = gdk_monitor_is_primary(monitor);

		if (monitorids == NULL && gdk_rectangle_equal(&primary_monitor_geometry, &wpriv->geometry)) {
			wpriv->isPrimary = 1;
		}

		wpriv->monitor = i;
		// main monitor is either the primary, or first configured monitor
		if (wpriv->isPrimary && multimon_main_monitor == -1 && monitorids == NULL)
			multimon_main_monitor = i;
		g_ptr_array_add(priv->multimon_areas, fullscreenWindow);
	}

	if (monitorids != NULL) {
	    g_strfreev(monitorids);
	}

	if (multimon_main_monitor == -1) {
		multimon_main_monitor = first_monitor;
	}

	rcw_normalize_monitors(priv, multimon_main_monitor);

	return multimon_main_monitor;
}

static void rcw_create_widget_areas(RemminaConnectionObject *cnnobj) {
	TRACE_CALL(__func__);
	g_return_if_fail(cnnobj != NULL && cnnobj->cnnwin != NULL && cnnobj->cnnwin->priv != NULL);

	RemminaConnectionWindow*cnnwin = cnnobj->cnnwin;
	RemminaConnectionWindowPriv*priv = cnnwin->priv;
	gint multimon_main_monitor = -1;
	const gchar*monitorids_str = remmina_file_get_string(cnnobj->remmina_file, "monitorids");


	if (priv->multimon_areas != NULL) {
	    rcw_dispose_widget_areas(priv);
	}
	
	multimon_main_monitor = rcw_probe_monitors(cnnobj, monitorids_str);
	if (multimon_main_monitor ==-1 && monitorids_str!=NULL && strlen(monitorids_str)>0) {
		REMMINA_DEBUG("Multimonitor filtered all monitors, now using all available monitors");
		multimon_main_monitor = rcw_probe_monitors(cnnobj, NULL);
	}

	if (multimon_main_monitor == -1) {
		REMMINA_DEBUG("No monitors where found!");
		return;
	}

	REMMINA_DEBUG("total number of active monitors %d", priv->multimon_areas->len);
	const gchar*title = remmina_file_get_string(cnnobj->remmina_file, "name");
	for (gint i = 0; i < priv->multimon_areas->len; ++i) {
		MultimonWindow* fullscreenWindow = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, i));
		MultimonWindowPriv *wpriv = fullscreenWindow->priv;
		if (wpriv->monitor != multimon_main_monitor) {	// main monitor is handled by the legacy code
	        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(fullscreenWindow), TRUE);
			gtk_widget_set_name(GTK_WIDGET(fullscreenWindow), "remmina-connection-window-fullscreen");
			if (title != NULL) {
				gchar*childTitle = g_malloc(strlen(title)+20);
				sprintf(childTitle, "%s#%d", title, wpriv->monitor);
				gtk_window_set_title(GTK_WINDOW(fullscreenWindow), childTitle);
				g_free(childTitle);
			}
		}
		REMMINA_DEBUG("monitor#%d (%s) primary=%d position=%dx%d size=%dx%d scale=%d",
			wpriv->monitor, wpriv->monitor == multimon_main_monitor?"main":"extra",
			wpriv->isPrimary, wpriv->geometry.x,wpriv->geometry.y,wpriv->geometry.width,wpriv->geometry.height,wpriv->scale);
	}
	priv->multimon_main_monitor = multimon_main_monitor;
}

static void*widget_area_last_restored_cnnwin = NULL;

static void rcw_iconify_widget_areas(RemminaConnectionWindow*cnnwin) {
	TRACE_CALL(__func__);
	g_return_if_fail(cnnwin != NULL && cnnwin->priv != NULL);
	if (cnnwin->priv->multimon_areas == NULL)
		return;

	widget_area_last_restored_cnnwin = NULL;
	for (guint i = 0; i < cnnwin->priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(cnnwin->priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != cnnwin->priv->multimon_main_monitor) {
			if (!fswin->priv->iconified) {
				gtk_window_iconify(GTK_WINDOW(fswin));
				if (IS_WAYLAND(fswin)) {
					gtk_widget_hide(GTK_WIDGET(fswin));
				}
				fswin->priv->iconified = TRUE;
			}
		}
	}
}


// Windowing actions in multi monitor when a multimonitor connection gained the focus:
// 	in X11: unraise all other connection fullscreen windows
//  in Wayland: hide all other connection fullscreen windows that are not in common
static gboolean rcw_active_connection_changed(GtkWidget *widget, gpointer data)
{
	TRACE_CALL(__func__);
	if (REMMINA_IS_CONNECTION_WINDOW(widget) && REMMINA_IS_CONNECTION_WINDOW(data)) {
		RemminaConnectionWindow*receiver = RCW(widget);
		RemminaConnectionWindow*sender = RCW(data);
		if (sender != receiver && receiver->priv != NULL && receiver->priv->multimon_areas != NULL) {
			if (IS_WAYLAND(widget)) {
				GHashTable *sender_monitors = g_hash_table_new(g_direct_hash, g_direct_equal);
				if (sender->priv->multimon_areas != NULL) {
					for (guint i = 0; i < sender->priv->multimon_areas->len; i++) {
						MultimonWindow *fswin = g_ptr_array_index(sender->priv->multimon_areas, i);
						g_hash_table_insert(sender_monitors, GINT_TO_POINTER(fswin->priv->monitor), NULL);
					}
				}
				g_hash_table_insert(sender_monitors, GINT_TO_POINTER(sender->priv->multimon_main_monitor), NULL);
				for (guint i = 0; i < receiver->priv->multimon_areas->len; i++) {
					MultimonWindow *fswin = g_ptr_array_index(receiver->priv->multimon_areas, i);
					if (fswin != NULL && g_hash_table_contains(sender_monitors, GINT_TO_POINTER(fswin->priv->monitor))) {
						if (!fswin->priv->iconified) {
							gtk_widget_hide(GTK_WIDGET(fswin));
							fswin->priv->iconified = TRUE;
						}
					}
				}
				g_hash_table_destroy(sender_monitors);
			} else {
				RemminaConnectionWindowPriv*priv = receiver->priv;
				for (guint i = 0; i < priv->multimon_areas->len; i++) {
					MultimonWindow *fswin = g_ptr_array_index(priv->multimon_areas, i);
					if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != priv->multimon_main_monitor) {
						gtk_window_set_keep_above(GTK_WINDOW(fswin), FALSE);
					}
				}
				gtk_window_set_keep_above(GTK_WINDOW(receiver), FALSE);
			}
		}
		return TRUE;
	}
	return FALSE;
}


static void rcw_show_widget_areas(RemminaConnectionWindow*cnnwin) {
	TRACE_CALL(__func__);
	g_return_if_fail(cnnwin != NULL && cnnwin->priv != NULL);
	if (cnnwin->priv->multimon_areas == NULL)
	        return;

	REMMINA_DEBUG("rcw_show_widget_areas");
	for (guint i = 0; i < cnnwin->priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(cnnwin->priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != cnnwin->priv->multimon_main_monitor && fswin->priv->drawing_area != NULL) {
			gtk_widget_show(GTK_WIDGET(fswin));
		}
	}
}

static GdkWindow* rcw_widget_active_area(RemminaConnectionWindow*cnnwin) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(cnnwin != NULL && cnnwin->priv != NULL, FALSE);
	if (cnnwin->priv->multimon_areas == NULL)
	    return NULL;

	for (guint i = 0; i < cnnwin->priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(cnnwin->priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != cnnwin->priv->multimon_main_monitor && fswin->priv->drawing_area != NULL) {
			GdkWindow* w = gtk_widget_get_parent_window(GTK_WIDGET(fswin->priv->drawing_area));
			if (gdk_window_get_state(w) & GDK_WINDOW_STATE_FOCUSED)
				return w;
		}
	}
	return NULL;
}

static void rcw_restore_widget_areas(RemminaConnectionWindow*cnnwin) {
	TRACE_CALL(__func__);
	g_return_if_fail(cnnwin != NULL && cnnwin->priv != NULL);
	if (cnnwin->priv->multimon_areas == NULL)
		return;
	if (widget_area_last_restored_cnnwin == cnnwin) {
		return;
	}
	widget_area_last_restored_cnnwin = cnnwin;


	REMMINA_DEBUG("rcw_restore_widget_areas");
	/* Signal all windows that which connection is active */	
	remmina_widget_pool_foreach(rcw_active_connection_changed, cnnwin);

	for (guint i = 0; i < cnnwin->priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(cnnwin->priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin) && fswin->priv != NULL && fswin->priv->monitor != cnnwin->priv->multimon_main_monitor && fswin->priv->drawing_area != NULL) {
			if (fswin->priv->iconified) {
				REMMINA_DEBUG("deiconify");
				fswin->priv->iconified = FALSE;
				gtk_window_deiconify(GTK_WINDOW(fswin));
				if (IS_WAYLAND(fswin)) {
					gtk_widget_show(GTK_WIDGET(fswin));
				}
			}
			// needed to raise again in case other connection is using fullscreen on same monitor(s)
			gtk_window_set_keep_above(GTK_WINDOW(fswin), FALSE);
			gtk_window_set_keep_above(GTK_WINDOW(fswin), TRUE);
		}
	}	
	if (IS_WAYLAND(cnnwin)) {
		gtk_widget_show(GTK_WIDGET(cnnwin));
	}
}

static void rcw_dispose_widget_areas(RemminaConnectionWindowPriv*priv) {
	g_return_if_fail(priv != NULL);
	if (priv->multimon_areas == NULL)
		return;

	for (guint i = 0; i < priv->multimon_areas->len; i++) {
		MultimonWindow *fswin = g_ptr_array_index(priv->multimon_areas, i);
		if (fswin != NULL && G_IS_OBJECT(fswin)) {
			if (GTK_IS_WINDOW(fswin)) {
	            gtk_widget_destroy(GTK_WIDGET(fswin));
	        }
		}
	}
	g_ptr_array_free(priv->multimon_areas, TRUE);
	priv->multimon_areas = NULL;
}

gint rcw_get_monitor_count(RemminaConnectionWindow *cnnwin) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(cnnwin != NULL && cnnwin->priv != NULL, 0);

	if (cnnwin->priv->multimon_areas == NULL)
		return 0;

	return cnnwin->priv->multimon_areas->len;
}

gboolean rcw_get_monitor_info(RemminaConnectionWindow *cnnwin, gint index, gboolean*is_primary, gint*x, gint*y, gint*width, gint*height, gint*phys_width_mm, gint* phys_height_mm) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(cnnwin != NULL && cnnwin->priv != NULL, FALSE);

	if (cnnwin->priv->multimon_areas == NULL || index >= cnnwin->priv->multimon_areas->len)
		return FALSE;

	MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(cnnwin->priv->multimon_areas, index));
	g_return_val_if_fail(monwin != NULL && monwin->priv != NULL, FALSE);

	if (is_primary != NULL) *is_primary = (monwin->priv->isPrimary == 1);
	if (x != NULL) *x = cnnwin->priv->multimon_areas->len != 1 ? monwin->priv->geometry.x : 0;
	if (y != NULL) *y = cnnwin->priv->multimon_areas->len != 1 ? monwin->priv->geometry.y : 0;
	if (width != NULL) *width = monwin->priv->geometry.width;
	if (height != NULL) *height = monwin->priv->geometry.height;
	if (phys_width_mm != NULL) *phys_width_mm = monwin->priv->physicalWidth;
	if (phys_height_mm != NULL) *phys_height_mm = monwin->priv->physicalHeight;

	return TRUE;
}

gint multimon_get_monitor_count(RemminaProtocolWidget *proto) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL, 0);
	return rcw_get_monitor_count(proto->cnnobj->cnnwin);
}

gboolean multimon_get_monitor_info(RemminaProtocolWidget *proto, gint index, gboolean*is_primary, gint*x, gint*y, gint*width, gint*height, gint*phys_width_mm, gint* phys_height_mm) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL, FALSE);
	return rcw_get_monitor_info(proto->cnnobj->cnnwin, index, is_primary, x, y, width, height, phys_width_mm, phys_height_mm);
}

int multimon_get_main_monitor_index(RemminaProtocolWidget *proto) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL
					&& proto->cnnobj->cnnwin->priv != NULL, -1);
	if (proto->cnnobj->cnnwin->priv->multimon_areas == NULL)
					return -1;

	RemminaConnectionWindowPriv *priv = proto->cnnobj->cnnwin->priv;
	for (gint i = 0 ; i<priv->multimon_areas->len ; i++) {
		MultimonWindow* win = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, i));
		if (win->priv->monitor == priv->multimon_main_monitor)
			return i;
	}
	return -1;
}

void multimon_set_monitor_drawing_area(RemminaProtocolWidget *proto, gint index, GtkWidget*drawing_area) {
	TRACE_CALL(__func__);
	g_return_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL
					&& proto->cnnobj->cnnwin->priv != NULL);
	if (proto->cnnobj->cnnwin->priv->multimon_areas == NULL || index >= proto->cnnobj->cnnwin->priv->multimon_areas->len)
		return;
	RemminaConnectionWindowPriv *priv = proto->cnnobj->cnnwin->priv;

	MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, index));
	if (monwin->priv->monitor == priv->multimon_main_monitor)
		return;
	if (monwin->priv->drawing_area != NULL) {
		gtk_widget_destroy(monwin->priv->drawing_area);
		monwin->priv->drawing_area = NULL;
	}
	monwin->priv->drawing_area = drawing_area;
	if (drawing_area != NULL) {
			gtk_container_add(GTK_CONTAINER(monwin), drawing_area);
	}
}

GtkWidget* multimon_get_monitor_drawing_area(RemminaProtocolWidget *proto, gint index) {
	TRACE_CALL(__func__);
	g_return_val_if_fail(proto != NULL && proto->cnnobj != NULL && proto->cnnobj->cnnwin != NULL
				     && proto->cnnobj->cnnwin->priv != NULL, NULL);
	if (proto->cnnobj->cnnwin->priv->multimon_areas == NULL || index >= proto->cnnobj->cnnwin->priv->multimon_areas->len)
		return NULL;
	RemminaConnectionWindowPriv *priv = proto->cnnobj->cnnwin->priv;

	MultimonWindow* monwin = RCW_MULTIMON(g_ptr_array_index(priv->multimon_areas, index));
	if (monwin->priv->monitor == priv->multimon_main_monitor)
		return NULL;
	if (monwin->priv->drawing_area == NULL) {
		monwin->priv->drawing_area = gtk_drawing_area_new();		
		gtk_container_add(GTK_CONTAINER(monwin), monwin->priv->drawing_area);
		gtk_widget_realize(monwin->priv->drawing_area);
		if (proto->cnnobj->connected)
			gtk_widget_show(GTK_WIDGET(monwin));
	}
	return monwin->priv->drawing_area;
}
