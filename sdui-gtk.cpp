// SD -- square dance caller's helper.
//
//    This file copyright (C) 2005  C. Scott Ananian.
//    Derived heavily from sdui-win.cpp.
//
//    This file is part of "Sd".
//
//    Sd is free software; you can redistribute it and/or modify it
//    under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    Sd is distributed in the hope that it will be useful, but WITHOUT
//    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//    or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//    License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with Sd; if not, write to the Free Software Foundation, Inc.,
//    59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//
//    This is for version 36.


//    sdui-gtk.cpp - SD -- GTK2 User Interface (*nix and other platforms)

#define UI_VERSION_STRING "0.1"

#define PACKAGE "sd"
#define VERSION "36.57"
#define PACKAGE_DATA_DIR "/usr/share/sd"

// This file defines all the functions in class iofull.

#include <assert.h>
#include <gnome.h>
#include <glade/glade.h>

#include "sd.h"
#include "paths.h"
#include "sdui-ico.h"
extern "C" {
#include "sdui-gtk.h" /* GLADE callback function prototypes */
}

// GLADE interface definitions
static GladeXML *sd_xml;
static GtkWidget *window_startup, *window_main, *window_font, *window_text;
static GdkPixbuf *ico_pixbuf;
#define SDG(widget_name) (glade_xml_get_widget(sd_xml, (widget_name)))
#define gtk_update() while (gtk_events_pending ()) gtk_main_iteration ()

// default window size.
static int window_size_args[4] = {-1, -1, -1, -1};
// Under Sdtty, if you type something ambiguous and press ENTER, it
// isn't accepted.  We want to be keystroke-compatible with Sdtty.
// BUT: If the user has used any up-down arrows to move around in the
// menu, and then presses ENTER, we accept the highlighted item, even
// if it would have been ambiguous to the matcher.  This is obviously
// what the user wants, and, by using the arrow keys, the user has
// gone outside of the Sdtty behavior.  This variable keeps track of
// whether the menu changed.
static int menu_moved = 0;
// do we want the window to start maximized?
static int do_maximize = 0;
// do we want to override the window size?
static char *window_size_str = NULL;

#define DISPLAY_LINE_LENGTH 90

struct DisplayType {
   char Line [DISPLAY_LINE_LENGTH];
   int in_picture;
   int Height;
   int DeltaToNext;
   DisplayType *Next;
   DisplayType *Prev;
};


#define ui_undefined -999

static char szResolveWndTitle [MAX_TEXT_LINE_LENGTH];

#ifdef TAB_FOCUS
static bool InPopup = false;
#endif
static bool WaitingForCommand = false;
static int nLastOne;
static uims_reply MenuKind;
static DisplayType *DisplayRoot = NULL;
static DisplayType *CurDisplay = NULL;

// This is the last title sent by the main program.  We add stuff to it.
static gchar main_title[MAX_TEXT_LINE_LENGTH];

// misc. prototypes.
static void about_open_url(GtkAboutDialog *about,
			   const gchar *link, gpointer data);


static void uims_bell()
{
    if (!ui_options.no_sound) gdk_beep();
}


static void UpdateStatusBar(const gchar *text)
{
   gchar buf[strlen(text)+15+16+12+14+11+13+1];
   assert(text);

   snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s",
	    ((allowing_modifications == 2) ? "all mods | " :
	     (allowing_modifications ? "simple mods | " : "")),
	    (allowing_all_concepts ? "all concepts | " : ""),
	    (using_active_phantoms ? "act phan | " : ""),
	    ((ui_options.singing_call_mode == 2) ? "rev singer | " :
	     (ui_options.singing_call_mode ? "singer | " : "")),
	    (ui_options.nowarn_mode ? "no warn | " : ""),
	    (allowing_minigrand ? "minigrand | " : ""),
	    text);
   gnome_appbar_set_status(GNOME_APPBAR(SDG("main_appbar")), buf);
   gtk_update();
}
/// 259
/// 838
// Size of the square pixel array in the bitmap for one person.
// The bitmap is exactly 8 of these wide and 4 of them high.
#define BMP_PERSON_SIZE 36
// This should be even.
// was 10
#define BMP_PERSON_SPACE 0
/// 845
/// 1132
static popup_return do_general_text_popup(Cstring prompt1, Cstring prompt2,
                                          Cstring seed, char dest[])
{
   int result;
   gtk_label_set_text(GTK_LABEL(SDG("text_line1")), prompt1);
   gtk_label_set_text(GTK_LABEL(SDG("text_line2")), prompt2);
   gtk_entry_set_text(GTK_ENTRY(SDG("text_entry")), seed);
   gtk_widget_show(window_text);
   result = gtk_dialog_run(GTK_DIALOG(window_text));
   gtk_widget_hide(window_text);
   dest[0] = 0;
   if (result == GTK_RESPONSE_OK) {
      const gchar *txt = gtk_entry_get_text(GTK_ENTRY(SDG("text_entry")));
      if (txt && txt[0]) {
	 strcpy(dest, txt); // XXX UNSAFE!  WATCH BUFFER LENGTH!
	 return POPUP_ACCEPT_WITH_STRING;
      }
      return POPUP_ACCEPT;
   }
   return POPUP_DECLINE;
}

