/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Written by: Matthias Clasen <mclasen@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <ibus.h>
/* http://code.google.com/p/ibus/issues/detail?id=1338 */
#define IBUS_COMPILATION
#include <ibusutil.h>
#undef IBUS_COMPILATION

#include <gdk/gdkx.h>
#include <libgnomekbd/gkbd-keyboard-drawing.h>

#include "gnome-region-panel-input.h"

#define WID(s) GTK_WIDGET(gtk_builder_get_object (builder, s))

/* TODO
 *
 * - There is a large overlap between xkb and m17n:kbd -
 *   we need to somehow decide and filter out
 *   cases to consider:
 *    Amharic: xkb:layout:et vs m17n:am:sera  - probably keep both
 *    Arabic: xkb:layout:ara vs m17n:ar:kbd   - probably not
 *
 * - Straighten out the naming. It needs to be either
 *   "Language" or "Language (Method)" when the same
 *   language occurs more than once. Need to decide what
 *   to do about xkb and m17n
 *
 * - Implement system-wide settings
 *
 * - Debug hotkeys, they don't act right
 *
 * - Allow changing shortcuts ?
 */


static GtkWidget *input_chooser_new          (GtkBuilder    *builder);
static gboolean   input_chooser_get_selected (GtkWidget     *chooser,
                                              GtkTreeModel **model,
                                              GtkTreeIter   *iter);

enum
{
  COL_NAME,
  COL_DESC,
  COL_LANG,
  COL_LAYOUT,
  COL_XKB
};

/* IBus interaction {{{1 */

static gchar *
engine_description (IBusEngineDesc *description, gboolean unique_lang)
{
  const gchar *lang;
  gchar *longname;
  gchar *desc;

  lang = ibus_get_language_name (ibus_engine_desc_get_language (description));

  if (g_getenv ("DEBUG_IBUS"))
    return g_strdup_printf ("%s %s/%s/%s/%s", lang,
                            ibus_engine_desc_get_name (description),
                            ibus_engine_desc_get_longname (description),
                            ibus_engine_desc_get_language (description),
                            ibus_engine_desc_get_layout (description));

  if (unique_lang)
    return g_strdup (lang);

  if (g_str_has_prefix (ibus_engine_desc_get_name (description), "xkb:layout:"))
    {
      if (strcmp (ibus_engine_desc_get_longname (description), lang) == 0)
        return g_strdup_printf ("%s (xkb)", lang);
      else
        return g_strdup (ibus_engine_desc_get_longname (description));
    }

  longname = g_strdup (ibus_engine_desc_get_longname (description));
  if (g_str_has_suffix (longname, " (m17n)"))
    {
       longname[strlen (longname) - strlen (" (m17n)")] = '\0';
    }

  desc = g_strdup_printf ("%s (%s)", lang, longname);

  g_free (longname);

  return desc;
}

static void
populate_model (GtkListStore *store,
                gboolean      only_active)
{
  IBusBus *bus;
  GList *list, *l;
  GList *active_list;
  IBusEngineDesc *description;
  const gchar *name;
  const gchar *lang;
  gchar *desc;
  gint count;
  GtkTreeIter iter;
  GHashTable *lang_hash;
  GHashTable *active_hash;

  bus = ibus_bus_new ();

  list = ibus_bus_list_engines (bus);
  active_list = ibus_bus_list_active_engines (bus);

  lang_hash = g_hash_table_new (g_str_hash, g_str_equal);
  active_hash = g_hash_table_new (g_str_hash, g_str_equal);

  /* Figure out which languages occur more than once
   * for these, showing only the language is not sufficient
   */
  for (l = list; l; l = l->next)
    {
      description = (IBusEngineDesc *)l->data;

      name = ibus_engine_desc_get_name (description);
      if (g_str_has_prefix (name, "xkb:layout:default:#"))
        continue;

      lang = ibus_engine_desc_get_language (description);
      lang = ibus_get_language_name (lang);
      if (lang == NULL)
        continue;

      count = GPOINTER_TO_INT (g_hash_table_lookup (lang_hash, lang));
      count++;
      g_hash_table_insert (lang_hash, (gpointer)lang, GINT_TO_POINTER (count));
    }

  if (only_active)
    {
      g_list_free (list);
      list = active_list;
      active_list = NULL;
    }
  else
    {
      /* We want to skip active engines when
       * making a list of inactive engines
       */
      for (l = active_list; l; l = l->next)
        {
          description = (IBusEngineDesc *)l->data;
          name = ibus_engine_desc_get_name (description);
          g_hash_table_insert (active_hash, (gpointer)name, (gpointer)name);
        }
      g_list_free (active_list);
      active_list = NULL;
    }

  for (l = list; l; l = l->next)
    {
      description = (IBusEngineDesc *)l->data;

      name = ibus_engine_desc_get_name (description);
      if (g_str_has_prefix (name, "xkb:layout:default:#"))
        continue;

      if (g_hash_table_lookup (active_hash, name))
        continue;

      lang = ibus_engine_desc_get_language (description);
      lang = ibus_get_language_name (lang);
      if (lang == NULL)
        continue;

      count = GPOINTER_TO_INT (g_hash_table_lookup (lang_hash, lang));
      desc = engine_description (description, count <= 1);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
                          COL_NAME, name,
                          COL_DESC, desc,
                          COL_LANG, lang,
                          COL_LAYOUT, ibus_engine_desc_get_layout (description),
                          COL_XKB, g_str_has_prefix (name, "xkb:layout:"),
                          -1);
      g_free (desc);
    }

  g_hash_table_unref (lang_hash);
  g_hash_table_unref (active_hash);

  g_list_free (list);

  g_object_unref (bus);
}

