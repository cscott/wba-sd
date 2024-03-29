#include <gnome.h>


void
on_file_new_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_file_open_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_file_save_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_file_save_as_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_file_choose_font_activate           (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_file_print_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_file_print_any_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_file_quit_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_edit_cut_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_edit_copy_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_edit_paste_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_edit_clear_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_cut_one_call_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_paste_one_call_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_paste_all_calls_activate            (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_delete_one_call_from_clipboard_activate
                                        (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_delete_all_calls_from_clipboard_activate
                                        (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_undo_last_call_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_abort_this_sequence_activate        (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_end_this_sequence_activate          (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_write_this_sequence_activate        (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_insert_a_comment_activate           (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_toggle_concept_levels_activate      (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_toggle_active_phantoms_activate     (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_discard_entered_concepts_activate   (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_change_output_file_activate         (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_change_title_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_keep_picture_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_resolve_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_normalize_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_reconcile_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_standardize_activate                (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_random_call_activate                (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_simple_call_activate                (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_concept_call_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_level_call_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_8_person_level_call_activate        (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_waves_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_2_faced_lines_activate              (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_lines_in_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_lines_out_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_inverted_lines_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_3_and_1_lines_activate              (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_any_lines_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_columns_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_magic_columns_activate              (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_dpt_activate                        (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_cdpt_activate                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_8_chain_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_trade_by_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_any_columns_activate                (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_tidal_wave_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_any_tidal_setup_activate            (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_diamonds_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_qtag_activate                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on__3qtag_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_qline_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on__3qline_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_any_qtag_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_help_manual_activate                (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_help_faq_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_help_about_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_abridge_changed                     (GtkButton        *button,
                                        gpointer         user_data);

void
on_calldb_changed                      (GtkButton       *button,
                                        gpointer         user_data);

gboolean
on_startup_list_button_press_event     (GtkWidget       *widget,
                                        GdkEventButton  *event,
                                        gpointer         user_data);

void
on_startup_list_selection_changed(GtkTreeSelection *selection, gpointer data);

void
on_startup_output_select_clicked       (GtkButton       *button,
                                        gpointer         user_data);
void
on_main_entry_changed                  (GtkEditable     *editable,
                                        gpointer         user_data);

gboolean
on_main_cmds_button_press_event        (GtkWidget       *widget,
                                        GdkEventButton  *event,
                                        gpointer         user_data);

void
on_main_cmds_row_activated             (GtkTreeView     *treeview,
                                        GtkTreePath     *path,
                                        GtkTreeViewColumn *column,
                                        gpointer         user_data);
void
on_main_accept_clicked                 (GtkButton       *button,
                                        gpointer         user_data);

void
on_main_cancel_clicked                 (GtkButton       *button,
                                        gpointer         user_data);

gboolean
on_app_main_delete_event               (GtkWidget       *widget,
                                        GdkEvent        *event,
                                        gpointer         user_data);

void
on_outfile_use_clicked                 (GtkButton       *button,
                                        gpointer         user_data);