///////////////////////////////////////////////////////////////////////////
// command-line options.
static struct {
    int argc;
    char **argv;
    int allocd;
} fake_args = { 0, NULL, 0 };
static void stash_away_sd_options(poptContext con,const struct poptOption *opt,
				  const char *args, void * data) {
    char *buf;
    int n;
    if (fake_args.argc==fake_args.allocd) {
	if (fake_args.allocd==0)
	    fake_args.allocd=10; // first allocation
	else
	    fake_args.allocd*=2; // re-allocation
	fake_args.argv = (char**) get_more_mem
	    (fake_args.argv, fake_args.allocd * sizeof(*(fake_args.argv)));
    }
    // okay, stash away this option
    assert(opt->longName);
    n = strlen(opt->longName)+2;
    buf = (char*) get_mem(n);
    snprintf(buf, n, "-%s", opt->longName);
    fake_args.argv[fake_args.argc++]=buf;
    // is there an argument? stash it away too
    if (args)
	fake_args.argv[fake_args.argc++]=strdup(args);
    // okay, done.
}
static const struct poptOption tty_options[] = {
    {"no_line_delete", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "do not use the \"line delete\" function for screen management (ignored)",
     NULL},
    {"no_cursor", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "do not use screen management functions at all (ignored)", NULL},
    {"no_console", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "??? (ignored)", NULL}, // XXX FIXME
    {"alternative_glyphs_1", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "??? (ignored)", NULL}, // XXX FIXME
    {"lines", 0, POPT_ARG_INT | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "assume this many lines on the screen", "<n>"},
    {"journal", 0, POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "echo input commands to journal file", "<filename>"},
    POPT_TABLEEND /* end the list */
}, gui_options[] = {
    {"maximize", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, &do_maximize, 0,
     "start with sd main window maximized", NULL},
    {"window_size", 0, POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
     &window_size_str, 0,
     "set the initial sd window size", "<width>x<height>[x<left>x<top>]"},
    POPT_TABLEEND /* end the list */
}, sd_options[] = {
    {NULL, 0, POPT_ARG_CALLBACK, (void*)&stash_away_sd_options, 0, NULL, NULL},
    {"write_list", 0,
     POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_OPTIONAL, NULL, 0,
     "write out list for this level", "<filename>"},
    {"write_full_list", 0,
     POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_OPTIONAL, NULL, 0,
     "write this list and all lower", "<filename>"},
    {"abridge", 0,
     POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_OPTIONAL, NULL, 0,
     "do not use calls in this file", "<filename>"},
    {"sequence", 0,
     POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_OPTIONAL, NULL, 0,
     "base name for sequence output file (def \"sequence\")", "<filename>"},
    {"db", 0,
     POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_OPTIONAL, NULL, 0,
     "calls database file (def \"sd_calls.dat\")", "<filename>"},
    {"sequence_num", 0,
     POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_OPTIONAL,
     &ui_options.sequence_num_override, 0,
     "use this initial sequence number", "<n>"},
    {"session", 0,
     POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_OPTIONAL,
     &ui_options.force_session, 0,
     "use the indicated session number", "<n>"},
    {"resolve_test", 0,
     POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_OPTIONAL,
     &ui_options.resolve_test_minutes, 0,
     "????", "<minutes>"}, // XXX ???
    {"print_length", 0,
     POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_OPTIONAL,
     &ui_options.max_print_length, 0,
     "????", "<n>"}, // XXX ???
    {"delete_abridge", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARG_VAL,
     &glob_abridge_mode, abridge_mode_deleting_abridge,
     "delete abridgement from existing session", NULL },
    {"no_intensify", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "show text in the normal shade instead of extra-bright", NULL },
    {"reverse_video", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "(Sd only) display transcript in white-on-black", NULL },
    {"normal_video", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "(Sdtty only) display transcript in black-on-white", NULL },
    {"pastel_color", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "use pastel colors when not coloring by couple or corner", NULL },
    {"bold_color", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "use bold colors when not coloring by couple or corner", NULL },
    {"no_color", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARG_VAL,
     &ui_options.color_scheme, no_color,
     "do not display people in color", NULL },
    {"color_by_couple", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARG_VAL,
     &ui_options.color_scheme, color_by_couple,
     "display color according to couple number, rgby", NULL },
    {"color_by_couple_rgyb", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARG_VAL,
     &ui_options.color_scheme, color_by_couple_rgyb,
     "similar to color_by_couple, but with rgyb", NULL },
    {"color_by_couple_ygrb", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARG_VAL,
     &ui_options.color_scheme, color_by_couple_ygrb,
     "similar to color_by_couple, but with ygrb", NULL },
    {"color_by_corner", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARG_VAL,
     &ui_options.color_scheme, color_by_corner,
     "similar to color_by_couple, but make corners match", NULL },
    {"no_sound", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "do not make any noise when an error occurs", NULL },
    {"tab_changes_focus", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "use standard windows action on tab", NULL },
    {"keep_all_pictures", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "keep the picture after every call", NULL },
    {"single_click", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "(Sd only) act on single mouse clicks on the menu", NULL },
    {"no_checkers", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "do not use large \"checkers\" for setup display", NULL },
    {"no_graphics", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "do not use special characters for setup display", NULL },
    {"diagnostic", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "turn on sd diagnostic mode", NULL },
    {"singlespace", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "single space the output file", NULL },
    {"no_warnings", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "do not display or print any warning messages", NULL },
    {"concept_levels", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "allow concepts from any level", NULL },
    {"minigrand_getouts", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "allow \"mini-grand\" (RLG but promenade on 3rd hand) getouts", NULL },
    {"active_phantoms", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "use active phantoms for \"assume\" operations", NULL },
    {"discard_after_error", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "ciscard pending concepts after error (default)", NULL },
    {"retain_after_error", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "retain pending concepts after error", NULL },
    {"new_style_filename", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "use long file name, as in \"sequence_MS.txt\"", NULL },
    {"old_style_filename", 0, POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, NULL, 0,
     "use short file name, as in \"sequence.MS\"", NULL },
    POPT_TABLEEND /* end the list */
};
static const struct poptOption all_options[] = {
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, (void*)&sd_options, 0,
      "Sd options", NULL },
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, (void*)&gui_options, 0,
      "Sd GUI options", NULL },
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, (void*)&tty_options, 0,
      "Sdtty options (ignored)", NULL },
    POPT_TABLEEND /* end the list */
};

int main(int argc, char **argv) {
   GtkCellRenderer *renderer;
   GtkTreeView *startup_list;
   GtkTreeViewColumn *column;
   GdkPixbuf *ico_font;
   // Set the UI options for Sd.

   ui_options.reverse_video = false;
   ui_options.pastel_color = false;

   // Initialize all the callbacks that sdlib will need.
   iofull ggg;
   gg = &ggg;

   // somewhat unconventionally, initialize GNOME and process command-line
   // options *first*; we'll construct a fake argc/argv of sd-specific
   // command-line options that we'll pass to sdmain.
   gnome_program_init(PACKAGE, VERSION, LIBGNOMEUI_MODULE,
		      argc, argv,
		      GNOME_PARAM_HUMAN_READABLE_NAME, "Sd",
		      GNOME_PARAM_APP_DATADIR, PACKAGE_DATA_DIR,
		      GNOME_PARAM_POPT_TABLE, all_options,
		      LIBGNOMEUI_PARAM_CRASH_DIALOG, FALSE,
		      GNOME_PARAM_NONE);
   // initialize the interface.

   sd_xml = glade_xml_new("sd.glade", NULL, NULL);
   window_startup = SDG("dialog_startup");
   window_main = SDG("app_main");
   window_text = SDG("dialog_text");
   window_font = SDG("dialog_font");

   /* hide the progress bar (initially) */
   gtk_widget_hide(GTK_WIDGET(gnome_appbar_get_progress
			      (GNOME_APPBAR(SDG("main_appbar")))));

   /* set up the various treeview widgets */
   startup_list = GTK_TREE_VIEW(SDG("startup_list"));
   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes("Level/Session",renderer,
						     "text", 1, NULL);
   gtk_tree_view_append_column(startup_list, column);
   column = gtk_tree_view_column_new_with_attributes("Commands", renderer,
						     "text", 0, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(SDG("main_cmds")), column);

   /* Load the (inlined) sd icon. */
   ico_pixbuf = gdk_pixbuf_new_from_inline (-1, sdico_inline, FALSE, NULL);
   if (ico_pixbuf) {
      gtk_window_set_icon(GTK_WINDOW(window_main), ico_pixbuf);
      gtk_window_set_icon(GTK_WINDOW(window_startup), ico_pixbuf);
      gtk_window_set_icon(GTK_WINDOW(window_text), ico_pixbuf);
      // note that there's still an outstanding ref to ico_pixbuf, but
      // that's okay, because we've stashed it away in a static variable
      // for use later.
   }
   ico_font = gtk_widget_render_icon(window_font, GTK_STOCK_SELECT_FONT,
				     (GtkIconSize)-1, "sd");
   gtk_window_set_icon(GTK_WINDOW(window_font), ico_font);
   g_object_unref(ico_font);
   
   /* connect the signals in the interface */
   glade_xml_signal_autoconnect(sd_xml);
   gtk_about_dialog_set_url_hook(about_open_url, NULL, NULL);
   g_signal_connect
      (G_OBJECT(gtk_tree_view_get_selection(startup_list)),
       "changed", G_CALLBACK(on_startup_list_selection_changed), NULL);

   // Run the Sd program.  We'll have parsed the Sd-appropriate command-line
   // arguments into 'fake_args'.

   return sdmain(fake_args.argc, fake_args.argv);
}
/// 1173
///// 1362
bool iofull::help_manual()
{
   GError *error = NULL;
   gboolean result;
   result = gnome_help_display_with_doc_id
      (NULL, NULL, "sd_doc.html", NULL, &error);
   if (error && !result) {
      GtkWidget * dialog = gtk_message_dialog_new
	 (GTK_WINDOW(window_main), GTK_DIALOG_DESTROY_WITH_PARENT,
	  GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
	  "Can't open manual: %s", error->message);
      g_signal_connect_swapped(dialog, "response",
			       G_CALLBACK(gtk_widget_destroy), dialog);
      gtk_widget_show_all(dialog);
   }
   return result;
}
void
on_help_manual_activate(GtkMenuItem *menuitem, gpointer user_data) {
   gg->help_manual();
}