static void
update_ibus_configuration (GtkTreeModel *model)
{
  GtkTreeIter iter;
  IBusBus *bus;
  IBusConfig *config;
  gchar **active;
  gsize n_active;
  gchar *name;
  gint i;

  n_active = gtk_tree_model_iter_n_children (model, NULL);
  active = g_new0 (gchar *, n_active + 1);
  gtk_tree_model_get_iter_first (model, &iter);
  i = 0;
  do {
    gtk_tree_model_get (model, &iter, COL_NAME, &name, -1);
     active[i++] = name;
  } while (gtk_tree_model_iter_next (model, &iter));
  active[n_active] = NULL;

  bus = ibus_bus_new ();
  config = ibus_bus_get_config (bus);

  if (!ibus_config_set_value (config,
                              "general", "preload_engines",
                              g_variant_new_strv ((const gchar * const *)active, -1)))
    {
      g_warning ("Failed to update IBus configuration");
    }

  g_strfreev (active);

  g_object_unref (bus);
}

static void
get_shortcuts (gchar **previous, gchar **next)
{
  IBusBus *bus;
  IBusConfig *config;
  GVariant *value;
  const gchar **strv;
  gsize len;

  bus = ibus_bus_new ();
  config = ibus_bus_get_config (bus);

  *previous = NULL;
  *next = NULL;

  value = ibus_config_get_value (config, "general/hotkey", "previous_engine");
  if (value)
    {
       strv = g_variant_get_strv (value, &len);
       if (len > 0)
         *previous = g_strdup (strv[0]);
       g_free (strv);
       g_variant_unref (value);
    }

  value = ibus_config_get_value (config, "general/hotkey", "next_engine_in_menu");
  if (value)
    {
       strv = g_variant_get_strv (value, &len);
       if (len > 0)
         *next = g_strdup (strv[0]);
       g_free (strv);
       g_variant_unref (value);
    }

  g_debug ("Hotkeys: previous: %s next: %s\n", *previous, *next);

  g_object_unref (bus);
}

/* List handling {{{1 */

static gboolean
get_selected_iter (GtkBuilder    *builder,
                   GtkTreeModel **model,
                   GtkTreeIter   *iter)
{
  GtkTreeSelection *selection;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (WID ("active_input_sources")));

  return gtk_tree_selection_get_selected (selection, model, iter);
}

static gint
find_selected_layout_idx (GtkBuilder *builder)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreePath *path;
  gint idx;

  if (!get_selected_iter (builder, &model, &iter))
    return -1;

  path = gtk_tree_model_get_path (model, &iter);
  if (path == NULL)
    return -1;

  idx = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  return idx;
}

static void
update_button_sensitivity (GtkBuilder *builder)
{
  GtkWidget *remove_button;
  GtkWidget *up_button;
  GtkWidget *down_button;
  GtkWidget *show_button;
  GtkTreeView *tv;
  gint n_active;
  gint index;

  remove_button = WID("input_source_remove");
  show_button = WID("input_source_show");
  up_button = WID("input_source_move_up");
  down_button = WID("input_source_move_down");

  tv = GTK_TREE_VIEW (WID ("active_input_sources"));

  n_active = gtk_tree_model_iter_n_children (gtk_tree_view_get_model (tv), NULL);
  index = find_selected_layout_idx (builder);

  gtk_widget_set_sensitive (remove_button, index >= 0 && n_active > 1);
  gtk_widget_set_sensitive (show_button, index >= 0);
  gtk_widget_set_sensitive (up_button, index > 0);
  gtk_widget_set_sensitive (down_button, index >= 0 && index < n_active - 1);
}

