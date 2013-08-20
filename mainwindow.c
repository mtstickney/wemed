/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>

#include <string.h>
#include <stdlib.h>
#include <gmime/gmime.h>
#include <webkit2/webkit2.h>
#include "mimeapp.h"
#include "wemedpanel.h"
#include "mimemodel.h"
#include "mainwindow.h"
#include "openwith.h"

struct WemedWindow_S {
	MimeModel* model;
	GtkWidget* rootwindow;
	GtkWidget* view;
	GtkWidget* panel;
	char* filename;
	gboolean dirty;
	GMimeObject* current_part;

	struct Application mime_app;
	GtkWidget* menu_part_edit;
};

static GMimeObject* selected_object(GtkTreeSelection* sel) {
	GtkTreeIter iter;
	GtkTreeModel* model;
	if(gtk_tree_selection_get_selected(sel, &model, &iter)) {
		gpointer v;
		gtk_tree_model_get(model, &iter, 1, &v, -1);
		return GMIME_OBJECT(v);
	}

	return NULL;
}

void tree_selection_changed_cb(GtkTreeSelection* selection, gpointer data) {
	printf("selection changed!\n");
	GtkTreeIter iter;
	GtkTreeModel *model;
	gpointer part;
	WemedWindow* w = (WemedWindow*) data;

	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gtk_tree_model_get(model, &iter, 1, &part, -1);
		GMimeObject* obj = (GMimeObject*) part;
		w->current_part = obj;
		load_document_part(WEMED_PANEL(w->panel), obj);
		free(w->mime_app.name);
		free(w->mime_app.exec);
		w->mime_app = get_default_mime_app(mime_model_content_type(w->current_part));
		if(w->mime_app.exec) {
			const char* editwith = "_Edit with %s";
			char* label = malloc(strlen(editwith) + strlen(w->mime_app.name));
			sprintf(label, editwith, w->mime_app.name);
			gtk_menu_item_set_label(GTK_MENU_ITEM(w->menu_part_edit), label);
			gtk_widget_set_sensitive(w->menu_part_edit, TRUE);
		} else {
			gtk_widget_set_sensitive(w->menu_part_edit, FALSE);

		}
	}

}


gboolean menu_save_as(GtkMenuItem* item, WemedWindow* w) {
	(void) item; // unused
	gboolean ret = TRUE;
	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Save File", GTK_WINDOW(w->rootwindow), GTK_FILE_CHOOSER_ACTION_SAVE, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), w->filename?:"untitled.eml");

	ret = (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT);
	if(ret) {
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		ret = mime_model_write_to_file(w->model, filename);
		if(ret) {
			w->dirty = FALSE;
			w->filename = strdup(filename);
		}
		g_free(filename);
	}
	gtk_widget_destroy (dialog);
	return ret;
}

gboolean menu_save(GtkMenuItem* item, WemedWindow* w) {
	gboolean ret = TRUE;
	if(w->filename == NULL)
		ret = menu_save_as(NULL, w);
	else if(w->dirty) {
		ret = mime_model_write_to_file(w->model, w->filename);
		if(ret) w->dirty = FALSE;
	}
	return ret;
}
gboolean menu_close(GtkMenuItem* item, WemedWindow* w) {
	(void) item; // unused
	if(w->dirty) {
		GtkWidget* dialog = gtk_message_dialog_new(
				GTK_WINDOW(w->rootwindow),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_YES_NO,
				"File has been modified. Would you like to save it?");
		if(gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_YES) return FALSE;
		if(menu_save(NULL, w) != TRUE) return FALSE;
	}
	mime_model_free(w->model);
	w->model = 0;
	wemed_panel_clear(WEMED_PANEL(w->panel));
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->view), NULL);
	return TRUE;
}

void menu_open(GtkMenuItem* item, WemedWindow* w) {
	(void) item; // unused
	if(menu_close(NULL, w) == FALSE) return;

	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open File",
			GTK_WINDOW(w->rootwindow),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
			NULL);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		wemed_window_open(w, filename);
		g_free(filename);
	}

	gtk_widget_destroy (dialog);
}

void menu_file_new_blank(GtkMenuItem* item, WemedWindow* w) {
	(void) item; // unused
	if(menu_close(NULL, w) == FALSE) return;
	

}