bool iofull::help_faq()
{
   GError *error = NULL;
   gboolean result;
   result = gnome_help_display_with_doc_id
      (NULL, NULL, "faq.html", NULL, &error);
   if (error && !result) {
      GtkWidget * dialog = gtk_message_dialog_new
	 (GTK_WINDOW(window_main), GTK_DIALOG_DESTROY_WITH_PARENT,
	  GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
	  "Can't open FAQ: %s", error->message);
      g_signal_connect_swapped(dialog, "response",
			       G_CALLBACK(gtk_widget_destroy), dialog);
      gtk_widget_show_all(dialog);
   }
   return result;
}
void
on_help_faq_activate(GtkMenuItem *menuitem, gpointer user_data) {
   gg->help_faq();
}

void
on_help_about_activate(GtkMenuItem *menuitem, gpointer user_data) {
   static const gchar *authors[] = {
      "William B. Ackerman", "Stephen Gildea", "Alan Snyder",
      "Robert E. Cays", "Charles Petzold",
      "Chris \"Fen\" Tamanaha",
      "C. Scott Ananian", NULL };
   static const gchar *copyright =
      "Copyright (c) 1990-2005 William B. Ackerman and Stephen Gildea\n"
      "Copyright (c) 1992-1993 Alan Snyder\n"
      "Copyright (c) 1995 Robert E. Cays\n"
      "Copyright (c) 1996 Charles Petzold\n"
      "Copyright (c) 2001-2002 Chris \"Fen\" Tamanaha\n"
      "Copyright (c) 2005 C. Scott Ananian\n"
      "\n"
      "SD comes with ABSOLUTELY NO WARRANTY.\n"
      "This is free software, and you are welcome to redistribute it\n"
      "under certain conditions.  For details see the license.";
   gtk_show_about_dialog(GTK_WINDOW(window_main),
			 "name", PACKAGE,
			 "version", VERSION,
			 "logo", ico_pixbuf,
			 "copyright", copyright,
			 "comments", "Sd: Square Dance Caller's Helper.",
			 "license", "GPL",
			 "website", "http://www.lynette.org/sd",
			 "authors", authors,
			 NULL);
}
static void about_open_url(GtkAboutDialog *about,
			   const gchar *link, gpointer data) {
   GError *error = NULL;
   gboolean result;
   result = gnome_url_show(link, &error);
   if (error && !result) {
      GtkWidget * dialog = gtk_message_dialog_new
	 (GTK_WINDOW(window_main), GTK_DIALOG_DESTROY_WITH_PARENT,
	  GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
	  "Can't open %s: %s", link, error->message);
      g_signal_connect_swapped(dialog, "response",
			       G_CALLBACK(gtk_widget_destroy), dialog);
      gtk_widget_show_all(dialog);
   }
}

/// 1375


// iofull stubs.
static void Update_text_display() { assert(0); }
static void erase_questionable_stuff() { assert(0); }
void iofull::show_match() { assert(0); }
void iofull::final_initialize() { assert(0); }
static void do_my_final_shutdown() {
   // send 'close' to window.
   // XXX NOT CORRECT.
   g_signal_emit_by_name(GTK_WIDGET(window_main), "GtkWidget::delete-event",
			 NULL, NULL); // XXX is this correct?
}

////////// 1815
static void setup_level_menu(GtkListStore *startup_list)
{
   GtkTreeIter iter;
   int lev, i=0;

   for (lev=(int)l_mainstream ; ; lev++) {
      Cstring this_string = getout_strings[lev];
      if (!this_string[0]) break;
      gtk_list_store_insert_with_values(startup_list, &iter, G_MAXINT,
					0, i++, 1, this_string, -1);
   }
}


static void SetTitle()
{
   gtk_window_set_title(GTK_WINDOW(window_main), main_title);
}


void iofull::set_pick_string(const char *string)
{
   if (string && *string) {
      gtk_window_set_title(GTK_WINDOW(window_main), string);
   }
   else {
      SetTitle();   // End of pick, reset to our main title.
   }
}

void iofull::set_window_title(char s[])
{
   snprintf(main_title, sizeof(main_title), "Sd %s", s);
   SetTitle();
}



static enum dialog_menu_type {
   dialog_session,
   dialog_level,
   dialog_none}
dialog_menu_type;

static bool request_deletion = false;
static Cstring session_error_msg = NULL;


// Process Startup dialog box messages.


gboolean
on_startup_list_button_press_event     (GtkWidget       *widget,
                                        GdkEventButton  *event,
                                        gpointer         user_data)
{
   if (event->type != GDK_2BUTTON_PRESS)
      return FALSE; /* not a double-click, ignore. */
   /* double-click on session or level list item; treat as 'ok' click */
   gtk_dialog_response(GTK_DIALOG(window_startup), GTK_RESPONSE_OK);
   return TRUE; /* okay, I handled this. */
}

void
on_abridge_changed                     (GtkButton        *button,
                                        gpointer         user_data)
{
   // enable abridgement filename box if appropriate
   gboolean abridge_disable =
      gtk_toggle_button_get_active
      (GTK_TOGGLE_BUTTON(SDG("startup_abridge_normal"))) ||
      gtk_toggle_button_get_active
      (GTK_TOGGLE_BUTTON(SDG("startup_abridge_cancel")));

   GnomeFileEntry *abridge_file = GNOME_FILE_ENTRY(SDG("fileentry_abridge"));
   gtk_widget_set_sensitive(GTK_WIDGET(abridge_file), !abridge_disable);
   // pre-fill filename if it's empty.
   if (!abridge_disable) {
      char *path = gnome_file_entry_get_full_path(abridge_file, FALSE);
      if (path) g_free(path);
      else
	 gnome_file_entry_set_filename(abridge_file, "abridge.txt");
   }
}

