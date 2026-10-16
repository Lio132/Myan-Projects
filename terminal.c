#include <gtk/gtk.h>
#include <vte/vte.h>
#include <stdlib.h>

static void on_child_exited(VteTerminal *terminal,
                            gint status,
                            gpointer user_data)
{
    (void)terminal;

    GtkWindow *window = GTK_WINDOW(user_data);

    char *title = g_strdup_printf(
        "MeinTerminal - msh beendet (Status: %d)",
        status
    );

    gtk_window_set_title(window, title);
    g_free(title);
}

static void on_spawn_complete(VteTerminal *terminal,
                              GPid child_pid,
                              GError *error,
                              gpointer user_data)
{
    (void)child_pid;

    GtkWindow *window = GTK_WINDOW(user_data);

    if (error != NULL)
    {
        GtkWidget *dialog = gtk_message_dialog_new(
            window,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "msh konnte nicht gestartet werden:\n%s",
            error->message
        );

        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_error_free(error);

        gtk_window_set_title(window,
                             "MeinTerminal - Startfehler");
    }
    else
    {
        gtk_window_set_title(window,
                             "MeinTerminal - msh läuft");
    }

    (void)terminal;
}

static void activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    GtkWidget *window = gtk_application_window_new(app);

    gtk_window_set_title(GTK_WINDOW(window), "MeinTerminal");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);

    VteTerminal *terminal =
        VTE_TERMINAL(vte_terminal_new());

    PangoFontDescription *font =
        pango_font_description_from_string("Monospace 12");

    vte_terminal_set_font(terminal, font);
    pango_font_description_free(font);

    gtk_container_add(
        GTK_CONTAINER(window),
        GTK_WIDGET(terminal)
    );

    /* Projektpfad: ~/myan-projects/myanShell */
    char *project_dir = g_build_filename(
        g_get_home_dir(),
        "myan-projects",
        "myanShell",
        NULL
    );

    char *shell_path = g_build_filename(
        project_dir,
        "msh",
        NULL
    );

    char *argv[] = { shell_path, NULL };

    g_signal_connect(
        terminal,
        "child-exited",
        G_CALLBACK(on_child_exited),
        window
    );

    gtk_widget_show_all(window);

    vte_terminal_spawn_async(
        terminal,
        VTE_PTY_DEFAULT,
        project_dir,
        argv,
        NULL,
        G_SPAWN_DEFAULT,
        NULL,
        NULL,
        NULL,
        -1,
        NULL,
        on_spawn_complete,
        window
    );

    g_free(shell_path);
    g_free(project_dir);
}

int main(int argc, char **argv)
{
    GtkApplication *app = gtk_application_new(
        "de.example.meinterminal",
        G_APPLICATION_DEFAULT_FLAGS
    );

    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int status = g_application_run(
        G_APPLICATION(app),
        argc,
        argv
    );

    g_object_unref(app);

    return status;
}
