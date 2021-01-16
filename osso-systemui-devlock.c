/*
 * osso-systemui-devlock.c
 *
 * Copyright (C) 2021 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <codelockui.h>
#include <dbus/dbus.h>
#include <gconf/gconf-client.h>
#include <glib.h>
#include <hildon/hildon.h>
#include <libdevlock.h>
#include <systemui.h>

#include <libintl.h>

#include "devlock-dbus-names.h"

#define _(msgid) dgettext("osso-system-lock", msgid)

#define SYSTEMUI_GCONF_DEVLOCK_DIR SYSTEMUI_GCONF_DIR "devlock/"

#define DEVLOCK_GCONF_WINDOW_PRIORITY SYSTEMUI_GCONF_DEVLOCK_DIR "window_priority"
#define DEVLOCK_GCONF_WINDOW_PRIORITY_PINQUERY SYSTEMUI_GCONF_DEVLOCK_DIR "window_priority_pinquery"
#define DEVLOCK_GCONF_TEXT_ENTRY_TIMEOUT SYSTEMUI_GCONF_DEVLOCK_DIR "text_entry_timeout"
#define DEVLOCK_GCONF_AUTOLOCK_ENABLED SYSTEMUI_GCONF_DEVLOCK_DIR "devicelock_autolock_enabled"

#define DEVLOCK_WINDOW_PRIORITY_DEFAULT 120
#define DEVLOCK_TEXT_ENTRY_TIMEOUT_DEFAULT 5000

static gboolean dbus_filter_added;
static gboolean in_em_call;

static system_ui_data *ui = NULL;
static system_ui_callback_t devlock_callback;

static gint devicelock_priority;
static gint code_query_priority;
static gint code_entry_timeout;
static gboolean session_under_way;
static gboolean under_way = FALSE;
static gboolean under_way_status = FALSE;
static gboolean cpa_devicelock_password_changed = FALSE;

static CodeLockUI *CdUi = NULL;
static GtkWidget *devicelock = NULL;
static GtkWidget *note = NULL;

/* FIXME - ofono */
#define PHONE_CALL_DBUS_FILTER "type='signal',interface='com.nokia.csd.Call.Instance',member='CallStatus'"

/* taken from bluez, remove for ofono */
#define CSD_CALL_STATUS_MO_RELEASE 9
#define CSD_CALL_STATUS_MT_RELEASE 10
#define CSD_CALL_STATUS_TERMINATED 15

