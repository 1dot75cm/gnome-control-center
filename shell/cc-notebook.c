/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright © 2012 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *      Bastien Nocera <hadess@hadess.net>
 *      Emmanuele Bassi <ebassi@linux.intel.com>
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "cc-notebook.h"

#define GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), CC_TYPE_NOTEBOOK, CcNotebookPrivate))

/*
 * Structure:
 *
 *   Notebook
 *   +---- GtkClutterEmbed
 *         +---- ClutterStage
 *               +---- ClutterScrollActor:scroll
 *                     +---- ClutterActor:bin
 *                           +---- ClutterActor:frame<ClutterBinLayout>
 *                                 +---- GtkClutterActor:embed<GtkWidget>
 *
 * the frame element is needed to make the GtkClutterActor contents fill the allocation
 */

struct _CcNotebookPrivate
{
        GtkWidget *embed;

        ClutterActor *stage;
        ClutterActor *scroll;
        ClutterActor *bin;

        int last_width;

        GtkWidget *selected_page;
};

enum
{
        PROP_0,
        PROP_CURRENT_PAGE,
        LAST_PROP
};

static GParamSpec *obj_props[LAST_PROP] = { NULL, };

G_DEFINE_TYPE (CcNotebook, cc_notebook, GTK_TYPE_BOX)

static void
cc_notebook_get_property (GObject    *gobject,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
        CcNotebookPrivate *priv = CC_NOTEBOOK (gobject)->priv;

        switch (prop_id) {
        case PROP_CURRENT_PAGE:
                g_value_set_pointer (value, priv->selected_page);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
        }
}

static void
cc_notebook_set_property (GObject      *gobject,
			  guint         prop_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
        CcNotebook *self = CC_NOTEBOOK (gobject);

        switch (prop_id) {
        case PROP_CURRENT_PAGE:
                cc_notebook_select_page (self, g_value_get_pointer (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
        }
}

static GtkSizeRequestMode
cc_notebook_get_request_mode (GtkWidget *widget)
{
	CcNotebook *notebook;
	GtkWidget *target;

	notebook = CC_NOTEBOOK (widget);

	target = notebook->priv->selected_page ? notebook->priv->selected_page : widget;

	return GTK_WIDGET_GET_CLASS (target)->get_request_mode (target);
}

static void
cc_notebook_get_preferred_height (GtkWidget       *widget,
				  gint            *minimum_height,
				  gint            *natural_height)
{
	CcNotebook *notebook;
	GtkWidget *target;

	notebook = CC_NOTEBOOK (widget);

	target = notebook->priv->selected_page ? notebook->priv->selected_page : widget;

	GTK_WIDGET_GET_CLASS (target)->get_preferred_height (target, minimum_height, natural_height);
}

static void
cc_notebook_get_preferred_width_for_height (GtkWidget       *widget,
					    gint             height,
					    gint            *minimum_width,
					    gint            *natural_width)
{
	CcNotebook *notebook;
	GtkWidget *target;

	notebook = CC_NOTEBOOK (widget);

	target = notebook->priv->selected_page ? notebook->priv->selected_page : widget;

	GTK_WIDGET_GET_CLASS (target)->get_preferred_width_for_height (target, height, minimum_width, natural_width);
}

static void
cc_notebook_get_preferred_width (GtkWidget       *widget,
				 gint            *minimum_width,
				 gint            *natural_width)
{
	CcNotebook *notebook;
	GtkWidget *target;

	notebook = CC_NOTEBOOK (widget);

	target = notebook->priv->selected_page ? notebook->priv->selected_page : widget;

	GTK_WIDGET_GET_CLASS (target)->get_preferred_width (target, minimum_width, natural_width);
}

static void
cc_notebook_get_preferred_height_for_width (GtkWidget       *widget,
					    gint             width,
					    gint            *minimum_height,
					    gint            *natural_height)
{
	CcNotebook *notebook;
	GtkWidget *target;

	notebook = CC_NOTEBOOK (widget);

	target = notebook->priv->selected_page ? notebook->priv->selected_page : widget;

	GTK_WIDGET_GET_CLASS (target)->get_preferred_height_for_width (target, width, minimum_height, natural_height);
}

static void
cc_notebook_class_init (CcNotebookClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        g_type_class_add_private (klass, sizeof (CcNotebookPrivate));

        obj_props[PROP_CURRENT_PAGE] =
                g_param_spec_pointer (g_intern_static_string ("current-page"),
				      "Current Page",
				      "The currently selected page widget",
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

        gobject_class->get_property = cc_notebook_get_property;
        gobject_class->set_property = cc_notebook_set_property;
        g_object_class_install_properties (gobject_class, LAST_PROP, obj_props);

	widget_class->get_request_mode = cc_notebook_get_request_mode;
	widget_class->get_preferred_height = cc_notebook_get_preferred_height;
	widget_class->get_preferred_width_for_height = cc_notebook_get_preferred_width_for_height;
	widget_class->get_preferred_width = cc_notebook_get_preferred_width;
	widget_class->get_preferred_height_for_width = cc_notebook_get_preferred_height_for_width;
}

static void
on_embed_size_allocate (GtkWidget     *embed,
                        GtkAllocation *allocation,
                        CcNotebook  *self)
{
        ClutterActorIter iter;
        ClutterActor *child;
        ClutterActor *frame;
        float page_w, page_h;
        float offset = 0.f;
        ClutterPoint pos;

        if (self->priv->selected_page == NULL)
		return;

        page_w = allocation->width;
        page_h = allocation->height;

        clutter_actor_iter_init (&iter, self->priv->bin);
        while (clutter_actor_iter_next (&iter, &child)) {
                clutter_actor_set_x (child, offset);
                clutter_actor_set_size (child, page_w, page_h);

                offset += page_w;
        }

	/* This stops the non-animated scrolling from happening
	 * if we're still scrolling there */
	if (self->priv->last_width == allocation->width)
		return;

        self->priv->last_width = allocation->width;

	frame = g_object_get_data (G_OBJECT (self->priv->selected_page),
				   "cc-notebook-frame");

        pos.y = 0;
        pos.x = clutter_actor_get_x (frame);
        clutter_scroll_actor_scroll_to_point (CLUTTER_SCROLL_ACTOR (self->priv->scroll), &pos);
}

static void
cc_notebook_init (CcNotebook *self)
{
        self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CC_TYPE_NOTEBOOK, CcNotebookPrivate);

        self->priv->embed = gtk_clutter_embed_new ();
        gtk_widget_push_composite_child ();
        gtk_container_add (GTK_CONTAINER (self), self->priv->embed);
        gtk_widget_pop_composite_child ();
        g_signal_connect (self->priv->embed, "size-allocate", G_CALLBACK (on_embed_size_allocate), self);
        gtk_widget_show (self->priv->embed);

        self->priv->stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (self->priv->embed));

        self->priv->scroll = clutter_scroll_actor_new ();
        clutter_scroll_actor_set_scroll_mode (CLUTTER_SCROLL_ACTOR (self->priv->scroll),
                                                CLUTTER_SCROLL_HORIZONTALLY);
        clutter_actor_add_constraint (self->priv->scroll, clutter_bind_constraint_new (self->priv->stage, CLUTTER_BIND_SIZE, 0.f));
        clutter_actor_add_child (self->priv->stage, self->priv->scroll);

        self->priv->bin = clutter_actor_new ();
        clutter_actor_add_child (self->priv->scroll, self->priv->bin);

        self->priv->selected_page = NULL;
        gtk_widget_set_name (GTK_WIDGET (self), "GtkBox");
}

GtkWidget *
cc_notebook_new (void)
{
        return g_object_new (CC_TYPE_NOTEBOOK, NULL);
}

static void
_cc_notebook_select_page (CcNotebook *self,
			  GtkWidget  *widget,
			  int         index)
{
        ClutterPoint pos;

        g_return_if_fail (CC_IS_NOTEBOOK (self));
        g_return_if_fail (GTK_IS_WIDGET (widget));

        pos.y = 0;
        pos.x = self->priv->last_width * index;

        clutter_actor_save_easing_state (self->priv->scroll);
        clutter_actor_set_easing_duration (self->priv->scroll, 500);

        clutter_scroll_actor_scroll_to_point (CLUTTER_SCROLL_ACTOR (self->priv->scroll), &pos);

	clutter_actor_restore_easing_state (self->priv->scroll);

        /* Remember the last selected page */
        self->priv->selected_page = widget;

        g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CURRENT_PAGE]);
}

