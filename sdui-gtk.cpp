// SD -- square dance caller's helper.
//
//    This file copyright (C) 2005  C. Scott Ananian.
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
static GtkWidget *window_startup, *window_main;
static GdkPixbuf *ico_pixbuf;
#define SDG(widget_name) (glade_xml_get_widget(sd_xml, (widget_name)))

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

static char szOutFilename     [MAX_TEXT_LINE_LENGTH];
static char szDatabaseFilename[MAX_TEXT_LINE_LENGTH];
static char szResolveWndTitle [MAX_TEXT_LINE_LENGTH];

// This is the last title sent by the main program.  We add stuff to it.
static gchar main_title[MAX_TEXT_LINE_LENGTH];



static void uims_bell()
{
    if (!ui_options.no_sound) gdk_beep();
}


static void UpdateStatusBar(gchar *text)
{
   assert(text);

#if 0
   if (allowing_modifications || allowing_all_concepts ||
       using_active_phantoms || allowing_minigrand ||
       ui_options.singing_call_mode || ui_options.nowarn_mode) {
      (void) SendMessage(hwndStatusBar, SB_SETPARTS, 7, (LPARAM) StatusBarDimensions);

      SendMessage(hwndStatusBar, SB_SETTEXT, 1,
                  (LPARAM) ((allowing_modifications == 2) ? "all mods" :
                            (allowing_modifications ? "simple mods" : "")));

      SendMessage(hwndStatusBar, SB_SETTEXT, 2,
                  (LPARAM) (allowing_all_concepts ? "all concepts" : ""));

      SendMessage(hwndStatusBar, SB_SETTEXT, 3,
                  (LPARAM) (using_active_phantoms ? "act phan" : ""));

      SendMessage(hwndStatusBar, SB_SETTEXT, 4,
                  (LPARAM) ((ui_options.singing_call_mode == 2) ? "rev singer" :
                            (ui_options.singing_call_mode ? "singer" : "")));

      SendMessage(hwndStatusBar, SB_SETTEXT, 5,
                  (LPARAM) (ui_options.nowarn_mode ? "no warn" : ""));

      SendMessage(hwndStatusBar, SB_SETTEXT, 6,
                  (LPARAM) (allowing_minigrand ? "minigrand" : ""));
   }
   else {
      (void) SendMessage(hwndStatusBar, SB_SETPARTS, 1, (LPARAM) StatusBarDimensions);
   }

   (void) SendMessage(hwndStatusBar, SB_SETTEXT, 0, (LPARAM) szGLOBFirstPane);
   (void) SendMessage(hwndStatusBar, SB_SIMPLE, 0, 0);
   UpdateWindow(hwndStatusBar);
#endif
}
///// 259
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
void about_open_url(GtkAboutDialog *about, const gchar *link, gpointer data) {
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
int iofull::do_abort_popup() { assert(0); }
uims_reply iofull::get_startup_command() { assert(0); }
void iofull::add_new_line(char the_line[], uint32 drawing_picture) { assert(0); }
void iofull::reduce_line_count(int n) { assert(0); }
void iofull::update_resolve_menu(command_kind goal, int cur, int max,
                                    resolver_display_state state) { assert(0); }
void iofull::show_match() { assert(0); }
uims_reply iofull::get_resolve_command() { assert(0); }
bool iofull::choose_font() { assert(0); }
bool iofull::print_this() { assert(0); }
bool iofull::print_any() { assert(0); }
popup_return iofull::do_outfile_popup(char dest[]) { assert(0); }
popup_return iofull::do_header_popup(char dest[]) { assert(0); }
popup_return iofull::do_getout_popup(char dest[]) { assert(0); }
void iofull::fatal_error_exit(int code, Cstring s1, Cstring s2) { assert(0); }
void iofull::serious_error_print(Cstring s1) { assert(0); }
void iofull::create_menu(call_list_kind cl) { assert(0); }
int iofull::do_selector_popup() { assert(0); }
int iofull::do_direction_popup() { assert(0); }
int iofull::do_circcer_popup() { assert(0); }
int iofull::do_tagger_popup(int tagger_class) { assert(0); }
int iofull::yesnoconfirm(char *title, char *line1, char *line2, bool excl, bool info) { assert(0); }
popup_return iofull::do_comment_popup(char dest[]) { assert(0); }
uint32 iofull::get_number_fields(int nnumbers, bool forbid_zero) { assert(0); }
bool iofull::get_call_command(uims_reply *reply_p) { assert(0); }
void iofull::terminate(int code) { assert(0); }
void iofull::bad_argument(Cstring s1, Cstring s2, Cstring s3) { assert(0); }
void iofull::final_initialize() { assert(0); }

////////// 1815
static void setup_level_menu(GtkListStore *startup_list)
{
   GtkTreeIter iter;
   int lev;

   for (lev=(int)l_mainstream ; ; lev++) {
      Cstring this_string = getout_strings[lev];
      if (!this_string[0]) break;
      gtk_list_store_insert_with_values(startup_list, &iter, G_MAXINT,
					0, this_string, -1);
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
      startup_list = gtk_list_store_new (1, G_TYPE_STRING);
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
					   0, line, -1);
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
}

bool iofull::init_step(init_callback_state s, int n)
{
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
	 startup_init();
	 gtk_widget_show(window_startup);
	 result = gtk_dialog_run(GTK_DIALOG(window_startup));
	 if (result!=GTK_RESPONSE_ACCEPT) {
	    // User hit the "cancel" button.
	    session_index = 0;  // Prevent attempts to update session file.
	    general_final_exit(0);
            gg->fatal_error_exit(1, "Startup cancelled", "");
	 }
	 gtk_widget_hide(window_startup);
      }

      if (request_deletion) return true;
      break;

   case final_level_query:
      calling_level = l_mainstream;   // User really doesn't want to tell us the level.
      strncat(outfile_string, filename_strings[calling_level], MAX_FILENAME_LENGTH);
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

#if 0
      UpdateWindow(hwndMain);

      UpdateStatusBar("Reading database");
      break;

   case init_database2:
      ShowWindow(hwndProgress, SW_SHOWNORMAL);
      UpdateStatusBar("Creating Menus");
      break;

   case calibrate_tick:
      SendMessage(hwndProgress, PBM_SETRANGE, 0, MAKELONG(0, n));
      SendMessage(hwndProgress, PBM_SETSTEP, 1, 0);
      break;

   case do_tick:
      SendMessage(hwndProgress, PBM_SETSTEP, n, 0);
      SendMessage(hwndProgress, PBM_STEPIT, 0, 0);
      break;

   case tick_end:
      break;

   case do_accelerator:
      ShowWindow(hwndProgress, SW_HIDE);
      UpdateStatusBar("Processing Accelerator Keys");
#endif
      break;
   }
   return false;
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