void
on_calldb_changed                      (GtkButton       *button,
                                        gpointer         user_data)
{
   // enable database filename box if appropriate
   gboolean db_disable = gtk_toggle_button_get_active
      (GTK_TOGGLE_BUTTON(SDG("startup_db_default")));
   GnomeFileEntry *db_file = GNOME_FILE_ENTRY(SDG("fileentry_db"));
   gtk_widget_set_sensitive(GTK_WIDGET(db_file), !db_disable);
   // pre-fill filename if it's empty.
   if (!db_disable) {
      char *path = gnome_file_entry_get_full_path(db_file, FALSE);
      if (path) g_free(path);
      else
	 gnome_file_entry_set_filename(db_file, "database.txt");
   }
}

void
on_startup_list_selection_changed(GtkTreeSelection *selection, gpointer data)
{
   // enable 'ok' button iff there is a selection.
   gboolean result = gtk_tree_selection_get_selected(selection, NULL, NULL);
   gtk_dialog_set_response_sensitive(GTK_DIALOG(window_startup),
				     GTK_RESPONSE_OK, result);
}

static gboolean startup_accept()
{
   gchar *output_filename;
   GtkTreeSelection *select;
   GtkTreeModel *model;
   GtkTreeIter iter;
   gboolean result;
   int i;
      /* User hit the "accept" button.  Read out all the information.
         But it's more complicated than that.  We sometimes do a two-stage
         presentation of this dialog, getting the session number and then
         the level.  So we may have to go back to the second stage.
         The variable "dialog_menu_type" tells what we were getting. */

   select = gtk_tree_view_get_selection(GTK_TREE_VIEW(SDG("startup_list")));
   result = gtk_tree_selection_get_selected(select, &model, &iter);
   assert(result);
   gtk_tree_model_get(model, &iter, 0, &i, -1); /* get selected index */

      if (dialog_menu_type == dialog_session) {
         /* The user has just responded to the session selection.
            Figure out what to do.  We may need to go back and get the
            level. */

         session_index = i;

         // If the user wants that session deleted, do that, and get out
         // immediately.  Setting the number to zero will cause it to
         // be deleted when the session file is updated during program
         // termination.

	 if (gtk_toggle_button_get_active
	     (GTK_TOGGLE_BUTTON(SDG("startup_session_delete")))) {
            session_index = -session_index;
            request_deletion = true;
            goto getoutahere;
         }

         // If the user selected the button for canceling the abridgement
         // on the current session, do so.

	 if (gtk_toggle_button_get_active
	     (GTK_TOGGLE_BUTTON(SDG("startup_abridge_cancel"))))
            glob_abridge_mode = abridge_mode_deleting_abridge;

         // Analyze the indicated session number.

         int session_info = process_session_info(&session_error_msg);

         if (session_info & 1) {
            // We are not using a session, either because the user selected
            // "no session", or because of some error in processing the
            // selected session.
            session_index = 0;
            sequence_number = -1;
         }

         if (session_info & 2)
            gg->serious_error_print(session_error_msg);

         // If the level never got specified, either from a command line
         // argument or from the session file, put up the level selection
         // screen and go back for another round.

         if (calling_level == l_nonexistent_concept) {
	    return FALSE;
         }
      }
      else if (dialog_menu_type == dialog_level) {
         // Either there was no session file, or there was a session
         // file but the user selected no session or a new session.
         // In the latter case, we went back and asked for the level.
         // So now we have both, and we can proceed.
         calling_level = (dance_level) i;
         strncat(outfile_string, filename_strings[calling_level], MAX_FILENAME_LENGTH);
      }

      // If a session was selected, and that session specified
      // an abridgement file, that file overrides what the buttons
      // and the abridgement file name edit box specify.

      if (glob_abridge_mode != abridge_mode_abridging) {
	 if (gtk_toggle_button_get_active
	     (GTK_TOGGLE_BUTTON(SDG("startup_abridge_abridge"))))
            glob_abridge_mode = abridge_mode_abridging;
	 else if (gtk_toggle_button_get_active
		  (GTK_TOGGLE_BUTTON(SDG("startup_abridge_write"))))
            glob_abridge_mode = abridge_mode_writing;
	 else if (gtk_toggle_button_get_active
		  (GTK_TOGGLE_BUTTON(SDG("startup_abridge_write_full"))))
            glob_abridge_mode = abridge_mode_writing_full;
	 else if (gtk_toggle_button_get_active
		  (GTK_TOGGLE_BUTTON(SDG("startup_abridge_normal"))))
            glob_abridge_mode = abridge_mode_none;

         // If the user specified a call list file, get the name.

         if (glob_abridge_mode >= abridge_mode_abridging) {
	    char *call_list_filename;

            // This may have come from the command-line switches,
            // in which case we already have the file name.

	    call_list_filename = gnome_file_entry_get_full_path
	       (GNOME_FILE_ENTRY(SDG("fileentry_abridge")), FALSE);
 
	    if (call_list_filename) {
	       if (call_list_filename[0])
		  snprintf(abridge_filename, sizeof(abridge_filename), "%s",
			   call_list_filename);
	       g_free(call_list_filename);
	    }
         }
      }
      else if (gtk_toggle_button_get_active
	       (GTK_TOGGLE_BUTTON(SDG("startup_abridge_abridge")))) {
         // But if the session specified an abridgement file, and the user
         // also clicked the button for abridgement and gave a file name,
         // use that file name, overriding what was in the session line.

	 gchar *call_list_filename;

	 call_list_filename = gnome_file_entry_get_full_path
	    (GNOME_FILE_ENTRY(SDG("fileentry_abridge")), FALSE);
	 
	 if (call_list_filename) {
	    if (call_list_filename[0])
	       snprintf(abridge_filename, sizeof(abridge_filename), "%s",
			call_list_filename);
	    g_free(call_list_filename);
	 }
      }

      // If user specified the output file during startup dialog, install that.
      // It overrides anything from the command line.

      output_filename = gnome_file_entry_get_full_path
	 (GNOME_FILE_ENTRY(SDG("fileentry_output")), FALSE);

      if (output_filename) {
	 if (output_filename[0])
	    new_outfile_string = output_filename;//never freed;stored in static
	 else
	    g_free(output_filename);
      }

      // Handle user-specified database file.

      if (gtk_toggle_button_get_active
	       (GTK_TOGGLE_BUTTON(SDG("startup_db_user")))) {
	 gchar *db_filename;
	 db_filename = gnome_file_entry_get_full_path
	    (GNOME_FILE_ENTRY(SDG("fileentry_db")), TRUE);
	 if (db_filename) {
	    if (db_filename[0])
	       database_filename = db_filename;//never freed;stored in static
	    else
	       g_free(db_filename);
	 }
      }

      ui_options.sequence_num_override =
	 gtk_spin_button_get_value_as_int
	 (GTK_SPIN_BUTTON(SDG("seq_num_override")));

   getoutahere:

      return TRUE;
}