void
cc_notebook_select_page (CcNotebook *self,
                         GtkWidget  *widget)
{
	int i, n_children;
	GList *children, *l;
	ClutterActor *frame;
	gboolean found;

        if (widget == self->priv->selected_page)
		return;

	found = FALSE;
	frame = g_object_get_data (G_OBJECT (widget), "cc-notebook-frame");

        n_children = clutter_actor_get_n_children (self->priv->bin);
        children = clutter_actor_get_children (self->priv->bin);
        for (i = 0, l = children; i < n_children; i++, l = l->next) {
		if (frame == l->data) {
			_cc_notebook_select_page (self, widget, i);
			found = TRUE;
			break;
		}
	}
	g_list_free (children);
	if (found == FALSE)
		g_warning ("Could not find widget '%p' in CcNotebook '%p'", widget, self);
}

int
cc_notebook_add_page (CcNotebook *self,
                      GtkWidget  *widget)
{
        ClutterActor *frame;
        ClutterActor *embed;
        int res;

        g_return_val_if_fail (CC_IS_NOTEBOOK (self), -1);
        g_return_val_if_fail (GTK_IS_WIDGET (widget), -1);

        frame = clutter_actor_new ();
        clutter_actor_set_layout_manager (frame, clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_FILL,
                                                                         CLUTTER_BIN_ALIGNMENT_FILL));

        embed = gtk_clutter_actor_new_with_contents (widget);
        g_object_set_data (G_OBJECT (widget), "cc-notebook-frame", frame);
        clutter_actor_add_child (frame, embed);
        gtk_widget_show (widget);

        res = clutter_actor_get_n_children (self->priv->bin);
        clutter_actor_insert_child_at_index (self->priv->bin, frame, res);

        if (self->priv->selected_page == NULL)
		_cc_notebook_select_page (self, widget, res);

        return res;
}

void
cc_notebook_remove_page (CcNotebook *self,
                         GtkWidget  *widget)
{
        ClutterActorIter iter;
        ClutterActor *child;

        g_return_if_fail (CC_IS_NOTEBOOK (self));
        g_return_if_fail (GTK_IS_WIDGET (widget));
        g_return_if_fail (widget != self->priv->selected_page);

        clutter_actor_iter_init (&iter, self->priv->bin);
        while (clutter_actor_iter_next (&iter, &child)) {
                ClutterActor *embed = clutter_actor_get_child_at_index (child, 0);

                if (gtk_clutter_actor_get_contents (GTK_CLUTTER_ACTOR (embed)) == widget) {
                        clutter_actor_iter_remove (&iter);
                        /* FIXME reset scroll to current page */
                        return;
                }
        }
}

GtkWidget *
cc_notebook_get_selected_page (CcNotebook *self)
{
        g_return_val_if_fail (CC_IS_NOTEBOOK (self), NULL);

        return self->priv->selected_page;
}