static void
set_selected_path (GtkBuilder  *builder,
                   GtkTreePath *path)
{
  GtkTreeSelection *selection;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (WID ("active_input_sources")));

  gtk_tree_selection_select_path (selection, path);
}

static void
chooser_response (GtkWidget *chooser, gint response_id, gpointer data)
{
  GtkBuilder *builder = data;

  if (response_id == GTK_RESPONSE_OK)
    {
      GtkTreeModel *model;
      GtkTreeIter iter;

      if (input_chooser_get_selected (chooser, &model, &iter))
        {
          GtkTreeView *my_tv;
          GtkListStore *my_model;
          GtkTreeIter my_iter;
          gchar *name;
          gchar *desc;
          gchar *lang;
          gchar *layout;
          gboolean xkb;

          gtk_tree_model_get (model, &iter,
                              COL_NAME, &name,
                              COL_DESC, &desc,
                              COL_LANG, &lang,
                              COL_LAYOUT, &layout,
                              COL_XKB, &xkb,
                              -1);

          my_tv = GTK_TREE_VIEW (WID ("active_input_sources"));
          my_model = GTK_LIST_STORE (gtk_tree_view_get_model (my_tv));

          gtk_list_store_append (my_model, &my_iter);

          gtk_list_store_set (my_model, &my_iter,
                              COL_NAME, name,
                              COL_DESC, desc,
                              COL_LANG, lang,
                              COL_LAYOUT, layout,
                              COL_XKB, xkb,
                              -1);

          g_free (name);
          g_free (desc);
          g_free (lang);
          g_free (layout);

          gtk_tree_selection_select_iter (gtk_tree_view_get_selection (my_tv), &my_iter);

          update_button_sensitivity (builder);
          update_ibus_configuration (GTK_TREE_MODEL (my_model));
        }
      else
        {
          g_debug ("nothing selected, nothing added");
        }
    }

  gtk_widget_destroy (GTK_WIDGET (chooser));
}

static void
add_input (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkWidget *chooser;

  g_debug ("add an input source");

  chooser = input_chooser_new (builder);
  g_signal_connect (chooser, "response",
                    G_CALLBACK (chooser_response), builder);
}

static void
remove_selected_input (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;

  g_debug ("remove selected input source");

  if (get_selected_iter (builder, &model, &iter) == FALSE)
    return;

  path = gtk_tree_model_get_path (model, &iter);
  gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

  if (!gtk_tree_model_get_iter (model, &iter, path))
    gtk_tree_path_prev (path);

  set_selected_path (builder, path);

  gtk_tree_path_free (path);

  update_button_sensitivity (builder);
  update_ibus_configuration (model);
}

static void
move_selected_input_up (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkTreeModel *model;
  GtkTreeIter iter, prev;
  GtkTreePath *path;

  g_debug ("move selected input source up");

  if (!get_selected_iter (builder, &model, &iter))
    return;

  prev = iter;
  if (!gtk_tree_model_iter_previous (model, &prev))
    return;

  path = gtk_tree_model_get_path (model, &prev);

  gtk_list_store_swap (GTK_LIST_STORE (model), &iter, &prev);
  set_selected_path (builder, path);
  gtk_tree_path_free (path);

  update_button_sensitivity (builder);
  update_ibus_configuration (model);
}

static void
move_selected_input_down (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkTreeModel *model;
  GtkTreeIter iter, next;
  GtkTreePath *path;

  g_debug ("move selected input source down");

  if (!get_selected_iter (builder, &model, &iter))
    return;

  next = iter;
  if (!gtk_tree_model_iter_next (model, &next))
    return;

  path = gtk_tree_model_get_path (model, &next);
  gtk_list_store_swap (GTK_LIST_STORE (model), &iter, &next);
  set_selected_path (builder, path);
  gtk_tree_path_free (path);

  update_button_sensitivity (builder);
  update_ibus_configuration (model);
}

static void
show_selected_layout (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkWidget *parent;
  GtkWidget *popup;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *layout;
  gchar *desc;
  gchar *title;
  XklEngine *engine;
  XklConfigRegistry *registry;

  g_debug ("show selected layout");

  if (!get_selected_iter (builder, &model, &iter))
    return;

  gtk_tree_model_get (model, &iter,
                      COL_LAYOUT, &layout,
                      COL_DESC, &desc,
                      -1);

  parent = WID ("region_notebook");
  popup = gkbd_keyboard_drawing_dialog_new ();

  engine = xkl_engine_get_instance (GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (parent)));
  registry = xkl_config_registry_get_instance (engine);

  gkbd_keyboard_drawing_dialog_set_layout (popup, registry, layout);

  g_object_unref (registry);

  title = g_strdup_printf (_("Keyboard layout for %s"), desc);
  gtk_window_set_title (GTK_WINDOW (popup), title);
  g_free (title);

  gtk_window_set_transient_for (GTK_WINDOW (popup),
                                GTK_WINDOW (gtk_widget_get_toplevel (parent)));
  gtk_widget_show_all (popup);

  g_free (layout);
  g_free (desc);
}