static void startup_init()
{
   GtkSpinButton *seq_num_override;
   static GtkListStore *startup_list = NULL;
   GtkTreeSelection *ts;
   GtkTooltipsData *td;
   // Set up the sequence number override.  Its text is the null string
   // unless a command line value was given.

   seq_num_override = GTK_SPIN_BUTTON(SDG("seq_num_override"));
   gtk_entry_set_text(GTK_ENTRY(seq_num_override), "");

   if (ui_options.sequence_num_override > 0)
      gtk_spin_button_set_value(seq_num_override,
				ui_options.sequence_num_override);

   // Select the default radio buttons.

   switch (glob_abridge_mode) {
   case abridge_mode_writing:
      gtk_toggle_button_set_active
	 (GTK_TOGGLE_BUTTON(SDG("startup_abridge_write")), TRUE);
      if (abridge_filename)
	 gnome_file_entry_set_filename
	    (GNOME_FILE_ENTRY(SDG("fileentry_abridge")), abridge_filename);
      break;
   case abridge_mode_writing_full:
      gtk_toggle_button_set_active
	 (GTK_TOGGLE_BUTTON(SDG("startup_abridge_write_full")), TRUE);
      if (abridge_filename)
	 gnome_file_entry_set_filename
	    (GNOME_FILE_ENTRY(SDG("fileentry_abridge")), abridge_filename);
      break;
   case abridge_mode_abridging:
      gtk_toggle_button_set_active
	 (GTK_TOGGLE_BUTTON(SDG("startup_abridge_abridge")), TRUE);
      if (abridge_filename)
	 gnome_file_entry_set_filename
	    (GNOME_FILE_ENTRY(SDG("fileentry_abridge")), abridge_filename);
      break;
   default:
      gtk_toggle_button_set_active
	 (GTK_TOGGLE_BUTTON(SDG("startup_abridge_normal")), TRUE);
      if (abridge_filename)
	 gnome_file_entry_set_filename
	    (GNOME_FILE_ENTRY(SDG("fileentry_abridge")), "");
      break;
   }

   gtk_toggle_button_set_active
      (GTK_TOGGLE_BUTTON(SDG("startup_db_default")), TRUE);

   // Seed the various file names with the null string.

   gnome_file_entry_set_filename
	    (GNOME_FILE_ENTRY(SDG("fileentry_output")), "");
   gnome_file_entry_set_filename
	    (GNOME_FILE_ENTRY(SDG("fileentry_db")), "");

   // Put up the session list or the level list,
   // depending on whether a session file is in use.

   if (!startup_list) {
      startup_list = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_STRING);
      gtk_tree_view_set_model(GTK_TREE_VIEW(SDG("startup_list")),
			      GTK_TREE_MODEL(startup_list));
   } else {
      gtk_list_store_clear(startup_list);
   }
   ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(SDG("startup_list")));
   gtk_tree_selection_set_mode(ts, GTK_SELECTION_BROWSE);
   td = gtk_tooltips_data_get(GTK_WIDGET(SDG("startup_list")));

   // If user specified session number on the command line, we must
   // just be looking for a level.  So skip the session part.

   if (ui_options.force_session == -1000000 && !get_first_session_line()) {
      char line[MAX_FILENAME_LENGTH];
      GtkTreeIter iter;
      int i=0;

      gtk_label_set_markup(GTK_LABEL(SDG("startup_caption")),
			   "<b>Choose a session:</b>");
      gtk_tooltips_set_tip
	 (td->tooltips, td->widget,
	  "Double-click a session to choose it and start Sd.\n"
	  "Double-click \"(no session)\" if you don't want to run under "
	  "any session at this time.\n"
	  "Double-click \"(create a new session)\" if you want to add a "
	  "new session to the list.\n"
	  "You will be asked about the particulars for that new session.",
	  NULL);

      while (get_next_session_line(line))
	 gtk_list_store_insert_with_values(startup_list, &iter, G_MAXINT,
					   0, i++, 1, line, -1);
      dialog_menu_type = dialog_session;
   }
   else if (calling_level == l_nonexistent_concept) {
      gtk_label_set_markup(GTK_LABEL(SDG("startup_caption")),
			   "<b>Choose a level:</b>");
      gtk_tooltips_set_tip
	 (td->tooltips, td->widget,
	  "Double-click a level to choose it and start Sd.", NULL);

      setup_level_menu(startup_list);
      dialog_menu_type = dialog_level;
   }
   else {
      gtk_label_set_markup(GTK_LABEL(SDG("startup_caption")), "");
      dialog_menu_type = dialog_none;
   }
   gtk_tree_selection_unselect_all(ts);
   gtk_dialog_set_response_sensitive(GTK_DIALOG(window_startup),
				     GTK_RESPONSE_OK, FALSE);
}

bool iofull::init_step(init_callback_state s, int n)
{
   GtkProgressBar *progress_bar;
   static int progress_num = 0, progress_steps=1;
   switch (s) {

   case get_session_info:

      // If the user specified a specific session, do that session.
      // If we succeed at it, we won't put up the dialog box at all.

      if (ui_options.force_session != -1000000 &&
          ui_options.force_session != 0 &&
          !get_first_session_line()) {
         while (get_next_session_line((char *) 0));   // Need to scan the file anyway.
         session_index = ui_options.force_session;
         if (session_index < 0) {
            request_deletion = true;
         }
         else {
            int session_info = process_session_info(&session_error_msg);

            if (session_info & 1) {
               // We are not using a session, either because the user selected
               // "no session", or because of some error in processing the
               // selected session.
               session_index = 0;
               sequence_number = -1;
            }

            if (session_info & 2)
               gg->serious_error_print(session_error_msg);
         }
      }
      else {
         session_index = 0;
         sequence_number = -1;
      }

      // Now put up the dialog box if we need a session or a level.
      // If the user requested an explicit session,
      // we don't do the dialog, since the user is presumably running
      // from command-line arguments, and doesn't need any of the
      // info that the dialog would give.

      if (ui_options.force_session == -1000000) {
	 gint result;
	 do {
	    startup_init();
	    gtk_widget_show(window_startup);
	    result = gtk_dialog_run(GTK_DIALOG(window_startup));
	    gtk_widget_hide(window_startup);
	 } while (result==GTK_RESPONSE_OK && !startup_accept());
	 if (result!=GTK_RESPONSE_OK) {
	    // User hit the "cancel" button.
	    session_index = 0;  // Prevent attempts to update session file.
	    general_final_exit(0);
            gg->fatal_error_exit(1, "Startup cancelled", "");
	 }
	 // user hit 'accept'; all done.
      }

      if (request_deletion) return true;
      break;

   case final_level_query:
      calling_level = l_mainstream;   // User really doesn't want to tell us the level.
      snprintf(outfile_string, sizeof(outfile_string), "%s",
	       filename_strings[calling_level]);
      break;

   case init_database1:
      // The level has been chosen.  We are about to open the database.
      // Put up the main window.

      gtk_window_set_default_size(GTK_WINDOW(window_main),
				  window_size_args[2], window_size_args[3]);
      gtk_widget_show(window_main);
      if (window_size_args[0]>0 && window_size_args[1]>0)
	 gtk_window_move(GTK_WINDOW(window_main),
			 window_size_args[0], window_size_args[1]);
      if (do_maximize)
	 gtk_window_maximize ( GTK_WINDOW(window_main) );

      gtk_update();

      UpdateStatusBar("Reading database");
      break;

   case init_database2:
      progress_bar =
	 gnome_appbar_get_progress(GNOME_APPBAR(SDG("main_appbar")));
      gtk_progress_bar_set_fraction(progress_bar, 0.0);
      gtk_widget_show(GTK_WIDGET(progress_bar));
      UpdateStatusBar("Creating Menus");
      break;

   case calibrate_tick:
      progress_num = 0;
      progress_steps = n;
      break;

   case do_tick:
      progress_num += n;
      progress_bar =
	 gnome_appbar_get_progress(GNOME_APPBAR(SDG("main_appbar")));
      gtk_progress_bar_set_fraction(progress_bar,
				    progress_num/(gdouble)progress_steps);
      gtk_update();
      break;

   case tick_end:
      break;

   case do_accelerator:
      gtk_widget_hide(GTK_WIDGET(gnome_appbar_get_progress
			      (GNOME_APPBAR(SDG("main_appbar")))));
      UpdateStatusBar("Processing Accelerator Keys");
      break;

   default:
      assert(0);
      break;
   }
   return false;
}
/// 2395