static void menu_part_new_empty(GtkMenuItem* item, WemedWindow* w) {
	mime_model_new_part(w->model, selected_object(gtk_tree_view_get_selection(GTK_TREE_VIEW(w->view))), NULL);
}
static void menu_part_new_from_file(GtkMenuItem* item, WemedWindow* w) {
}
static void open_part_with_external_app(GMimePart* part, const char* app) {
	char* tmpfile = strdup("wemed-tmpfile-XXXXXX");
	int fd = mkstemp(tmpfile);
	FILE* fp = fdopen(fd, "wb");
	mime_model_write_part(part, fp);
	char* buffer = malloc(strlen(app) + strlen(tmpfile) + 5);
	char* p = 0;
	if( (p = strstr(app, "%f")) || (p = strstr(app, "%U")) || (p = strstr(app, "%s"))) {
		p[1] = 's';
		sprintf(buffer, app, tmpfile);
	} else {
		sprintf(buffer, "%s %s", app, tmpfile);
	}
	system(buffer);
	free(buffer);
	unlink(tmpfile); // be a tidy kiwi
}
static void menu_part_edit(GtkMenuItem* item, WemedWindow* w) {
	(void) item; //unused
	//wemed_open_part(WEMED_PANEL(w->panel), w->mime_app.exec);
	open_part_with_external_app(GMIME_PART(w->current_part), w->mime_app.exec);
}
static void menu_part_edit_with(GtkMenuItem* item, WemedWindow* w) {
	(void) item; //unused
	const char* content_type_name = mime_model_content_type(w->current_part);
	printf("calling open_with on type %s\n", content_type_name);
	char* exec = open_with(NULL, content_type_name);
	if(!exec) return;
	//wemed_open_part(WEMED_PANEL(w->panel), exec);
	open_part_with_external_app(GMIME_PART(w->current_part), exec);
}

GtkWidget* build_menubar(WemedWindow* w) {
	GtkWidget* menubar = gtk_menu_bar_new();

	{ // File
	GtkWidget* file = gtk_menu_item_new_with_mnemonic("_File");
	GtkWidget* filemenu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), filemenu);
		{ // File -> New
		GtkWidget* new = gtk_menu_item_new_with_mnemonic("_New");
		GtkWidget* newmenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(new), newmenu);
			{ // File -> New -> Blank Document
			GtkWidget* blank = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);
			g_signal_connect(G_OBJECT(blank), "activate", G_CALLBACK(menu_file_new_blank), w);
			gtk_menu_item_set_label(GTK_MENU_ITEM(blank), "_Blank Document");
			gtk_menu_shell_append(GTK_MENU_SHELL(newmenu), blank);
			}
			{ // File -> New -> Email Template
			GtkWidget* email = gtk_menu_item_new_with_mnemonic("_Email Template");
			gtk_menu_shell_append(GTK_MENU_SHELL(newmenu), email);
			}
			{ // File -> New -> MHTML Template
			GtkWidget* mhtml = gtk_menu_item_new_with_mnemonic("M_HTML Template");
			gtk_menu_shell_append(GTK_MENU_SHELL(newmenu), mhtml);
			}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), new);
		}
		{ // File -> Open
		GtkWidget* open = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
		g_signal_connect(G_OBJECT(open), "activate", G_CALLBACK(menu_open), w);
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), open);
		}
		{ // File -> Save
		GtkWidget* save = gtk_image_menu_item_new_from_stock(GTK_STOCK_SAVE, NULL);
		g_signal_connect(G_OBJECT(save), "activate", G_CALLBACK(menu_save), w);
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), save);
		}
		{ // File -> Save As
		GtkWidget* saveas = gtk_image_menu_item_new_from_stock(GTK_STOCK_SAVE_AS, NULL);
		g_signal_connect(G_OBJECT(saveas), "activate", G_CALLBACK(menu_save_as), w);
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), saveas);
		}
		{ // File -> Close
		GtkWidget* close = gtk_image_menu_item_new_from_stock(GTK_STOCK_CLOSE, NULL);
		g_signal_connect(G_OBJECT(close), "activate", G_CALLBACK(menu_close), w);
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), close);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Quit
		GtkWidget* quit = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
		g_signal_connect(G_OBJECT(quit), "activate", G_CALLBACK(gtk_main_quit), NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), quit);
		}
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file);
	}
	{ // Part
	GtkWidget* part = gtk_menu_item_new_with_mnemonic("_Part");
	GtkWidget* partmenu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(part), partmenu);
		{ // Part -> New
		GtkWidget* new = gtk_menu_item_new_with_mnemonic("_New");
		GtkWidget* newmenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(new), newmenu);
			{ // Part -> New -> Multipart Node
			GtkWidget* node = gtk_menu_item_new_with_mnemonic("_Multipart Node");
			gtk_menu_shell_append(GTK_MENU_SHELL(newmenu), node);
			}
			{ // Part -> New -> Empty Part
			GtkWidget* empty = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);
			g_signal_connect(G_OBJECT(empty), "activate", G_CALLBACK(menu_part_new_empty), w);
			gtk_menu_item_set_label(GTK_MENU_ITEM(empty), "_Empty Part");
			gtk_menu_shell_append(GTK_MENU_SHELL(newmenu), empty);
			}
			{ // Part -> New -> From File
			GtkWidget* fromfile = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
			g_signal_connect(G_OBJECT(fromfile), "activate", G_CALLBACK(menu_part_new_from_file), w);
			gtk_menu_item_set_label(GTK_MENU_ITEM(fromfile), "From _File");
			gtk_menu_shell_append(GTK_MENU_SHELL(newmenu), fromfile);
			}
		gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), new);
		}
		{ // Part -> Edit
		w->menu_part_edit = gtk_image_menu_item_new_from_stock(GTK_STOCK_EDIT, NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), w->menu_part_edit);
		g_signal_connect(G_OBJECT(w->menu_part_edit), "activate", G_CALLBACK(menu_part_edit), w);
		}
		{ // Part -> Edit with
		GtkWidget* editwith = gtk_image_menu_item_new_from_stock(GTK_STOCK_EDIT, NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), editwith);
		gtk_menu_item_set_label(GTK_MENU_ITEM(editwith), "Edit _With...");
		g_signal_connect(G_OBJECT(editwith), "activate", G_CALLBACK(menu_part_edit_with), w);
		}
		{ // Part -> Export
		}
		{ // Part -> Delete
		}
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), part);
	}

	return menubar;

}