/* Main setup {{{1 */

static gboolean
go_to_shortcuts (GtkLinkButton *button,
                 CcRegionPanel *panel)
{
  CcShell *shell;
  const gchar *argv[] = { "shortcuts", NULL };
  GError *error = NULL;

  shell = cc_panel_get_shell (CC_PANEL (panel));
  if (!cc_shell_set_active_panel_from_id (shell, "keyboard", argv, &error))
    {
      g_warning ("Failed to activate Keyboard panel: %s", error->message);
      g_error_free (error);
    }

  return TRUE;
}

void
setup_input_tabs (GtkBuilder    *builder,
                  CcRegionPanel *panel)
{
  GtkWidget *treeview;
  GtkTreeViewColumn *column;
  GtkCellRenderer *cell;
  GtkListStore *store;
  GtkTreeSelection *selection;
  gchar *previous = NULL;
  gchar *next = NULL;
  GtkWidget *label;

  ibus_init ();

  /* set up the list of active inputs */
  treeview = WID("active_input_sources");
  column = gtk_tree_view_column_new ();
  cell = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell, "text", COL_DESC);
  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

  /* id, name, language, layout, xkb? */
  store = gtk_list_store_new (5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);

  populate_model (store, TRUE);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  g_signal_connect_swapped (selection, "changed",
                            G_CALLBACK (update_button_sensitivity), builder);

  gtk_tree_view_set_model (GTK_TREE_VIEW (treeview), GTK_TREE_MODEL (store));

  /* set up the buttons */
  g_signal_connect (WID("input_source_add"), "clicked",
                    G_CALLBACK (add_input), builder);
  g_signal_connect (WID("input_source_remove"), "clicked",
                    G_CALLBACK (remove_selected_input), builder);
  g_signal_connect (WID("input_source_move_up"), "clicked",
                    G_CALLBACK (move_selected_input_up), builder);
  g_signal_connect (WID("input_source_move_down"), "clicked",
                    G_CALLBACK (move_selected_input_down), builder);
  g_signal_connect (WID("input_source_show"), "clicked",
                    G_CALLBACK (show_selected_layout), builder);

  /* show the right shortcuts */
  get_shortcuts (&previous, &next);

  /* use an em dash is no shortcut */
  if (!previous)
    previous = g_strdup ("\342\200\224");
  if (!next)
    next = g_strdup ("\342\200\224");

  label = WID("prev-source-shortcut-label");
  gtk_label_set_label (GTK_LABEL (label), previous);
  label = WID("next-source-shortcut-label");
  gtk_label_set_label (GTK_LABEL (label), next);

  g_free (previous);
  g_free (next);

  g_signal_connect (WID("jump-to-shortcuts"), "activate-link",
                    G_CALLBACK (go_to_shortcuts), panel);
}

/* Chooser dialog {{{1 */

static void
filter_clear (GtkEntry             *entry,
              GtkEntryIconPosition  icon_pos,
              GdkEvent             *event,
              gpointer              user_data)
{
  gtk_entry_set_text (entry, "");
}

static gchar **search_pattern_list;

static void
filter_changed (GtkBuilder *builder)
{
  GtkTreeModelFilter *filtered_model;
  GtkWidget *filter_entry;
  const gchar *pattern;
  gchar *upattern;

  filtered_model = GTK_TREE_MODEL_FILTER (gtk_builder_get_object (builder,
                                                   "filtered_input_source_model"));
  filter_entry = WID ("input_source_filter");
  pattern = gtk_entry_get_text (GTK_ENTRY (filter_entry));
  upattern = g_utf8_strup (pattern, -1);
  if (!g_strcmp0 (pattern, ""))
    g_object_set (G_OBJECT (filter_entry),
                  "secondary-icon-name", "edit-find-symbolic",
                  "secondary-icon-activatable", FALSE,
                  "secondary-icon-sensitive", FALSE,
                  NULL);
  else
    g_object_set (G_OBJECT (filter_entry),
                  "secondary-icon-name", "edit-clear-symbolic",
                  "secondary-icon-activatable", TRUE,
                  "secondary-icon-sensitive", TRUE,
                  NULL);

  if (search_pattern_list != NULL)
    g_strfreev (search_pattern_list);

  search_pattern_list = g_strsplit (upattern, " ", -1);
  g_free (upattern);

  gtk_tree_model_filter_refilter (filtered_model);
}

