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
#include <glib/gprintf.h>
#include <librsvg/rsvg.h>

#include "sd.h"
#include "paths.h"
#include "sdui-ico.h" // sd icon
#include "sdui-chk.h" // Checker images
#include "resource.h" // for menu command constants
extern "C" {
#include "sdui-gtk.h" /* GLADE callback function prototypes */
// misc. prototypes.
static void about_open_url(GtkAboutDialog *about,
			   const gchar *link, gpointer data);
static void stash_away_sd_options(poptContext con,
				  enum poptCallbackReason reason,
				  const struct poptOption *opt,
				  const char *args, void * data);
static void use_computed_match(void);
}

// GLADE interface definitions
static GladeXML *sd_xml;
static GtkWidget *window_startup, *window_main, *window_font, *window_text;
static GdkPixbuf *ico_pixbuf;
static GtkTextBuffer *main_buffer;
#define SDG(widget_name) (glade_xml_get_widget(sd_xml, (widget_name)))
#define gtk_update() while (gtk_events_pending ()) gtk_main_iteration ()
#define QUESTION_MARK "questionable_stuff_to_erase"

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

#define ui_undefined -999

static char szResolveWndTitle [MAX_TEXT_LINE_LENGTH];

#ifdef TAB_FOCUS
static bool InPopup = false;
#endif
static bool WaitingForCommand = false;
static int nLastOne;
static uims_reply MenuKind;

// This is the last title sent by the main program.  We add stuff to it.
static gchar main_title[MAX_TEXT_LINE_LENGTH];



static void uims_bell()
{
    if (!ui_options.no_sound) gdk_beep();
}