/// 2561
// Process GTK messages
void EnterMessageLoop()
{
   gboolean is_quit = false;
   user_match.valid = FALSE;
   erase_matcher_input();
   WaitingForCommand = true;

   while (WaitingForCommand && !is_quit)
      is_quit = gtk_main_iteration();

   // The message loop has been broken.  Either we got a message
   // that requires action (user clicked on a call, as opposed to
   // WM_PAINT), or gtk_main_quit() has been called.  The former case
   // is indicated by the message handler turning off WaitingForCommand.
   // Final exit is indicated by 'is_quit' becoming true while
   // WaitingForCommand remains true.

   if (is_quit) {
      // User closed the window.
#if 0
      delete GLOBprinter;
#endif
      general_final_exit(0);
   }
}


void iofull::display_help() {}

char *iofull::version_string ()
{
   return UI_VERSION_STRING "gtk";
}


void iofull::process_command_line(int *argcp, char ***argvp)
{
   /* Args are really processed in gnome_program_init; we just do a little
    * post-processing here. */

   if (window_size_str) {
         int nn = sscanf(window_size_str, "%dx%dx%dx%d",
                         &window_size_args[0],
                         &window_size_args[1],
                         &window_size_args[2],
                         &window_size_args[3]);

         // We allow the user to give two numbers (size only) or 4 numbers (size and position).
         if (nn != 2 && nn != 4) {
            gg->fatal_error_exit(1, "Bad size argument", window_size_str);
         }
   }
}



static void scan_menu(GtkListStore *main_list,
		      Cstring name, int *nLongest_p,
		      gint index, enum uims_reply choice_type)
{
   GtkTreeIter iter;
   // THIS FUNCTION should add each name/number/number row to the list,
   // also keeping track of the longest name it sees an ensuring that the
   // list is currently 'at least that wide'. (We're going to ignore that
   // last feature, for the moment.)
#if 0
   SIZE Size;

   GetTextExtentPoint(hDC, name, lstrlen(name), &Size);
   if ((Size.cx > *nLongest_p) && (Size.cx > CallsClientRect.right)) {
      SendMessage(hwndCallMenu, LB_SETHORIZONTALEXTENT, Size.cx, 0L);
      *nLongest_p = Size.cx;
   }
#endif

   gtk_list_store_insert_with_values(main_list, &iter, G_MAXINT,
				     0, name, 1, index, 2, choice_type, -1);
}



void ShowListBox(int nWhichOne)
{
   GtkTreeView *main_cmds = GTK_TREE_VIEW(SDG("main_cmds"));
   GtkListStore *main_list;
   if (nWhichOne != nLastOne) {

      nLastOne = nWhichOne;
      menu_moved = false;

      main_list = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
      gtk_list_store_clear(main_list);

      int nLongest = 0;

      if (nLastOne == match_number) {
         UpdateStatusBar("<number>");

         for (int iu=0 ; iu<NUM_CARDINALS; iu++)
            scan_menu(main_list, cardinals[iu], &nLongest, iu, (uims_reply) -1);
      }
      else if (nLastOne == match_circcer) {
         UpdateStatusBar("<circulate replacement>");

         for (unsigned int iu=0 ; iu<number_of_circcers ; iu++)
            scan_menu(main_list, circcer_calls[iu]->menu_name, &nLongest, iu, (uims_reply) -1);
      }
      else if (nLastOne >= match_taggers &&
               nLastOne < match_taggers+NUM_TAGGER_CLASSES) {
         int tagclass = nLastOne - match_taggers;

         UpdateStatusBar("<tagging call>");

         for (unsigned int iu=0 ; iu<number_of_taggers[tagclass] ; iu++)
            scan_menu(main_list, tagger_calls[tagclass][iu]->menu_name, &nLongest, iu, (uims_reply) -1);
      }
      else if (nLastOne == match_startup_commands) {
         UpdateStatusBar("<startup>");

         for (int i=0 ; i<num_startup_commands ; i++)
            scan_menu(main_list, startup_commands[i],
                      &nLongest, i, ui_start_select);
      }
      else if (nLastOne == match_resolve_commands) {
         for (int i=0 ; i<number_of_resolve_commands ; i++)
            scan_menu(main_list, resolve_command_strings[i],
                      &nLongest, i, ui_resolve_select);
      }
      else if (nLastOne == match_directions) {
         UpdateStatusBar("<direction>");

         for (int i=0 ; i<last_direction_kind ; i++)
            scan_menu(main_list, direction_names[i+1],
                      &nLongest, i, ui_special_concept);
      }
      else if (nLastOne == match_selectors) {
         UpdateStatusBar("<selector>");

         // Menu is shorter than it appears, because we are skipping first item.
         for (int i=0 ; i<selector_INVISIBLE_START-1 ; i++)
            scan_menu(main_list, selector_menu_list[i],
                      &nLongest, i, ui_special_concept);
      }
      else {
         int i;

         UpdateStatusBar(menu_names[nLastOne]);

         for (i=0; i<number_of_calls[nLastOne]; i++)
            scan_menu(main_list, main_call_lists[nLastOne][i]->menu_name,
                      &nLongest, i, ui_call_select);

         short int *item;
         int menu_length;

         if (allowing_all_concepts) {
            item = concept_list;
            menu_length = concept_list_length;
         }
         else {
            item = level_concept_list;
            menu_length = level_concept_list_length;
         }

         for (i=0 ; i<menu_length ; i++)
            scan_menu(main_list, concept_descriptor_table[item[i]].menu_name,
                      &nLongest, item[i], ui_concept_select);

         for (i=0 ;  ; i++) {
            Cstring name = command_menu[i].command_name;
            if (!name) break;
            scan_menu(main_list, name, &nLongest, i, ui_command_select);
         }
      }

      
      gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(main_cmds));
      gtk_tree_view_set_model(main_cmds, GTK_TREE_MODEL(main_list));
   }