static void
selection_changed (GtkTreeSelection *selection,
                   GtkBuilder       *builder)
{
  GList *selected;

  selected = gtk_tree_selection_get_selected_rows (selection, NULL);
  gtk_widget_set_sensitive (WID ("ok-button"), selected != NULL);
}

static void
row_activated (GtkTreeView       *tree_view,
               GtkTreePath       *path,
               GtkTreeViewColumn *column,
               GtkBuilder        *builder)
{
  GtkWidget *add_button;
  GtkWidget *dialog;

  add_button = WID ("ok-button");
  dialog = WID ("input_source_chooser");
  if (gtk_widget_is_sensitive (add_button))
    gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

static gboolean
filter_func (GtkTreeModel *model,
             GtkTreeIter  *iter,
             gpointer      data)
{
  gchar *desc = NULL;
  gchar **pattern;
  gboolean rv = TRUE;

  if (search_pattern_list == NULL || search_pattern_list[0] == NULL)
    return TRUE;

  gtk_tree_model_get (model, iter,
                      COL_DESC, &desc,
                      -1);

  pattern = search_pattern_list;
  do {
    gboolean is_pattern_found = FALSE;
    gchar *udesc = g_utf8_strup (desc, -1);
    if (udesc != NULL && g_strstr_len (udesc, -1, *pattern))
      {
        is_pattern_found = TRUE;
      }
    g_free (udesc);

    if (!is_pattern_found)
      {
        rv = FALSE;
        break;
      }

  } while (*++pattern != NULL);

  g_free (desc);

  return rv;
}

static GtkWidget *
input_chooser_new (GtkBuilder *main_builder)
{
  GtkBuilder *builder;
  GtkWidget *chooser;
  GtkWidget *filtered_list;
  GtkWidget *filter_entry;
  GtkTreeViewColumn *visible_column;
  GtkTreeSelection *selection;
  GtkListStore *model;
  GtkTreeModelFilter *filtered_model;

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder,
                             GNOMECC_UI_DIR "/gnome-region-panel-input-chooser.ui",
                             NULL);
  chooser = WID ("input_source_chooser");
  filtered_list = WID ("filtered_input_source_list");
  filter_entry = WID ("input_source_filter");

  g_object_set_data (G_OBJECT (chooser),
                     "filtered_input_source_list", filtered_list);
  visible_column =
    gtk_tree_view_column_new_with_attributes ("Layout",
                                              gtk_cell_renderer_text_new (),
                                              "text", COL_DESC,
                                              NULL);

  gtk_window_set_transient_for (GTK_WINDOW (chooser),
                                GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (gtk_builder_get_object (main_builder, "region_notebook")))));

  gtk_tree_view_append_column (GTK_TREE_VIEW (filtered_list),
                               visible_column);
  g_signal_connect_swapped (G_OBJECT (filter_entry), "notify::text",
                            G_CALLBACK (filter_changed), builder);

  g_signal_connect (G_OBJECT (filter_entry), "icon-release",
                    G_CALLBACK (filter_clear), NULL);

  selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (filtered_list));

  g_signal_connect (G_OBJECT (selection), "changed",
                    G_CALLBACK (selection_changed), builder);

  selection_changed (selection, builder);

  g_signal_connect (G_OBJECT (filtered_list), "row-activated",
                    G_CALLBACK (row_activated), builder);

  filtered_model =
    GTK_TREE_MODEL_FILTER (gtk_builder_get_object (builder, "filtered_input_source_model"));
  model =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "input_source_model"));

  populate_model (model, FALSE);

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
                                        COL_DESC, GTK_SORT_ASCENDING);

  gtk_tree_model_filter_set_visible_func (filtered_model,
                                          filter_func,
                                          NULL, NULL);

  gtk_widget_grab_focus (filter_entry);

  gtk_widget_show (chooser);

  return chooser;
}

static gboolean
input_chooser_get_selected (GtkWidget     *dialog,
                            GtkTreeModel **model,
                            GtkTreeIter   *iter)
{
  GtkWidget *tv;
  GtkTreeSelection *selection;

  tv = g_object_get_data (G_OBJECT (dialog), "filtered_input_source_list");
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));

  return gtk_tree_selection_get_selected (selection, model, iter);
}
/* Epilogue {{{1 */
/* vim: set foldmethod=marker: */