static DBusHandlerResult
_dbus_filter(DBusConnection *connection, DBusMessage *message, void *user_data)
{
  if (in_em_call)
  {
    if (dbus_message_get_type(message) == 4 &&
        !strcmp(dbus_message_get_interface(message),
                "com.nokia.csd.Call.Instance") &&
        !strcmp(dbus_message_get_member(message), "CallStatus"))
    {
      dbus_uint32_t cause = 0;
      dbus_uint32_t status = 0;
      dbus_uint32_t cause_type = 0;

      dbus_message_get_args(message, NULL,
                            DBUS_TYPE_UINT32, &status,
                            DBUS_TYPE_UINT32, &cause_type,
                            DBUS_TYPE_UINT32, &cause,
                            DBUS_TYPE_INVALID);

      if (status == CSD_CALL_STATUS_MO_RELEASE ||
          status == CSD_CALL_STATUS_MT_RELEASE)
      {
        codelock_clear_code(CdUi);
        codelock_set_emergency_mode(CdUi, FALSE);
        in_em_call = FALSE;
      }
      else if (status == CSD_CALL_STATUS_TERMINATED)
      {
        if (cause_type == 3 || cause != 3)
        {
          hildon_banner_show_information_override_dnd(
                CdUi->dialog,_("lock_error_emergencycallfails"));
          in_em_call = FALSE;
        }

        codelock_clear_code(CdUi);
        codelock_set_emergency_mode(CdUi, FALSE);
      }
    }
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
remove_dbus_filer()
{
  if (dbus_filter_added)
  {
    dbus_connection_remove_filter(ui->system_bus, _dbus_filter, NULL);
    dbus_bus_remove_match(ui->system_bus, PHONE_CALL_DBUS_FILTER, NULL);
  }

  dbus_filter_added = FALSE;
}

static int
devlock_close(const char *interface, const char *method, GArray *args,
              system_ui_data *data, system_ui_handler_arg *result)
{
  if (CdUi && CdUi->dialog)
      WindowPriority_HideWindow(CdUi->dialog);

  if (devicelock)
    WindowPriority_HideWindow(devicelock);

  if (note)
    WindowPriority_HideWindow(note);

  remove_dbus_filer();

  if (CdUi && CdUi->dialog)
  {
    codelock_destroy_dialog(CdUi);
    g_free(CdUi);
    CdUi = NULL;
  }

  if (devicelock)
  {
    gtk_widget_destroy(devicelock);
    devicelock = NULL;
  }

  if (note)
  {
    gtk_object_destroy(&note->object);
    note = NULL;
  }

  systemui_free_callback(&devlock_callback);

  return DBUS_TYPE_VARIANT;
}

static void
_note_set_geometry_hints()
{
  GdkGeometry geometry;

  if (!note)
    return;

  geometry.base_width = note->requisition.width;
  geometry.max_width = geometry.base_width;
  geometry.min_width = geometry.base_width;
  geometry.base_height = note->requisition.height;
  geometry.width_inc = 0;
  geometry.max_height = geometry.base_height;
  geometry.min_height = geometry.base_height;
  geometry.height_inc = 0;

  gtk_window_set_geometry_hints(
        GTK_WINDOW(note), note, &geometry,
        GDK_HINT_RESIZE_INC | GDK_HINT_BASE_SIZE | GDK_HINT_MAX_SIZE |
                                                             GDK_HINT_MIN_SIZE);
}

static void
response_cb(GtkDialog *dialog, int response_id, gpointer user_data)
{
  if (response_id == GTK_RESPONSE_DELETE_EVENT)
    g_signal_stop_emission_by_name(dialog, "response");
}

static void
_catch_dialog_delete_event(GtkWidget *dialog)
{
  g_return_if_fail(GTK_IS_DIALOG(dialog));

  g_signal_connect(dialog, "delete-event", G_CALLBACK(gtk_true), NULL);
  g_signal_connect(dialog, "response", G_CALLBACK(response_cb), NULL);
}

static void
devlock_do_callback(gint argc, system_ui_data *data)
{
  switch (argc)
  {
    case DEVLOCK_RESPONSE_CORRECT:
    {
      WindowPriority_HideWindow(CdUi->dialog);
      WindowPriority_HideWindow(devicelock);
    }
    case DEVLOCK_RESPONSE_CANCEL:
    case DEVLOCK_RESPONSE_INCORRECT:
    case DEVLOCK_RESPONSE_NOSHUTDOWN:
    case DEVLOCK_RESPONSE_LOCKED:
    {
      codelock_clear_code(CdUi);
      break;
    }
    case DEVLOCK_RESPONSE_SHUTDOWN:
    {
      if (note)
        gtk_object_destroy(&note->object);

      break;
    }
    default:
      WindowPriority_HideWindow(CdUi->dialog);
      codelock_clear_code(CdUi);
      break;
  }

  systemui_do_callback(data, &devlock_callback, argc);
}

static void
cdui_response_cb(GtkDialog *dialog, gint response_id, gpointer user_data)
{
  if (dialog == (GtkDialog *)note)
  {
    if (response_id == GTK_RESPONSE_OK)
      devlock_do_callback(DEVLOCK_RESPONSE_SHUTDOWN, ui);
    else
      devlock_do_callback(DEVLOCK_RESPONSE_NOSHUTDOWN, ui);
  }
  else if (response_id == GTK_RESPONSE_OK)
  {
    if (codelock_is_passwd_correct(codelock_get_code(CdUi)))
    {
      gconf_client_set_bool(
            ui->gc_client, "/system/osso/dsm/locks/devicelock_password_changed",
            FALSE, NULL);

      if (under_way)
      {
        hildon_banner_show_information_override_dnd(
              NULL, _("secu_info_codeaccepted"));
        devlock_do_callback(DEVLOCK_RESPONSE_CORRECT, ui);
      }
      else
      {
        under_way = TRUE;
        session_under_way = TRUE;
        WindowPriority_ShowWindow(devicelock, devicelock_priority);

        gconf_client_set_bool(
          ui->gc_client, "/system/systemui/devlock/devicelock_autolock_enabled",
          session_under_way, NULL);
        hildon_banner_show_information_override_dnd(CdUi->dialog,
                                                    _("secu_info_locked"));
        WindowPriority_ShowWindow(CdUi->dialog, code_query_priority);
        devlock_do_callback(DEVLOCK_RESPONSE_LOCKED, ui);
      }
    }
    else if (under_way)
    {
      hildon_banner_show_information_override_dnd(NULL,
                                                  _("secu_info_incorrectcode"));
      devlock_do_callback(DEVLOCK_RESPONSE_INCORRECT, ui);
    }
    else
    {
      codelock_clear_code(CdUi);
      hildon_banner_show_information_override_dnd(NULL,
                                                  _("secu_info_incorrectcode"));
    }
  }
  else
  {
    if (response_id != GTK_RESPONSE_CANCEL &&
        response_id != GTK_RESPONSE_DELETE_EVENT)
    {
      if (response_id == 100)
      {
        g_signal_stop_emission_by_name(CdUi->dialog, "response");
        in_em_call = TRUE;
      }
    }
    else
    {
      gconf_client_set_bool(
            ui->gc_client,
            "/system/osso/dsm/locks/devicelock_password_changed",
            cpa_devicelock_password_changed, NULL);

      if (under_way)
        devlock_do_callback(DEVLOCK_RESPONSE_CANCEL, ui);
      else
      {
        devlock_do_callback(DEVLOCK_RESPONSE_CORRECT, ui);
        hildon_banner_show_information_override_dnd(NULL,
                                                    _("secu_info_notlocked"));
      }
    }
  }
}

static void
_create_codelock_ui()
{
  if (CdUi)
    return;

  CdUi = g_new0(CodeLockUI, 1);
  codelock_create_dialog(CdUi, code_entry_timeout, TRUE);
  codelock_set_max_code_length(CdUi, 10);

  g_signal_connect(CdUi->dialog, "response",
                   G_CALLBACK(cdui_response_cb), NULL);

  _catch_dialog_delete_event(CdUi->dialog);

  if (!dbus_filter_added)
  {
    DBusError error = DBUS_ERROR_INIT;

    dbus_filter_added = TRUE;
    dbus_error_init(&error);
    dbus_bus_add_match(ui->system_bus,
                       "type='signal',interface='com.nokia.csd.Call.Instance',member='CallStatus'",
                       &error);

    if (dbus_error_is_set(&error))
      dbus_error_free(&error);
    else
      dbus_connection_add_filter(ui->system_bus, _dbus_filter, NULL, NULL);
  }
}

static void
_create_devlock()
{
  if (!devicelock)
  {
    GdkColor color = {};

    devicelock = gtk_dialog_new_with_buttons("devlock_bg", NULL,
                                             GTK_DIALOG_NO_SEPARATOR, NULL);
    gtk_window_set_decorated(GTK_WINDOW(devicelock), FALSE);
    gtk_widget_modify_bg(devicelock, GTK_STATE_NORMAL, &color);
    gtk_window_fullscreen(GTK_WINDOW(devicelock));
    gtk_widget_realize(devicelock);
  }
}

static int
devlock_enable(const char *interface, const char *method, GArray *args,
               system_ui_data *data, system_ui_handler_arg *result)
{
  int supported_args[1] = {'u'};
  system_ui_handler_arg *hargs;
  devlock_query_mode mode;

  if (!systemui_check_plugin_arguments(args, supported_args, 1))
    return 0;

  hargs = ((system_ui_handler_arg *)args->data);
  mode = hargs[4].data.i32;

  _create_codelock_ui();
  _create_devlock();

  session_under_way = gconf_client_get_bool(
        ui->gc_client, DEVLOCK_GCONF_AUTOLOCK_ENABLED, NULL);

  cpa_devicelock_password_changed =
      gconf_client_get_bool(
        ui->gc_client, "/system/osso/dsm/locks/devicelock_password_changed",
        NULL);

  if (!session_under_way)
  {
    get_autolock_key(&under_way);
    under_way_status = under_way;
  }
  else {
    under_way_status = TRUE;
  }

  under_way = under_way_status;

  switch (mode)
  {
    case DEVLOCK_QUERY_CLOSE:
    {
      if (under_way &&
          WindowPriority_ShowWindow(devicelock, devicelock_priority))
      {
        hildon_banner_show_information_override_dnd(devicelock,
                                                    _("secu_info_locked"));
      }

      WindowPriority_HideWindow(CdUi->dialog);

      if (note)
        WindowPriority_HideWindow(note);

      codelock_destroy_dialog(CdUi);
      break;
    }
    case DEVLOCK_QUERY_OPEN:
    {
      if (under_way)
        WindowPriority_ShowWindow(devicelock, devicelock_priority);

      WindowPriority_ShowWindow(CdUi->dialog, code_query_priority);

      if (note)
        WindowPriority_HideWindow(note);

      codelock_disable_input(CdUi, TRUE);
      break;
    }
    case DEVLOCK_QUERY_ENABLE:
    {
      if (cpa_devicelock_password_changed)
      {
        under_way = FALSE;
        gtk_widget_show_all(CdUi->dialog);
      }
      else
      {
        under_way = under_way_status;

        if (!under_way_status)
          gtk_widget_show_all(CdUi->dialog);
        else
        {
          WindowPriority_ShowWindow(devicelock, devicelock_priority);
          WindowPriority_ShowWindow(CdUi->dialog, code_query_priority);
        }
      }

      if (under_way)
      {
        hildon_banner_show_information_override_dnd(CdUi->dialog,
                                                    _("secu_info_locked"));
      }

      if (note)
        WindowPriority_HideWindow(note);

      codelock_disable_input(CdUi, FALSE);

      break;
    }
    case DEVLOCK_QUERY_HIDE:
    {
      if (under_way &&
          WindowPriority_ShowWindow(devicelock, devicelock_priority))
      {
        hildon_banner_show_information_override_dnd(devicelock,
                                                    _("secu_info_locked"));
      }

      WindowPriority_HideWindow(CdUi->dialog);

      if (note)
        WindowPriority_HideWindow(note);

      break;
    }
    case DEVLOCK_QUERY_NOTE:
    {
      if (under_way &&
          WindowPriority_ShowWindow(devicelock, devicelock_priority))
      {
        hildon_banner_show_information_override_dnd(devicelock,
                                                    _("secu_info_locked"));
      }

      WindowPriority_ShowWindow(CdUi->dialog, code_query_priority);

      if (!note)
      {
        note = hildon_note_new_confirmation(GTK_WINDOW(CdUi->dialog),
                                            _("secu_info_closedevice"));
        g_signal_connect(note, "response", G_CALLBACK(cdui_response_cb), NULL);
        g_signal_connect(note, "destroy",
                         G_CALLBACK(WindowPriority_HideWindow), NULL);

        g_object_weak_ref(G_OBJECT(note), (GWeakNotify)g_nullify_pointer,
                          &note);
        gtk_widget_realize(note);

        _note_set_geometry_hints();
      }

      WindowPriority_ShowWindow(note, code_query_priority + 1);
      gtk_grab_remove(note);
      codelock_disable_input(CdUi, FALSE);

      break;
    }
    case DEVLOCK_QUERY_ENABLE_QUIET:
    {
      WindowPriority_ShowWindow(CdUi->dialog, code_query_priority);

      if (note)
        WindowPriority_HideWindow(note);

      codelock_disable_input(CdUi, FALSE);

      break;
    }
    default:
      return 0;
  }

  if (!under_way)
  {
    hildon_banner_show_information_override_dnd(CdUi->dialog,
                                                _("secu_info_enterlock"));
  }

  if (cpa_devicelock_password_changed)
  {
    gconf_client_set_bool(
          ui->gc_client, "/system/osso/dsm/locks/devicelock_password_changed",
          FALSE, NULL);
  }

  if (systemui_check_set_callback(args, &devlock_callback))
  {
    if (under_way)
      result->data.i32 = DEVLOCK_REPLY_LOCKED;
    else
      result->data.i32 = DEVLOCK_REPLY_VERIFY;
  }
  else
    result->data.i32 = DEVLOCK_REPLY_FAILED;

  return DBUS_TYPE_INT32;
}

gboolean
plugin_init(system_ui_data *data)
{
  ui = data;
  dbus_filter_added = FALSE;

  g_return_val_if_fail(ui != NULL, FALSE);

  devicelock_priority = gconf_client_get_int(
        data->gc_client, DEVLOCK_GCONF_WINDOW_PRIORITY, NULL);
  code_query_priority = gconf_client_get_int(
        ui->gc_client, DEVLOCK_GCONF_WINDOW_PRIORITY_PINQUERY, NULL);
  code_entry_timeout = gconf_client_get_int(
        ui->gc_client, DEVLOCK_GCONF_TEXT_ENTRY_TIMEOUT, NULL);

  if (!devicelock_priority)
    devicelock_priority = DEVLOCK_WINDOW_PRIORITY_DEFAULT;

  if (!code_query_priority)
    code_query_priority = DEVLOCK_WINDOW_PRIORITY_DEFAULT + 1;

  if (!code_entry_timeout)
    code_entry_timeout = DEVLOCK_TEXT_ENTRY_TIMEOUT_DEFAULT;

  session_under_way = gconf_client_get_bool(
        ui->gc_client, DEVLOCK_GCONF_AUTOLOCK_ENABLED, NULL);

  systemui_add_handler(SYSTEMUI_DEVLOCK_OPEN_REQ, devlock_enable, ui);
  systemui_add_handler(SYSTEMUI_DEVLOCK_CLOSE_REQ, devlock_close, ui);

  return TRUE;
}

void
plugin_close(system_ui_data *data)
{
  g_return_if_fail(data);

  devlock_close(NULL, NULL, NULL, NULL, NULL);
  systemui_remove_handler(SYSTEMUI_DEVLOCK_OPEN_REQ, data);
  systemui_remove_handler(SYSTEMUI_DEVLOCK_CLOSE_REQ, data);
}