#if 0
int main2(int argc, char *argv[]) {
    /* show everything (well, for now) */
    gtk_widget_show(dialog_startup);
    gtk_widget_show(about_sd);
    gtk_widget_show(app_main);


    /* start the event loop */
    gtk_main();

    return 0;
}
#endif

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
   GtkTreeViewColumn *column;
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
		      GNOME_PARAM_APP_DATADIR, PACKAGE_DATA_DIR,
		      GNOME_PARAM_POPT_TABLE, all_options,
		      NULL);
   // initialize the interface.

   sd_xml = glade_xml_new("sd.glade", NULL, NULL);
   window_startup = SDG("dialog_startup");
   window_main = SDG("app_main");

   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes("Level/Session",renderer,
						     "text", 0, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(SDG("startup_list")), column);
   column = gtk_tree_view_column_new_with_attributes("Commands", renderer,
						     "text", 0, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(SDG("main_cmds")), column);

   /* Load the (inlined) sd icon. */
   ico_pixbuf = gdk_pixbuf_new_from_inline (-1, sdico_inline, FALSE, NULL);
   if (ico_pixbuf) {
      gtk_window_set_icon(GTK_WINDOW(window_main), ico_pixbuf);
      gtk_window_set_icon(GTK_WINDOW(window_startup), ico_pixbuf);
      // note that there's still an outstanding ref to ico_pixbuf, but
      // that's okay, because we've stashed it away in a static variable
      // for use later.
   }
   
   /* connect the signals in the interface */
   glade_xml_signal_autoconnect(sd_xml);
   gtk_about_dialog_set_url_hook(about_open_url, NULL, NULL);

   // Run the Sd program.  We'll have parsed the Sd-appropriate command-line
   // arguments into 'fake_args'.

   return sdmain(fake_args.argc, fake_args.argv);
}