#ifdef TAB_FOCUS
   ButtonFocusIndex = 0;
#endif
   gtk_widget_grab_focus(SDG("entry_cmd"));
}




void iofull::create_menu(call_list_kind cl) {}


uims_reply iofull::get_startup_command()
{
   nLastOne = ui_undefined;
   MenuKind = ui_start_select;
   ShowListBox(match_startup_commands);

   EnterMessageLoop();

   uims_menu_index = user_match.match.index;

   if (uims_menu_index < 0)
      /* Special encoding from a function key. */
      uims_menu_index = -1-uims_menu_index;
   else if (user_match.match.kind == ui_command_select) {
      /* Translate the command. */
      uims_menu_index = (int) command_command_values[uims_menu_index];
   }
   else if (user_match.match.kind == ui_start_select) {
      /* Translate the command. */
      uims_menu_index = (int) startup_command_values[uims_menu_index];
   }

   return user_match.match.kind;
}


bool iofull::get_call_command(uims_reply *reply_p)
{
   uims_reply my_reply;
   bool my_retval;
 startover:
   if (allowing_modifications)
      parse_state.call_list_to_use = call_list_any;

   SetTitle();
   nLastOne = ui_undefined;    /* Make sure we get a new menu,
                                  in case concept levels were toggled. */
   MenuKind = ui_call_select;
   ShowListBox(parse_state.call_list_to_use);
   my_retval = false;
   EnterMessageLoop();

   my_reply = user_match.match.kind;
   uims_menu_index = user_match.match.index;

   if (uims_menu_index < 0)
      /* Special encoding from a function key. */
      uims_menu_index = -1-uims_menu_index;
   else if (my_reply == ui_command_select) {
      /* Translate the command. */
      uims_menu_index = (int) command_command_values[uims_menu_index];
   }
   else if (my_reply == ui_special_concept) {
   }
   else {
      // Reject off-level concept accelerator key presses.
      if (!allowing_all_concepts && my_reply == ui_concept_select &&
          user_match.match.concept_ptr->level > higher_acceptable_level[calling_level])
         goto startover;

      call_conc_option_state save_stuff = user_match.match.call_conc_options;
      there_is_a_call = false;
      my_retval = deposit_call_tree(&user_match.match, (parse_block *) 0, 2);
      user_match.match.call_conc_options = save_stuff;
      if (there_is_a_call) {
         parse_state.topcallflags1 = the_topcallflags;
         my_reply = ui_call_select;
      }
   }

   *reply_p = my_reply;
   return my_retval;
}


uims_reply iofull::get_resolve_command()
{
   UpdateStatusBar(szResolveWndTitle);

   nLastOne = ui_undefined;
   MenuKind = ui_resolve_select;
   ShowListBox(match_resolve_commands);
#if 0
   my_retval = false;
#endif
   EnterMessageLoop();

   if (user_match.match.index < 0)
      uims_menu_index = -1-user_match.match.index;   // Special encoding from a function key.
   else
      uims_menu_index = (int) resolve_command_values[user_match.match.index];

   return user_match.match.kind;
}



popup_return iofull::do_comment_popup(char dest[])
{
   if (do_general_text_popup("Enter comment:", "", "", dest) == POPUP_ACCEPT_WITH_STRING)
      return POPUP_ACCEPT_WITH_STRING;
   else
      return POPUP_DECLINE;
}


popup_return iofull::do_outfile_popup(char dest[])
{
   char buffer[MAX_TEXT_LINE_LENGTH];
   snprintf(buffer, sizeof(buffer),
	   "Current sequence output file is \"%s\".", outfile_string);
   return do_general_text_popup(buffer,
                                "Enter new name (or '+' to base it on today's date):",
                                outfile_string,
                                dest);
}


popup_return iofull::do_header_popup(char dest[])
{
   char myPrompt[MAX_TEXT_LINE_LENGTH];

   if (header_comment[0])
      snprintf(myPrompt, sizeof(myPrompt),
	       "Current title is \"%s\".", header_comment);
   else
      myPrompt[0] = 0;

   return do_general_text_popup(myPrompt, "Enter new title:", "", dest);
}


popup_return iofull::do_getout_popup (char dest[])
{
   char buffer[MAX_TEXT_LINE_LENGTH+MAX_FILENAME_LENGTH];

   if (header_comment[0]) {
      snprintf(buffer, sizeof(buffer),
	       "Session title is \"%s\".", header_comment);
      return
         do_general_text_popup(buffer,
                               "You can give an additional comment for just this sequence:",
                               "",
                               dest);
   }
   else {
      snprintf(buffer, sizeof(buffer),
	       "Output file is \"%s\".", outfile_string);
      return do_general_text_popup(buffer, "Sequence comment:", "", dest);
   }
}

int iofull::yesnoconfirm(char *title, char *line1, char *line2, bool excl, bool info)
{
   GtkWidget * dialog;
   GtkMessageType m_type;
   int result;

   m_type = (excl) ? GTK_MESSAGE_WARNING : (info) ? GTK_MESSAGE_INFO :
      GTK_MESSAGE_QUESTION;

   dialog = gtk_message_dialog_new
      (GTK_WINDOW(window_main), GTK_DIALOG_DESTROY_WITH_PARENT,
       m_type, GTK_BUTTONS_YES_NO, "%s", line2);
   if (line1 && line1[0])
      gtk_message_dialog_format_secondary_text
	 (GTK_MESSAGE_DIALOG(dialog), "%s", line1);
   gtk_window_set_title(GTK_WINDOW(dialog), title);
   
   result = gtk_dialog_run (GTK_DIALOG (dialog));
   gtk_widget_destroy (dialog);
   if (result == GTK_RESPONSE_YES)
      return POPUP_ACCEPT;
   else
      return POPUP_DECLINE;
}

int iofull::do_abort_popup()
{
   return yesnoconfirm("Confirmation", (char *) 0,
                       "Do you really want to abort this sequence?", true, false);
}


static bool do_popup(int nWhichOne)
{
   uims_reply SavedMenuKind = MenuKind;
   nLastOne = ui_undefined;
   MenuKind = ui_call_select;
#ifdef TAB_FOCUS
   InPopup = true;
   ButtonFocusHigh = 3;
   ButtonFocusIndex = 0;
   PositionAcceptButtons();
#endif
   gtk_widget_show(SDG("main_cancel"));
   ShowListBox(nWhichOne);
   EnterMessageLoop();
#ifdef TAB_FOCUS
   InPopup = false;
   ButtonFocusHigh = 2;
   ButtonFocusIndex = 0;
   PositionAcceptButtons();
#endif
   gtk_widget_hide(SDG("main_cancel"));
   MenuKind = SavedMenuKind;
   // A value of -1 means that the user hit the "cancel" button.
   return (user_match.match.index >= 0);
}