static void UpdateStatusBar(const gchar *text)
{
   gchar buf[strlen(text)+15+16+12+14+11+13+1];
   assert(text);

   g_snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s",
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


static void erase_questionable_stuff()
{
   GtkTextMark *mark;
   GtkTextIter iter1, iter2;
   mark = gtk_text_buffer_get_mark(main_buffer, QUESTION_MARK);
   if (!mark) return; // no questionable stuff.
   gtk_text_buffer_get_iter_at_mark(main_buffer, &iter1, mark);
   gtk_text_buffer_get_end_iter(main_buffer, &iter2);
   gtk_text_buffer_delete(main_buffer, &iter1, &iter2);
   gtk_text_buffer_delete_mark(main_buffer, mark);
}


void iofull::show_match()
{
   char szLocalString[MAX_TEXT_LINE_LENGTH];
   g_snprintf(szLocalString, sizeof(szLocalString),
	      "%s%s%s", GLOB_match.indent ? "   " : "", GLOB_user_input,
	      GLOB_full_extension);
   gg->add_new_line(szLocalString, 0);
}


static gint select_partial(char *partial) {
   GtkTreeView *main_cmds;
   GtkTreeModel *cmd_list;
   GtkTreeIter iter;
   gboolean valid;
   gint idx=0, partial_len;

   partial_len = strlen(partial);
   if (!partial_len) return -1; // no matches for empty string.
   main_cmds = GTK_TREE_VIEW(SDG("main_cmds"));
   cmd_list = gtk_tree_view_get_model(main_cmds);
   if (!cmd_list) return -1; // no model set yet.
   valid = gtk_tree_model_get_iter_first(cmd_list, &iter);
   while (valid) {
      gchar *cmd;
      gtk_tree_model_get(cmd_list, &iter, 0, &cmd, -1);
      // check partial match.
      if (strncasecmp(partial, cmd, partial_len)==0)
	 return idx; // this is it.
      g_free(cmd);
      idx++;
      valid = gtk_tree_model_iter_next(cmd_list, &iter);
   }
   // no match found.
   return -1;
}

static gboolean warp_to_entry_end(gpointer data) {
   gtk_editable_set_position(GTK_EDITABLE(data), -1);
   return FALSE; // once only
}
static gboolean scroll_transcript_to_mark(gpointer data) {
   GtkTextView *main_transcript = GTK_TEXT_VIEW(SDG("main_transcript"));
   GtkTextMark *mark = GTK_TEXT_MARK(data);
   gtk_text_view_scroll_mark_onscreen(main_transcript, mark);
   gtk_text_buffer_delete_mark(main_buffer, mark);
   return FALSE; // once only
}
static void check_text_change(bool doing_escape)
{
   char szLocalString[MAX_TEXT_LINE_LENGTH];
   int nLen;
   int nIndex;
   char *p;
   int matches;
   bool changed_editbox = false;

   // Find out what the text input box contains now.

   p = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(SDG("main_entry"))), -1);
   g_snprintf(szLocalString, sizeof(szLocalString), "%s%n", p, &nLen);
   g_free(p);
   nLen--;     // Location of last character.

   // Only process stuff if it changed from what we thought it was.
   // Otherwise we would unnecessarily process changes that weren't typed
   // by the user but were merely the stuff we put in due to completion.

   if (doing_escape) {
      nLen++;
      matches = match_user_input(nLastOne, false, false, false);
      user_match = GLOB_match;
      p = GLOB_echo_stuff;
      if (*p) {
         changed_editbox = true;

	 p = g_utf8_strdown(p, -1);
	 nLen = g_strlcat(szLocalString, p, sizeof(szLocalString));
	 nLen--;
	 g_free(p);

      }
   }
   else if (strcmp(szLocalString, GLOB_user_input)) {
      if (nLen >= 0) {
         char cCurChar = szLocalString[nLen];

         if (cCurChar == '!' || cCurChar == '?') {
	    GtkTextView *main_transcript;
            szLocalString[nLen] = '\0';   // Erase the '?' itself.

            // Before we start, erase any previous stuff.
            erase_questionable_stuff();

            // Next line used to be "nLen > 0" so that we wouldn't do this if
            // the line was blank.  It was believed that the '?' operation
            // was unwieldy if it printed absolutely everything.  Maybe it
            // was when computers were slower.  But it seems fine now.
            // In particular, on a 150 MHz machine (very slow in 2002),
            // it takes less than 2 seconds to show the full output from
            // a question mark at C4.
            if (nLen > -33) {    // Don't do this on a blank line.
	       GtkTextMark *start_mark, *end_mark;
	       GtkTextIter iter;
	       main_transcript = GTK_TEXT_VIEW(SDG("main_transcript"));
	       gtk_text_buffer_get_end_iter(main_buffer, &iter);
	       start_mark = gtk_text_buffer_create_mark
		  (main_buffer, NULL, &iter, TRUE);

	       g_snprintf(GLOB_user_input, sizeof(GLOB_user_input),
			  "%s%n", szLocalString, &GLOB_user_input_size);
               // This will call show_match with each match.
               (void) match_user_input(nLastOne, true, cCurChar == '?', false);

               // Restore the scroll position so that the user will see the start,
               // not the end, of what we displayed.

               // Don't hide stuff unnecessarily.  If we would have space to show
               // some pre-existing text as well as all "?" output, do so.
	       gtk_text_buffer_get_end_iter(main_buffer, &iter);
	       end_mark = gtk_text_buffer_create_mark
		  (main_buffer, NULL, &iter, FALSE);
	       gtk_text_view_scroll_mark_onscreen(main_transcript, end_mark);
	       // allow this first scroll to complete before we do this second
	       g_idle_add_full(G_PRIORITY_LOW,
			       scroll_transcript_to_mark,
			       start_mark, NULL);

	       // make the start mark into the QUESTION_MARK (can't do this
	       // before 'match_user_input' or it would be erased)
	       gtk_text_buffer_get_iter_at_mark(main_buffer,&iter, start_mark);
	       gtk_text_buffer_create_mark
		  (main_buffer, QUESTION_MARK, &iter, TRUE);
	       gtk_text_buffer_delete_mark(main_buffer, end_mark);
               // Give focus to the transcript, so the user can scroll easily.
	       gtk_widget_grab_focus(GTK_WIDGET(main_transcript));
            }
            changed_editbox = true;
         }
         else if (cCurChar == ' ' || cCurChar == '-') {
            erase_questionable_stuff();
	    g_snprintf(GLOB_user_input, sizeof(GLOB_user_input),
		       "%s%n", szLocalString, &GLOB_user_input_size);
	    GLOB_user_input[nLen] = '\0'; // chop off the space/hyphen
            // Extend only to one space or hyphen, inclusive.
            matches = match_user_input(nLastOne, false, false, true);
            user_match = GLOB_match;
            p = GLOB_echo_stuff;

            if (*p) {
               changed_editbox = true;

               while (*p) {
                  if (*p != ' ' && *p != '-') {
                     szLocalString[nLen++] = *p++;
                     szLocalString[nLen] = '\0';
                  }
                  else {
                     szLocalString[nLen++] = cCurChar;
                     szLocalString[nLen] = '\0';
                     goto pack_us;
                  }
               }
            }
            else if (!GLOB_space_ok || matches <= 1) {
               uims_bell();
               szLocalString[nLen] = '\0';    // Do *not* pack the character.
               changed_editbox = true;
            }
         }
         else
            erase_questionable_stuff();
      }
      else {
         erase_questionable_stuff();
         goto scroll_call_menu;
      }
   }
   else {
      goto scroll_call_menu;
   }

 pack_us:

   p = g_utf8_strdown(szLocalString, -1);
   g_snprintf(GLOB_user_input, sizeof(GLOB_user_input),
	      "%s%n", p, &GLOB_user_input_size);
   g_free(p);

   // Write it back to the window.

   if (changed_editbox) {
      GtkEntry *main_entry = GTK_ENTRY(SDG("main_entry"));
      g_signal_handlers_block_by_func
	 (main_entry, (void*)on_main_entry_changed, NULL);
      gtk_entry_set_text(main_entry, szLocalString);
      g_signal_handlers_unblock_by_func
	 (main_entry, (void*)on_main_entry_changed, NULL);
      // This moves the cursor to the end of the text, apparently.
      // TRICKY - have to do it *after* on_entry_changed() returns
      // (signal is called before the key-insertion code finishes)
      g_idle_add_full(G_PRIORITY_HIGH_IDLE, warp_to_entry_end,
		      GTK_EDITABLE(main_entry), NULL);
   }

 scroll_call_menu:

   // Search call menu for match on current string.
   nIndex = select_partial(szLocalString);

   if (nIndex >= 0) {
      // If possible, scroll the call menu so that
      // current selection remains centered.
      GtkTreeView *main_cmds = GTK_TREE_VIEW(SDG("main_cmds"));
      GtkTreeSelection *sel = gtk_tree_view_get_selection(main_cmds);
      GtkTreePath *path = gtk_tree_path_new_from_indices(nIndex, -1);
      gtk_tree_selection_select_path(sel, path);
      gtk_tree_view_scroll_to_cell(main_cmds, path, NULL, FALSE, 0., 0.);
      gtk_tree_path_free(path);
      menu_moved = false;
   }
   else if (!szLocalString[0]) {
      GtkTreePath *path;
      // No match and no string.
      nIndex = 0;  // Select first entry in call menu.
      path = gtk_tree_path_new_first();
      gtk_tree_view_set_cursor(GTK_TREE_VIEW(SDG("main_cmds")),
			       path, NULL, FALSE);
      gtk_tree_path_free(path);
   }
   else
      menu_moved = false;
}
/// 487
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
   GtkWidget *line1 = SDG("text_line1"), *line2 = SDG("text_line2");
   GtkWidget *entry = SDG("text_entry");
   int result;
   gtk_widget_show(line1);
   gtk_widget_show(line2);
   gtk_label_set_text(GTK_LABEL(line1), prompt1);
   gtk_label_set_text(GTK_LABEL(line2), prompt2);
   if (prompt1[0]=='\0') gtk_widget_hide(line1);
   if (prompt2[0]=='\0') gtk_widget_hide(line2);
   gtk_entry_set_text(GTK_ENTRY(entry), seed);
   gtk_widget_grab_focus(entry);
   gtk_widget_show(window_text);
   result = gtk_dialog_run(GTK_DIALOG(window_text));
   gtk_widget_hide(window_text);
   dest[0] = 0;
   if (result == GTK_RESPONSE_OK) {
      const gchar *txt = gtk_entry_get_text(GTK_ENTRY(entry));
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
static void stash_away_sd_options(poptContext con,
				  enum poptCallbackReason reason,
				  const struct poptOption *opt,
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
	if (fake_args.argc==0) // fake the program name.
	   fake_args.argv[fake_args.argc++] = "sd";
    }
    // okay, stash away this option
    assert(opt->longName);
    n = strlen(opt->longName)+2;
    buf = (char*) get_mem(n);
    g_snprintf(buf, n, "-%s", opt->longName);
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
    {NULL, 0, POPT_ARG_CALLBACK,
     (void*)((poptCallbackType)stash_away_sd_options), 0, NULL, NULL},
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

   /* Hide the progress bar (initially) */
   gtk_widget_hide(GTK_WIDGET(gnome_appbar_get_progress
			      (GNOME_APPBAR(SDG("main_appbar")))));

   /* Set up the various treeview widgets */
   startup_list = GTK_TREE_VIEW(SDG("startup_list"));
   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes("Filename",renderer,
						     "text", 1, NULL);
   gtk_tree_view_append_column(startup_list, column);
   column = gtk_tree_view_column_new_with_attributes("Level",renderer,
						     "text", 2, NULL);
   gtk_tree_view_append_column(startup_list, column);
   column = gtk_tree_view_column_new_with_attributes("Sequence #",renderer,
						     "text", 3, NULL);
   gtk_tree_view_append_column(startup_list, column);
   column = gtk_tree_view_column_new_with_attributes("Commands", renderer,
						     "text", 0, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(SDG("main_cmds")), column);

   /* Set up the transcript buffer */
   main_buffer = gtk_text_buffer_new(NULL);
   gtk_text_view_set_buffer(GTK_TEXT_VIEW(SDG("main_transcript")),main_buffer);
   gtk_text_buffer_create_tag(main_buffer, "picture",
			      "family", "monospace", NULL);

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

   /* Set default buttons */
   gtk_dialog_set_default_response(GTK_DIALOG(window_startup),GTK_RESPONSE_OK);
   gtk_dialog_set_default_response(GTK_DIALOG(window_text), GTK_RESPONSE_OK);
   gtk_dialog_set_default_response(GTK_DIALOG(window_font), GTK_RESPONSE_OK);
   
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
   static const gchar *license =
      "SD comes with ABSOLUTELY NO WARRANTY; for details see the license.\n"
      "This is free software, and you are welcome to redistribute "
      "it under certain conditions; for details see the "
      "license.\n"
      "You should have received a copy of the GNU General Public "
      "License along with this program, in the file "
      "\"COPYING.txt\"; if not, write to the Free Software "
      "Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA "
      "02111-1307 USA.";
   gtk_show_about_dialog(GTK_WINDOW(window_main),
			 "name", PACKAGE,
			 "version", VERSION,
			 "logo", ico_pixbuf,
			 "copyright", copyright,
			 "comments", "Sd: Square Dance Caller's Helper.",
			 "license", license,
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
void
on_main_entry_changed(GtkEditable *editable, gpointer user_data) {
   check_text_change(false);
}
void
on_main_cancel_clicked(GtkButton *button, gpointer user_data) {
      user_match.match.index = -1;
      WaitingForCommand = false;
}
gboolean
on_main_cmds_button_press_event(GtkWidget *widget, GdkEventButton *event,
				gpointer user_data) {
   GtkTreeView *main_cmds = GTK_TREE_VIEW(widget);
      // See whether this an appropriate single-click or double-click.
   if (event->type ==
       (ui_options.accept_single_click ? GDK_BUTTON_PRESS : GDK_2BUTTON_PRESS)
       && event->window == gtk_tree_view_get_bin_window(main_cmds))
      {
	 GtkTreePath *path;
	 // kinda annoying: have to handle selection ourself for single-click
	 // case =(
	 gtk_tree_view_get_path_at_pos
	    (main_cmds, (gint)event->x, (gint)event->y, &path, NULL,NULL,NULL);
	 gtk_tree_selection_select_path
	    (gtk_tree_view_get_selection(main_cmds), path);
	 gtk_tree_path_free(path);
	 // okay, now 'click' the accept button.
	 on_main_accept_clicked(GTK_BUTTON(SDG("main_accept")), NULL);
	 return TRUE; /* I handled this */
      }
  return FALSE;
}
void
on_main_cmds_row_activated(GtkTreeView *treeview, GtkTreePath *path,
			   GtkTreeViewColumn *column, gpointer user_data) {
    // 'click' the accept button.
    on_main_accept_clicked(GTK_BUTTON(SDG("main_accept")), NULL);
}

gint
on_key_snoop(GtkWidget *grab_widget, GdkEventKey *event, gpointer func_data) {
   GtkWidget *main_entry = SDG("main_entry");

   // special bindings when in main entry area.
   if (gtk_widget_is_ancestor(main_entry, grab_widget) &&
       gtk_widget_is_focus(main_entry) &&
       event->type==GDK_KEY_PRESS &&
       // no shift keys (although caps-lock is okay)
       0== (event->state & (GDK_MODIFIER_MASK-GDK_RELEASE_MASK-GDK_LOCK_MASK)))
      switch (event->keyval) {
      case GDK_ISO_Enter:
      case GDK_KP_Enter:
      case GDK_Return:
	 on_main_accept_clicked(GTK_BUTTON(SDG("main_accept")), main_entry);
	 return TRUE; // handled it.
      case GDK_Tab:
      case GDK_KP_Tab:
      case GDK_ISO_Left_Tab:
	 if (ui_options.tab_changes_focus) break;
	 goto completion;
      case GDK_Escape:
      completion:
	 check_text_change(true);
	 return TRUE; // handled it.
      default:
	 break;
      }
   // XXX: handle special sd accelerators. (see LookupKeystrokeBinding)
   // handle key-stroke normally.
   return FALSE;
}

void
on_main_accept_clicked(GtkButton *button, gpointer user_data) {
      GtkTreeView *main_cmds;
      GtkTreeSelection *sel;
      GtkTreeIter menuIter;
      GtkTreeModel *main_list;
      gboolean is_selection, is_enter_pressed;
      int matches;

      erase_questionable_stuff();

      main_cmds = GTK_TREE_VIEW(SDG("main_cmds"));
      sel = gtk_tree_view_get_selection(main_cmds);
      is_selection=gtk_tree_selection_get_selected(sel, &main_list, &menuIter);
      is_enter_pressed = user_data ? true : false;

      // If the user moves around in the call menu (listbox) while there is
      // stuff in the edit box, and then types a CR, we need to clear the
      // edit box, so that the listbox selection will be taken exactly.
      // This is because the wandering around in the call menu may have
      // gone to something that has nothing to do with what was typed
      // in the edit box.  We detect this condition by noticing that the
      // listbox selection has changed from what we left it when we were
      // last trying to make the call menu track the edit box.

      // We also do this if the user selected by clicking the mouse.

      if ((!is_enter_pressed) || menu_moved) {
	 gtk_entry_set_text(GTK_ENTRY(SDG("main_entry")), "");
         erase_matcher_input();
      }

      // Look for abbreviations.

      {
         abbrev_block *asearch = (abbrev_block *) 0;

         if (nLastOne == match_startup_commands)
            asearch = abbrev_table_start;
         else if (nLastOne == match_resolve_commands)
            asearch = abbrev_table_resolve;
         else if (nLastOne >= 0)
            asearch = abbrev_table_normal;

         for ( ; asearch ; asearch = asearch->next) {
            if (!strcmp(asearch->key, GLOB_user_input)) {
               char linebuff[INPUT_TEXTLINE_SIZE+1];
               if (process_accel_or_abbrev(asearch->value, linebuff)) {
		  // Erase the edit box.
		  gtk_entry_set_text(GTK_ENTRY(SDG("main_entry")), "");
                  WaitingForCommand = false;
                  return;
               }

               return;   // Couldn't be processed.  Stop.  No other abbreviations will match.
            }
         }
      }

      matches = match_user_input(nLastOne, false, false, false);
      user_match = GLOB_match;

      /* We forbid a match consisting of two or more "direct parse" concepts, such as
         "grand cross".  Direct parse concepts may only be stacked if they are followed
         by a call.  The "match.next" field indicates that direct parse concepts
         were stacked. */

      if ((matches == 1 || matches - GLOB_yielding_matches == 1 || user_match.exact) &&
          ((!user_match.match.packed_next_conc_or_subcall &&
            !user_match.match.packed_secondary_subcall) ||
           user_match.match.kind == ui_call_select ||
           user_match.match.kind == ui_concept_select)) {

         // The matcher found an acceptable (and possibly quite complex)
         // utterance.  Use it directly.

	 gtk_entry_set_text(GTK_ENTRY(SDG("main_entry")), "");  // Erase the edit box.
         WaitingForCommand = false;
         return;
      }

      // The matcher isn't happy.  If we got here because the user typed <enter>,
      // that's not acceptable.  Just ignore it.  Unless, of course, the type-in
      // buffer was empty and the user scrolled around, in which case the user
      // clearly meant to accept the currently highlighted item.

      if (is_enter_pressed && (GLOB_user_input[0] != '\0' || !menu_moved))
	 return;

      // Or if, for some reason, the menu isn't anywhere, we don't accept it.

      if (!is_selection) return;

      // But if the user clicked on "accept", or did an acceptable single- or
      // double-click of a menu item, that item is clearly what she wants, so
      // we use it.

      gtk_tree_model_get(main_list, &menuIter,
			 1, &(user_match.match.index),
			 2, &(user_match.match.kind), -1);

      use_computed_match();
}
static void use_computed_match(void) {

      user_match.match.packed_next_conc_or_subcall = 0;
      user_match.match.packed_secondary_subcall = 0;
      user_match.match.call_conc_options = null_options;
      user_match.real_next_subcall = (match_result *) 0;
      user_match.real_secondary_subcall = (match_result *) 0;

      gtk_entry_set_text(GTK_ENTRY(SDG("main_entry")), "");  // Erase the edit box.

      /* We have the needed info.  Process it and exit from the command loop.
         However, it's not a fully filled in match item from the parser.
         So we need to concoct a low-class match item. */

      if (nLastOne == match_number) {
      }
      else if (nLastOne == match_circcer) {
         user_match.match.call_conc_options.circcer =
            user_match.match.index+1;
      }
      else if (nLastOne >= match_taggers &&
               nLastOne < match_taggers+NUM_TAGGER_CLASSES) {
         user_match.match.call_conc_options.tagger =
            ((nLastOne-match_taggers) << 5)+user_match.match.index+1;
      }
      else {
         if (user_match.match.kind == ui_concept_select) {
            user_match.match.concept_ptr =
               &concept_descriptor_table[user_match.match.index];
         }
         else if (user_match.match.kind == ui_call_select) {
            user_match.match.call_ptr =
               main_call_lists[parse_state.call_list_to_use][user_match.match.index];
         }
      }

      WaitingForCommand = false;
}
static void do_menu(int id) {
      int i;
      if (nLastOne == match_startup_commands) {
         for (i=0 ; startup_menu[i].startup_name ; i++) {
            if (id == startup_menu[i].resource_id) {
               user_match.match.index = i;
               user_match.match.kind = ui_start_select;
	       use_computed_match();
	       return;
            }
         }
      }
      else if (nLastOne >= 0) {
         for (i=0 ; command_menu[i].command_name ; i++) {
            if (id == command_menu[i].resource_id) {
               user_match.match.index = i;
               user_match.match.kind = ui_command_select;
	       use_computed_match();
	       return;
            }
         }
      }
      else
	 // do nothing.
	  return;
}
#define MENU(handler, command_index) \
void handler(GtkMenuItem *menuitem, gpointer user_data) { \
   do_menu(command_index); \
}
MENU(on_choose_font_for_printing_activate, ID_FILE_CHOOSE_FONT);
MENU(on_file_print_activate, ID_FILE_PRINTTHIS);
MENU(on_file_print_any_activate, ID_FILE_PRINTFILE);
MENU(on_file_quit_activate, ID_FILE_EXIT);
MENU(on_edit_cut_activate, ID_COMMAND_CUT_TEXT);
MENU(on_edit_copy_activate, ID_COMMAND_COPY_TEXT);
//MENU(on_edit_clear_activate, ID_COMMAND_CLEAR_TEXT);
MENU(on_cut_one_call_activate, ID_COMMAND_CLIPBOARD_CUT);
MENU(on_paste_one_call_activate, ID_COMMAND_CLIPBOARD_PASTE_ONE);
MENU(on_paste_all_calls_activate, ID_COMMAND_CLIPBOARD_PASTE_ALL);
MENU(on_delete_one_call_from_clipboard_activate, ID_COMMAND_CLIPBOARD_DEL_ONE);
MENU(on_delete_all_calls_from_clipboard_activate,ID_COMMAND_CLIPBOARD_DEL_ALL);
MENU(on_undo_last_call_activate, ID_COMMAND_UNDO);
MENU(on_abort_this_sequence_activate, ID_COMMAND_ABORTTHISSEQUENCE);
MENU(on_end_this_sequence_activate, ID_COMMAND_ENDTHISSEQUENCE);
MENU(on_write_this_sequence_activate, ID_COMMAND_ENDTHISSEQUENCE);
MENU(on_insert_a_comment_activate, ID_COMMAND_COMMENT);
MENU(on_toggle_concept_levels_activate, ID_COMMAND_TOGGLE_CONC);
MENU(on_toggle_active_phantoms_activate, ID_COMMAND_TOGGLE_PHAN);
MENU(on_discard_entered_concepts_activate, ID_COMMAND_DISCARD_CONCEPT);
MENU(on_change_output_file_activate, ID_COMMAND_CH_OUTFILE);
MENU(on_change_title_activate, ID_COMMAND_CH_TITLE);
MENU(on_keep_picture_activate, ID_COMMAND_KEEP_PICTURE);
MENU(on_resolve_activate, ID_COMMAND_RESOLVE);
MENU(on_normalize_activate, ID_COMMAND_NORMALIZE);
MENU(on_reconcile_activate, ID_COMMAND_RECONCILE);
MENU(on_standardize_activate, ID_COMMAND_STANDARDIZE);
MENU(on_random_call_activate, ID_COMMAND_PICK_RANDOM);
MENU(on_simple_call_activate, ID_COMMAND_PICK_SIMPLE);
MENU(on_concept_call_activate, ID_COMMAND_PICK_CONCEPT);
MENU(on_level_call_activate, ID_COMMAND_PICK_LEVEL);
MENU(on_8_person_level_call_activate, ID_COMMAND_PICK_8P_LEVEL);
MENU(on_waves_activate, ID_COMMAND_CREATE_WAVES);
MENU(on_2_faced_lines_activate, ID_COMMAND_CREATE_2FL);
MENU(on_lines_in_activate, ID_COMMAND_CREATE_LINESIN);
MENU(on_lines_out_activate, ID_COMMAND_CREATE_LINESOUT);
MENU(on_inverted_lines_activate, ID_COMMAND_CREATE_INVLINES);
MENU(on_3_and_1_lines_activate, ID_COMMAND_CREATE_3N1LINES);
MENU(on_any_lines_activate, ID_COMMAND_CREATE_ANYLINES);
MENU(on_columns_activate, ID_COMMAND_CREATE_COLUMNS);
MENU(on_magic_columns_activate, ID_COMMAND_CREATE_MAGCOL);
MENU(on_dpt_activate, ID_COMMAND_CREATE_DPT);
MENU(on_cdpt_activate, ID_COMMAND_CREATE_CDPT);
MENU(on_8_chain_activate, ID_COMMAND_CREATE_8CH);
MENU(on_trade_by_activate, ID_COMMAND_CREATE_TRBY);
MENU(on_any_columns_activate, ID_COMMAND_CREATE_ANYCOLS);
MENU(on_tidal_wave_activate, ID_COMMAND_CREATE_GWV);
MENU(on_any_tidal_setup_activate, ID_COMMAND_CREATE_ANY_TIDAL);
MENU(on_diamonds_activate, ID_COMMAND_CREATE_DMD);
MENU(on_qtag_activate, ID_COMMAND_CREATE_QTAG);
MENU(on__3qtag_activate, ID_COMMAND_CREATE_3QTAG);
MENU(on_qline_activate, ID_COMMAND_CREATE_QLINE);
MENU(on__3qline_activate, ID_COMMAND_CREATE_3QLINE);
MENU(on_any_qtag_activate, ID_COMMAND_CREATE_ANY_QTAG);
MENU(on_help_manual_activate, ID_HELP_SDHELP);
MENU(on_help_faq_activate, ID_HELP_FAQ);
/// 1578

////////// 1815

gboolean
on_app_main_delete_event(GtkWidget *widget, GdkEvent *event,
			 gpointer user_data) {
      // We get here if the user presses alt-F4 and we haven't bound it to anything,
      // or if the user selects "exit" from the "file" menu.

      if (MenuKind != ui_start_select && gg->do_abort_popup() != POPUP_ACCEPT)
         return TRUE;  // Queried user; user said no; so we don't shut down.

      // Close journal and session files; call general_final_exit,
      // which sends WM_USER+2 and shuts us down for real.

      general_final_exit(0);

      return FALSE; // never actually reached.
}

static void setup_session_menu(void)
{
   GtkTreeView *tv;
   GtkTooltipsData *td;
   GtkListStore *startup_list;
   char line[MAX_FILENAME_LENGTH];
   GtkTreeIter iter;
   int i=0;
   
   // Set caption and tool-tip.
   tv = GTK_TREE_VIEW(SDG("startup_list"));
   gtk_label_set_markup(GTK_LABEL(SDG("startup_caption")),
			"<b>Choose a session:</b>");
   td = gtk_tooltips_data_get(GTK_WIDGET(tv));
   gtk_tooltips_set_tip
      (td->tooltips, td->widget,
       "Double-click a session to choose it and start Sd.\n"
       "Double-click \"(no session)\" if you don't want to run under "
       "any session at this time.\n"
       "Double-click \"(create a new session)\" if you want to add a "
       "new session to the list.\n"
       "You will be asked about the particulars for that new session.",
       NULL);
   
   // create and set tree model and view.
   startup_list = gtk_list_store_new
      (4, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
      
   while (get_next_session_line(line)) {
      char filename[strlen(line)+1], session_level[strlen(line)+1];
      char sequence_number[10] = { '\0' };
      int num_fields, txt_start, item_no, seq_no;
      num_fields = sscanf(line, "%d %n%s %s %d", &item_no, &txt_start,
			  filename, session_level, &seq_no);
      if (num_fields < 4) {
	 strncpy(filename, line+txt_start, sizeof(filename));
	 session_level[0]=0;
	 sequence_number[0]=0;
      } else
	 snprintf(sequence_number, sizeof(sequence_number), "%d", seq_no);
      gtk_list_store_insert_with_values
	 (startup_list, &iter, G_MAXINT,
	  0, i++, 1, filename, 2, session_level, 3, sequence_number, -1);
   }
   gtk_tree_view_set_model(tv, GTK_TREE_MODEL(startup_list));
   gtk_tree_view_column_set_visible(gtk_tree_view_get_column(tv, 0), TRUE);
   gtk_tree_view_column_set_visible(gtk_tree_view_get_column(tv, 2), TRUE);
   gtk_tree_view_set_headers_visible(tv, TRUE);
   gtk_tree_view_columns_autosize(tv);
}
static void setup_level_menu(void)
{
   GtkTreeView *tv;
   GtkTooltipsData *td;
   GtkListStore *startup_list;
   GtkTreeIter iter;
   int lev, i=0;

   // Set caption and tool-tip.
   tv = GTK_TREE_VIEW(SDG("startup_list"));
   gtk_label_set_markup(GTK_LABEL(SDG("startup_caption")),
			"<b>Choose a level:</b>");
   td = gtk_tooltips_data_get(GTK_WIDGET(tv));
   gtk_tooltips_set_tip
      (td->tooltips, td->widget,
       "Double-click a level to choose it and start Sd.", NULL);

   // create and set tree model and view.
   startup_list = gtk_list_store_new
      (4, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

   for (lev=(int)l_mainstream ; ; lev++) {
      Cstring this_string = getout_strings[lev];
      if (!this_string[0]) break;
      gtk_list_store_insert_with_values(startup_list, &iter, G_MAXINT,
					0, i++, 2, this_string, -1);
   }
   gtk_tree_view_column_set_visible(gtk_tree_view_get_column(tv, 0), FALSE);
   gtk_tree_view_column_set_visible(gtk_tree_view_get_column(tv, 2), FALSE);
   gtk_tree_view_set_model(tv, GTK_TREE_MODEL(startup_list));
   gtk_tree_view_set_headers_visible(tv, FALSE);
   gtk_tree_view_columns_autosize(tv);
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
   g_snprintf(main_title, sizeof(main_title), "Sd %s", s);
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
	    setup_level_menu();
	    dialog_menu_type = dialog_level;
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
		  g_snprintf(abridge_filename, sizeof(abridge_filename), "%s",
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
	       g_snprintf(abridge_filename, sizeof(abridge_filename), "%s",
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
   GtkTreeSelection *ts;
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

   ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(SDG("startup_list")));
   gtk_tree_selection_set_mode(ts, GTK_SELECTION_BROWSE);

   // If user specified session number on the command line, we must
   // just be looking for a level.  So skip the session part.

   if (ui_options.force_session == -1000000 && !get_first_session_line()) {
      setup_session_menu();
      dialog_menu_type = dialog_session;
   }
   else if (calling_level == l_nonexistent_concept) {
      setup_level_menu();
      dialog_menu_type = dialog_level;
   }
   else {
      gtk_label_set_markup(GTK_LABEL(SDG("startup_caption")), "");
      dialog_menu_type = dialog_none;
   }
   gtk_tree_selection_unselect_all(ts);
   gtk_widget_grab_focus(SDG("startup_list"));
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
	 startup_init();
	 gtk_widget_show(window_startup);
	 do {
	    result = gtk_dialog_run(GTK_DIALOG(window_startup));
	 } while (result==GTK_RESPONSE_OK && !startup_accept());
	 gtk_widget_hide(window_startup);
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
      g_snprintf(outfile_string, sizeof(outfile_string), "%s",
		 filename_strings[calling_level]);
      break;

   case init_database1:
      // The level has been chosen.  We are about to open the database.
      // Put up the main window.

      gtk_window_set_default_size(GTK_WINDOW(window_main),
				  window_size_args[2], window_size_args[3]);
      gtk_key_snooper_install(on_key_snoop, NULL); // intercept some keypresses
      // xxx put calls in menu and text in transcript so that auto-size
      // happens correctly?
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

typedef enum {
    SPACE_QTR, SPACE_HALF, SPACE_3QTR, SPACE_FULL, PHANTOM, DANCER_START
} special_img_t;
static GdkPixbuf *icons[8/*dancers*/*4/*directions*/+4/*spaces*/+1/*phantom*/];

GdkPixbuf *make_svg_icon(const gchar *xml_template, ...) {
   RsvgHandle *handle;
   GdkPixbuf *result;
   GError *err;
   va_list ap;
   gchar *buf;
   int n;
   va_start(ap, xml_template);
   n = g_vasprintf(&buf, xml_template, ap);
   va_end(ap);
   handle = rsvg_handle_new();
   if (rsvg_handle_write(handle, (guchar*)buf, n, &err) &&
       rsvg_handle_close(handle, &err)) {
      result = rsvg_handle_get_pixbuf(handle);
      rsvg_handle_free(handle);
      return result;
   }
   g_error("%s", err->message);
   return NULL; // unreachable.
}	   

void iofull::final_initialize()
{
   char *bg="ffffff", *fg="000000";
   char *icon_color[8] = { // The internal color scheme is:
      "112233", // 0 - not used
      "808000", // 1 - substitute yellow
      "ff0000", // 2 - red
      "00ff00", // 3 - green
      "ffff00", // 4 - yellow
      "0000ff", // 5 - blue
      "ff00ff", // 6 - magenta
      "00ffff", // 7 - cyan
   };
   unsigned i, j;
   ui_options.use_escapes_for_drawing_people =
      (ui_options.no_graphics==0) ? 2 : 0;

   // Install the pointy triangles.

   if (ui_options.no_graphics < 2)
      ui_options.direc = "?\020?\021????\036?\037?????";

   // Build appropriate checker images.
   //   build empty space pixbufs
   icons[SPACE_FULL] = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
				      BMP_PERSON_SIZE, BMP_PERSON_SIZE);
   gdk_pixbuf_fill(icons[SPACE_FULL], ui_options.reverse_video ?
		   0x00000000 /* transparent black */ :
		   0xFFFFFF00 /* transparent white */);
   for(i=0; i<3; i++) // create 1/4, 1/2, 3/4 spaces
      icons[i] = gdk_pixbuf_new_subpixbuf
	 (icons[SPACE_FULL], 0, 0, (i+1)*BMP_PERSON_SIZE/4, BMP_PERSON_SIZE);
   //    create correct color translation table.
   if (ui_options.no_intensify)
      bg = "808080";
   if (ui_options.reverse_video) {
      char *tmp = fg; fg=bg; bg=tmp;
   }
   if (ui_options.color_scheme == no_color)
      for (i=0; i<8; i++)
	 icon_color[i] = fg;
   //    make phantom icon
   icons[PHANTOM] = make_svg_icon(sd_phantom_svg, fg);
   //    make dancer icons.
   for (i=0; i<8; i++)
      for (j=0; j<4; j++)
	 icons[DANCER_START+i+(8*j)] = make_svg_icon
	    ( (i&1) ? sd_girl_svg : sd_boy_svg,
	      90*j, icon_color[color_index_list[i]], fg, fg, 90*j, 1+(i/2));

   // Initialize the display window linked list.
   gtk_text_buffer_set_text(main_buffer, "", -1);
   // Setup text buffer foreground/background colors.
   if (ui_options.reverse_video) {
       GdkColor c;
       gdk_color_parse("white", &c);
       gtk_widget_modify_text(SDG("main_transcript"), GTK_STATE_NORMAL, &c);
       gdk_color_parse("black", &c);
       gtk_widget_modify_base(SDG("main_transcript"), GTK_STATE_NORMAL, &c);
   }
}



// Process GTK messages
void EnterMessageLoop()
{
   gboolean is_quit = false;
   user_match.valid = FALSE;
   erase_matcher_input();
   WaitingForCommand = true;

   while (WaitingForCommand && !is_quit)
      is_quit = !gtk_main_iteration();

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
   gtk_widget_grab_focus(SDG("main_entry"));
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
   g_snprintf(buffer, sizeof(buffer),
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
      g_snprintf(myPrompt, sizeof(myPrompt),
		 "Current title is \"%s\".", header_comment);
   else
      myPrompt[0] = 0;

   return do_general_text_popup(myPrompt, "Enter new title:", "", dest);
}


popup_return iofull::do_getout_popup (char dest[])
{
   char buffer[MAX_TEXT_LINE_LENGTH+MAX_FILENAME_LENGTH];

   if (header_comment[0]) {
      g_snprintf(buffer, sizeof(buffer),
		 "Session title is \"%s\".", header_comment);
      return
         do_general_text_popup(buffer,
                               "You can give an additional comment for just this sequence:",
                               "",
                               dest);
   }
   else {
      g_snprintf(buffer, sizeof(buffer),
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
   char buf[3*strlen(the_line)+2], *cp;
   bool in_picture, squeeze_newline;
   GtkTextIter iter;
   erase_questionable_stuff();
   // decode the 'drawing_picture' argument.
   in_picture = drawing_picture & 1;
   squeeze_newline = drawing_picture & 2;
   // translate the dos-437 triangles to utf-8-encoded unicode triangles.
   // see http://www.cl.cam.ac.uk/~mgk25/unicode.html#utf-8 for utf-8 info
   for (cp=buf; *the_line; the_line++)
      switch((ui_options.no_graphics<2) ? (*the_line) : '\0') {
      case '\020': // right, unicode 25B6
	 *cp++ = 0xE2; *cp++ = 0x96; *cp++ = 0xB6; break;
      case '\021': // left,  unicode 25C0
	 *cp++ = 0xE2; *cp++ = 0x97; *cp++ = 0x80; break;
      case '\036': // up,    unicode 25B2
	 *cp++ = 0xE2; *cp++ = 0x96; *cp++ = 0xB2; break;
      case '\037': // down,  unicode 25Bc
	 *cp++ = 0xE2; *cp++ = 0x96; *cp++ = 0xBC; break;
      default:
	 *cp++ = *the_line; break;
      }
   *cp++='\n'; // add a newline.
   *cp=0;    // end the string.

   // XXX we currently ignore 'squeeze newline'

   gtk_text_buffer_get_end_iter(main_buffer, &iter);
   if (!in_picture)
      gtk_text_buffer_insert(main_buffer, &iter, buf, -1);
   else if (ui_options.no_graphics != 0)
      gtk_text_buffer_insert_with_tags_by_name
	 (main_buffer, &iter, buf, -1, "picture", NULL);
   else for (cp=buf; *cp; cp++) { // use fancy checkers.
      int personidx, persondir;
      GdkPixbuf *p=NULL;
      switch(*cp) {
      case '\014': /* a phantom! */
	 p = icons[PHANTOM]; break;
      case '\013': /* a dancer! */
	 personidx = (*++cp) & 7;
	 persondir = 3-((*++cp) & 0x3);
	 p = icons[DANCER_START+personidx+8*persondir];
	 break;
      case '6': /* one person-width */
	 p = icons[SPACE_FULL]; break;
      case '5': /* half a person-width */
      case '8': /* ditto; for checkers (one space if ASCII) */
	 p = icons[SPACE_HALF]; break;
      case '9': /* three-quarters a person-width */
	 p = icons[SPACE_3QTR]; break;
	 break;
      case ' ': /* two blanks is the inter-person spacing */
	 continue; // we currently squeeze people tightly together.
      default: /* this should be '\n' */
	 /* (maybe do something different for squeeze_newline?) */
	 gtk_text_buffer_insert(main_buffer, &iter, cp, 1);
	 break;
      }
      if (p)
	 gtk_text_buffer_insert_pixbuf(main_buffer, &iter, p);
      gtk_text_buffer_get_end_iter(main_buffer, &iter); // reset iterator
   }
}



void iofull::reduce_line_count(int n)
{
   GtkTextIter iter1, iter2;
   gtk_text_buffer_get_iter_at_line(main_buffer, &iter1, n);
   gtk_text_buffer_get_end_iter(main_buffer, &iter2);
   gtk_text_buffer_delete(main_buffer, &iter1, &iter2);
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
   gint result;
   gtk_widget_show(window_font);
   result = gtk_dialog_run(GTK_DIALOG(window_font));
   gtk_widget_hide(window_font);
   //XXX do something with this font.
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
      g_snprintf(msg, sizeof(msg), "%s: %s", s1, s2);
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

      // this is for real
      gtk_widget_destroy(window_main);
      window_main=NULL;
   }

   if (ico_pixbuf)
      g_object_unref(ico_pixbuf);
   // XXX free the bitmap?

   exit(code);
}
/// 3210