gboolean wemed_window_open(WemedWindow* w, const char* filename) {
	// todo close old model
	MimeModel* m = mime_model_create_from_file(filename);
	if(!m) return FALSE;
	wemed_panel_set_cid_table(WEMED_PANEL(w->panel), mime_model_get_cid_hash(m));
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->view), mime_model_get_gtk_model(m));
	
	w->model = m;
	w->filename = strdup(filename);
	return TRUE;
}

void headers_modified(GObject* emitter, gchar* headers, gpointer userdata) {
	WemedWindow* w = userdata;
	printf("w=%p\n",w);
	GMimeObject* new_part = mime_model_update_header(w->model, w->current_part, headers);
	if(new_part) {
		w->dirty = TRUE;
		w->current_part = new_part;
		load_document_part(WEMED_PANEL(w->panel), new_part);
	}
}

WemedWindow* wemed_window_create() {
	WemedWindow* w = calloc(1, sizeof(WemedWindow));

	w->rootwindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(w->rootwindow, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* menubar = build_menubar(w);

	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 3);


	//GtkWidget* hpanel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget* hpanel = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);//gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);


	w->view = gtk_tree_view_new();
	GtkTreeSelection *select = gtk_tree_view_get_selection (GTK_TREE_VIEW(w->view));
	gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);


	GtkTreeViewColumn* col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Segment");
	gtk_tree_view_append_column(GTK_TREE_VIEW(w->view), col);

	GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", 0);

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Icon");
	gtk_tree_view_append_column(GTK_TREE_VIEW(w->view), col);

	renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "pixbuf", 2);



	//gtk_box_pack_start(GTK_BOX(hpanel), w->view, FALSE, FALSE, 0);
	GtkWidget* treeviewwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(treeviewwin), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(treeviewwin), w->view);
	gtk_paned_add1(GTK_PANED(hpanel), treeviewwin);
	w->panel = wemed_panel_new(); //wemed_panel_create();
	gtk_paned_add2(GTK_PANED(hpanel), w->panel);
	//wemed_panel_set_header_change_callback(WEMED_PANEL(w->panel), headers_changed, w);
	printf("w=%p\n",w);
	g_signal_connect(G_OBJECT(w->panel), "headers-changed", G_CALLBACK(headers_modified), w);

	g_signal_connect(G_OBJECT(select), "changed", G_CALLBACK(tree_selection_changed_cb), w);
	
	gtk_box_pack_start(GTK_BOX(vbox), hpanel, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(w->rootwindow), vbox);


	gtk_widget_show_all(w->rootwindow);

	return w;
}