int iofull::do_selector_popup()
{
   int retval = 0;
   match_result saved_match = user_match;

   // We skip the zeroth selector, which is selector_uninitialized.
   if (do_popup((int) match_selectors)) retval = user_match.match.index+1;
   user_match = saved_match;
   return retval;
}


int iofull::do_direction_popup()
{
   int retval = 0;
   match_result saved_match = user_match;

   // We skip the zeroth direction, which is direction_uninitialized.
   if (do_popup((int) match_directions)) retval = user_match.match.index+1;
   user_match = saved_match;
   return retval;
}



int iofull::do_circcer_popup()
{
   uint32 retval = 0;

   if (interactivity == interactivity_verify) {
      retval = verify_options.circcer;
      if (retval == 0) retval = 1;
   }
   else if (!user_match.valid || (user_match.match.call_conc_options.circcer == 0)) {
      match_result saved_match = user_match;
      if (do_popup((int) match_circcer))
         retval = user_match.match.call_conc_options.circcer;
      user_match = saved_match;
   }
   else {
      retval = user_match.match.call_conc_options.circcer;
      user_match.match.call_conc_options.circcer = 0;
   }

   return retval;
}



int iofull::do_tagger_popup(int tagger_class)
{
   match_result saved_match = user_match;
   saved_match.match.call_conc_options.tagger = 0;

   if (do_popup(((int) match_taggers) + tagger_class))
      saved_match.match.call_conc_options.tagger = user_match.match.call_conc_options.tagger;
   user_match = saved_match;

   int retval = user_match.match.call_conc_options.tagger;
   user_match.match.call_conc_options.tagger = 0;
   return retval;
}


uint32 iofull::get_number_fields(int nnumbers, bool forbid_zero)
{
   int i;
   uint32 number_fields = user_match.match.call_conc_options.number_fields;
   int howmanynumbers = user_match.match.call_conc_options.howmanynumbers;
   uint32 number_list = 0;

   for (i=0 ; i<nnumbers ; i++) {
      uint32 this_num = 99;

      if (!user_match.valid || (howmanynumbers <= 0)) {
         match_result saved_match = user_match;
         if (do_popup((int) match_number))
            this_num = user_match.match.index;
         user_match = saved_match;
      }
      else {
         this_num = number_fields & 0xF;
         number_fields >>= 4;
         howmanynumbers--;
      }

      if (forbid_zero && this_num == 0) return ~0UL;
      if (this_num > 15) return ~0UL;    /* User gave bad answer. */
      number_list |= (this_num << (i*4));
   }

   return number_list;
}


void iofull::add_new_line(char the_line[], uint32 drawing_picture)
{
   erase_questionable_stuff();
   strncpy(CurDisplay->Line, the_line, DISPLAY_LINE_LENGTH-1);
   CurDisplay->Line[DISPLAY_LINE_LENGTH-1] = 0;
   CurDisplay->in_picture = drawing_picture;

   if ((CurDisplay->in_picture & 1) && ui_options.no_graphics == 0) {
      if ((CurDisplay->in_picture & 2)) {
         CurDisplay->Height = BMP_PERSON_SIZE+BMP_PERSON_SPACE;
         CurDisplay->DeltaToNext = (BMP_PERSON_SIZE+BMP_PERSON_SPACE)/2;
      }
      else {
         if (!CurDisplay->Line[0])
            CurDisplay->Height = 0;
         else
            CurDisplay->Height = BMP_PERSON_SIZE+BMP_PERSON_SPACE;

         CurDisplay->DeltaToNext = CurDisplay->Height;
      }
   }
   else {
#if 0
      CurDisplay->Height = TranscriptTextHeight;
      CurDisplay->DeltaToNext = TranscriptTextHeight;
#else
      assert(0);
#endif
   }

   if (!CurDisplay->Next) {
      CurDisplay->Next = (DisplayType *) get_mem(sizeof(DisplayType));
      CurDisplay->Next->Prev = CurDisplay;
      CurDisplay = CurDisplay->Next;
      CurDisplay->Next = NULL;
   }
   else
      CurDisplay = CurDisplay->Next;

   CurDisplay->Line[0] = -1;

   Update_text_display();
}



void iofull::reduce_line_count(int n)
{
   CurDisplay = DisplayRoot;
   while (CurDisplay->Line[0] != -1 && n--) {
      CurDisplay = CurDisplay->Next;
   }

   CurDisplay->Line[0] = -1;

   Update_text_display();
}


void iofull::update_resolve_menu(command_kind goal, int cur, int max,
                                 resolver_display_state state)
{
   create_resolve_menu_title(goal, cur, max, state, szResolveWndTitle);
   UpdateStatusBar(szResolveWndTitle);
   // Put it in the transcript area also, where it's easy to see.
   gg->add_new_line(szResolveWndTitle, 0);
}


bool iofull::choose_font()
{
#if 0
   GLOBprinter->choose_font();
#else
   assert(0); // unimplemented
#endif
   return true;
}

bool iofull::print_this()
{
#if 0
   GLOBprinter->print_this(outfile_string, szMainTitle, false);
#else
   assert(0); // unimplemented
#endif
   return true;
}

bool iofull::print_any()
{
#if 0
   GLOBprinter->print_any(szMainTitle, false);
#else
   assert(0); // unimplemented
#endif
   return true;
}


void iofull::bad_argument(Cstring s1, Cstring s2, Cstring s3)
{
   // Argument s3 isn't important.  It only arises when the level can't
   // be parsed, and it consists of a list of all the available levels.
   // In Sd, they were all on the menu.

   gg->fatal_error_exit(1, s1, s2);
}


void iofull::fatal_error_exit(int code, Cstring s1, Cstring s2)
{
   char msg[200];
   if (s2 && s2[0]) {
      snprintf(msg, sizeof(msg), "%s: %s", s1, s2);
      s1 = msg;   // Yeah, we can do that.  Yeah, it's sleazy.
   }

   gg->serious_error_print(s1);
   session_index = 0;  // Prevent attempts to update session file.
   general_final_exit(code);
}


void iofull::serious_error_print(Cstring s1)
{
   GtkWidget * dialog = gtk_message_dialog_new
      (GTK_WINDOW(window_main), GTK_DIALOG_DESTROY_WITH_PARENT,
       GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
       "Error: %s", s1);
   gtk_dialog_run (GTK_DIALOG (dialog));
   gtk_widget_destroy (dialog);
}


void iofull::terminate(int code)
{
   if (window_main) {
      // Check whether we should write out the transcript file.
      if (code == 0 && wrote_a_sequence) {
         if (yesnoconfirm("Confirmation", (char *) 0,
                          "Do you want to print the file?",
                          false, true) == POPUP_ACCEPT)
#if 0
            GLOBprinter->print_this(outfile_string, szMainTitle, false);
#else
	 assert(0); /* unimplemented */
#endif
      }

      do_my_final_shutdown();
   }

   if (ico_pixbuf)
      g_object_unref(ico_pixbuf);
   // XXX free the bitmap?

   exit(code);
}
/// 3210
