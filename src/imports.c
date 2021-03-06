#include <gtk/gtk.h>
#include <gcrypt.h>
#include <jansson.h>
#include "db-misc.h"
#include "imports.h"
#include "password-cb.h"
#include "message-dialogs.h"
#include "gquarks.h"
#include "common.h"


static gboolean  parse_data_and_update_db    (GtkWidget     *main_window,
                                              const gchar   *filename,
                                              const gchar   *action_name,
                                              DatabaseData  *db_data,
                                              GtkListStore  *list_store);

static void      free_gslist                 (GSList        *otps,
                                              guint          list_len);


void
select_file_cb (GSimpleAction *simple,
                GVariant      *parameter __attribute__((unused)),
                gpointer       user_data)
{
    const gchar *action_name = g_action_get_name (G_ACTION (simple));
    ImportData *import_data = (ImportData *)user_data;
    GtkListStore *list_store = g_object_get_data (G_OBJECT (import_data->main_window), "lstore");

    GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open File",
                                                     GTK_WINDOW (import_data->main_window),
                                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                                     "Cancel", GTK_RESPONSE_CANCEL,
                                                     "Open", GTK_RESPONSE_ACCEPT,
                                                     NULL);

    gint res = gtk_dialog_run (GTK_DIALOG (dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
        gchar *filename = gtk_file_chooser_get_filename (chooser);
        parse_data_and_update_db (import_data->main_window, filename, action_name, import_data->db_data, list_store);
        g_free (filename);
    }

    gtk_widget_destroy (dialog);
}


static gboolean
parse_data_and_update_db (GtkWidget     *main_window,
                          const gchar   *filename,
                          const gchar   *action_name,
                          DatabaseData  *db_data,
                          GtkListStore  *list_store)
{
    GError *err = NULL;
    GSList *content = NULL;
    gchar *pwd = prompt_for_password (main_window, TRUE, NULL);
    if (pwd == NULL) {
        return FALSE;
    }

    if (g_strcmp0 (action_name, ANDOTP_IMPORT_ACTION_NAME) == 0) {
        content = get_andotp_data (filename, pwd, db_data->max_file_size_from_memlock, &err);
    } else {
        content = get_authplus_data (filename, pwd, db_data->max_file_size_from_memlock, &err);
    }

    if (content == NULL && err != NULL) {
        gchar *msg = g_strconcat ("An error occurred while importing, so nothing has been added to the database.\n"
                                          "The error is: ", err->message, NULL);
        show_message_dialog (main_window, msg, GTK_MESSAGE_ERROR);
        g_free (msg);
        return FALSE;
    }

    json_t *obj;
    guint list_len = g_slist_length (content);
    for (guint i = 0; i < list_len; i++) {
        otp_t *otp = g_slist_nth_data (content, i);
        obj = build_json_obj (otp->type, otp->label, otp->issuer, otp->secret, otp->digits, otp->algo, otp->counter);
        guint hash = json_object_get_hash (obj);
        if (g_slist_find_custom (db_data->objects_hash, GUINT_TO_POINTER (hash), check_duplicate) == NULL) {
            db_data->objects_hash = g_slist_append (db_data->objects_hash, g_memdup (&hash, sizeof (guint)));
            db_data->data_to_add = g_slist_append (db_data->data_to_add, obj);
        } else {
            g_print ("[INFO] Duplicate element not added\n");
        }
    }

    update_and_reload_db (db_data, list_store, TRUE, &err);
    if (err != NULL && !g_error_matches (err, missing_file_gquark (), MISSING_FILE_CODE)) {
        show_message_dialog (main_window, err->message, GTK_MESSAGE_ERROR);
    }

    gcry_free (pwd);
    free_gslist (content, list_len);

    return TRUE;
}


static void
free_gslist (GSList *otps,
             guint   list_len)
{
    otp_t *otp_data;
    for (guint i = 0; i < list_len; i++) {
        otp_data = g_slist_nth_data (otps, i);
        g_free (otp_data->type);
        g_free (otp_data->algo);
        g_free (otp_data->label);
        g_free (otp_data->issuer);
        gcry_free (otp_data->secret);
    }

    g_slist_free_full (otps, g_free);
}