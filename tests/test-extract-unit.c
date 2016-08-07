#include <gnome-autoar/autoar.h>
#include <gio/gio.h>


typedef void (*FileScannedCallback) (GFile *scanned_file,
                                     GFileInfo *scanned_file_info,
                                     gpointer user_data);

typedef struct {
  GFile *in;
  GFile *out;
  GFile *ref;

  GHashTable *unmatched_files;
} ExtractTest;

typedef struct {
  GCancellable *cancellable;

  guint number_of_files;

  GFile *suggested_destination;
  GFile *destination_to_suggest;

  guint64 completed_size;
  guint completed_files;

  GHashTable *conflict_files;
  GHashTable *conflict_files_actions;
  GHashTable *conflict_files_destinations;

  GError *error;

  gboolean cancelled_signalled;
  gboolean completed_signalled;
} ExtractTestData;


#define TESTS_DIR_NAME "tests"
#define EXTRACT_TESTS_DIR_NAME "files/extract"

GFile *extract_tests_dir;

AutoarPref *arpref;


static gboolean
remove_dir (GFile *dir)
{
  gboolean success = TRUE;
  GError *error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;

  enumerator = g_file_enumerate_children (dir,
                                          G_FILE_ATTRIBUTE_STANDARD_NAME,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL, NULL);

  if (enumerator) {
    GFileInfo *info;

    while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
      g_autoptr(GFile) child;

      child = g_file_get_child (dir, g_file_info_get_name (info));

      if (!g_file_delete (child, NULL, &error)) {
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY)) {
          success = success && remove_dir (child);
        } else {
          success = FALSE;
        }

        g_clear_error (&error);
      }

      g_object_unref (info);
    }
  }

  g_file_delete (dir, NULL, &error);

  if (error) {
    success = FALSE;
    g_error_free (error);
  }

  return success;
}

static ExtractTest*
extract_test_new (const char *test_name)
{
  ExtractTest *extract_test;
  g_autoptr (GFile) workdir = NULL;
  GFile *in;
  GFile *out;
  GFile *ref;

  workdir = g_file_get_child (extract_tests_dir, test_name);
  if (g_file_query_file_type (workdir, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY) {
    g_printerr ("%s: workdir does not exist", test_name);

    return NULL;
  }

  in = g_file_get_child (workdir, "in");
  ref = g_file_get_child (workdir, "ref");

  if (g_file_query_file_type (in, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY ||
      g_file_query_file_type (ref, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY) {
    g_printerr ("%s: input or output directory does not exist\n", test_name);

    g_object_unref (in);

    return NULL;
  }

  out = g_file_get_child (workdir, "out");

  remove_dir (out);

  g_file_make_directory_with_parents (out, NULL, NULL);

  extract_test = g_new0 (ExtractTest, 1);

  extract_test->in = in;
  extract_test->ref = ref;
  extract_test->out = out;

  extract_test->unmatched_files = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         g_free,
                                                         g_object_unref);

  return extract_test;
}

static void
extract_test_free (ExtractTest *extract_test)
{
  g_object_unref (extract_test->in);
  g_object_unref (extract_test->ref);
  g_object_unref (extract_test->out);

  g_hash_table_destroy (extract_test->unmatched_files);

  g_free (extract_test);
}

static void
scanned_handler (AutoarExtract *arextract,
                 guint files,
                 gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->number_of_files = files;
}

static GFile*
decide_destination_handler (AutoarExtract *arextract,
                            GFile *dest,
                            GList *files,
                            gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->suggested_destination = g_object_ref (dest);

  if (data->destination_to_suggest != NULL) {
    g_object_ref (data->destination_to_suggest);
  }

  return data->destination_to_suggest;
}

static void
progress_handler (AutoarExtract *arextract,
                  guint64 completed_size,
                  guint completed_files,
                  gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->completed_size = completed_size;
  data->completed_files = completed_files;
}

static AutoarConflictAction
conflict_handler (AutoarExtract *arextract,
                  GFile *file,
                  GFile **new_file,
                  gpointer user_data)
{
  ExtractTestData *data = user_data;
  AutoarConflictAction action = AUTOAR_CONFLICT_OVERWRITE;
  gpointer value;
  gboolean key_found;

  g_hash_table_add (data->conflict_files, g_object_ref (file));

  key_found = g_hash_table_lookup_extended (data->conflict_files_actions,
                                            file,
                                            NULL,
                                            &value);

  if (key_found) {
    action = GPOINTER_TO_UINT (value);

    switch (action) {
      case AUTOAR_CONFLICT_OVERWRITE:
        break;
      case AUTOAR_CONFLICT_CHANGE_DESTINATION:
        *new_file = g_object_ref (g_hash_table_lookup (data->conflict_files_destinations,
                                                       file));
        break;
      case AUTOAR_CONFLICT_SKIP:
        break;
    }
  }

  return action;
}

static void
error_handler (AutoarExtract *arextract,
               GError *error,
               gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->error = g_error_copy (error);
}

static void
completed_handler (AutoarExtract *arextract,
                   gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->completed_signalled = TRUE;
}

static void
cancelled_handler (AutoarExtract *arextract,
                   gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->cancelled_signalled = TRUE;
}

static ExtractTestData*
extract_test_data_new_for_extract (AutoarExtract *arextract)
{
  ExtractTestData *data;

  data = g_new0 (ExtractTestData, 1);

  data->cancellable = g_cancellable_new ();

  g_signal_connect (arextract, "scanned",
                    G_CALLBACK (scanned_handler), data);
  g_signal_connect (arextract, "decide-destination",
                    G_CALLBACK (decide_destination_handler), data);
  g_signal_connect (arextract, "progress",
                    G_CALLBACK (progress_handler), data);
  g_signal_connect (arextract, "conflict",
                    G_CALLBACK (conflict_handler), data);
  g_signal_connect (arextract, "completed",
                    G_CALLBACK (completed_handler), data);
  g_signal_connect (arextract, "error",
                    G_CALLBACK (error_handler), data);
  g_signal_connect (arextract, "cancelled",
                    G_CALLBACK (cancelled_handler), data);

  data->conflict_files = g_hash_table_new_full (g_file_hash,
                                                (GEqualFunc) g_file_equal,
                                                g_object_unref,
                                                NULL);
  data->conflict_files_actions = g_hash_table_new_full (g_file_hash,
                                                        (GEqualFunc) g_file_equal,
                                                        g_object_unref,
                                                        NULL);
  data->conflict_files_destinations = g_hash_table_new_full (g_file_hash,
                                                             (GEqualFunc) g_file_equal,
                                                             g_object_unref,
                                                             g_object_unref);

  return data;
}

static void
extract_test_data_free (ExtractTestData *data)
{
  g_object_unref (data->cancellable);

  if (data->suggested_destination) {
    g_object_unref (data->suggested_destination);
  }

  if (data->destination_to_suggest) {
    g_object_unref (data->destination_to_suggest);
  }

  if (data->error) {
    g_error_free (data->error);
  }

  g_hash_table_destroy (data->conflict_files);
  g_hash_table_destroy (data->conflict_files_actions);
  g_hash_table_destroy (data->conflict_files_destinations);

  g_free (data);
}

static gboolean
setup_extract_tests_dir (const char *executable_path)
{
  g_autoptr (GFile) tests_dir = NULL;
  gboolean found;

  tests_dir = g_file_new_for_commandline_arg (executable_path);

  found = FALSE;
  while (!found && g_file_has_parent (tests_dir, NULL)) {
    GFile *parent;
    gchar *parent_basename;

    parent = g_file_get_parent (tests_dir);
    parent_basename = g_file_get_basename (parent);

    if (g_strcmp0 (parent_basename, TESTS_DIR_NAME) == 0) {
      found = TRUE;
    }

    g_object_unref (tests_dir);
    g_free (parent_basename);

    tests_dir = parent;
  }

  if (!found) {
    g_printerr ("Tests directory not in executable path\n");
    return FALSE;
  }

  extract_tests_dir = g_file_get_child (tests_dir, EXTRACT_TESTS_DIR_NAME);

  if (!g_file_query_exists (extract_tests_dir, NULL)) {
    g_printerr ("Extract tests directory does not exist in tests directory\n");
    return FALSE;
  }

  return TRUE;
}

/* Asserts that all files in @included are also in @including */
static void
scan_directory (GFile *directory,
                FileScannedCallback scanned_callback,
                gpointer callback_data)
{
  GQueue *files;
  GQueue *file_infos;
  GFileEnumerator *enumerator;

  files = g_queue_new ();
  file_infos = g_queue_new ();

  g_queue_push_tail (files, g_object_ref (directory));
  g_queue_push_tail (file_infos,
                     g_file_query_info (directory,
                                        G_FILE_ATTRIBUTE_STANDARD_NAME","
                                        G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                        NULL, NULL));

  while (!g_queue_is_empty (files)) {
    g_autoptr (GFile) file;
    g_autoptr (GFileInfo) file_info;

    file = g_queue_pop_tail (files);
    file_info = g_queue_pop_tail (file_infos);

    if (scanned_callback) {
      scanned_callback (file, file_info, callback_data);
    }

    if (g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY) {
      enumerator = g_file_enumerate_children (file,
                                              G_FILE_ATTRIBUTE_STANDARD_NAME","
                                              G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                              G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                              NULL, NULL);

      if (enumerator) {
        GFile *child;
        GFileInfo *child_info;

        child_info = g_file_enumerator_next_file (enumerator, NULL, NULL);
        while (child_info != NULL) {
          child = g_file_get_child (file, g_file_info_get_name (child_info));

          g_queue_push_tail (files, child);
          g_queue_push_tail (file_infos, child_info);

          child_info = g_file_enumerator_next_file (enumerator, NULL, NULL);
        }

        g_object_unref (enumerator);
      }
    }
  }

  g_queue_free_full (files, g_object_unref);
  g_queue_free_full (file_infos, g_object_unref);
}

static void
output_file_scanned (GFile *scanned_file,
                     GFileInfo *scanned_file_info,
                     gpointer user_data)
{
  ExtractTest *extract_test = user_data;
  char *relative_path;

  relative_path = scanned_file == extract_test->out ?
                                  g_strdup ("") :
                                  g_file_get_relative_path (extract_test->out,
                                                            scanned_file);

  g_hash_table_insert (extract_test->unmatched_files, relative_path,
                       g_object_ref (scanned_file_info));
}

static void
reference_file_scanned (GFile *scanned_file,
                        GFileInfo *scanned_file_info,
                        gpointer user_data)
{
  ExtractTest *extract_test = user_data;
  g_autofree char *relative_path;
  GFileInfo *corresponding_file_info;

  relative_path = scanned_file == extract_test->ref ?
                                  g_strdup ("") :
                                  g_file_get_relative_path (extract_test->ref,
                                                            scanned_file);

  corresponding_file_info = g_hash_table_lookup (extract_test->unmatched_files,
                                                 relative_path);

  g_assert_nonnull (corresponding_file_info);
  if (corresponding_file_info != NULL) {
    g_assert_cmpuint (g_file_info_get_file_type (scanned_file_info),
                      ==,
                      g_file_info_get_file_type (corresponding_file_info));

    g_hash_table_remove (extract_test->unmatched_files, relative_path);
  }
}

/* Asserts that the output and reference directory match */
static void
assert_ref_and_output_match (ExtractTest *extract_test)
{
  scan_directory (extract_test->out, output_file_scanned, extract_test);
  scan_directory (extract_test->ref, reference_file_scanned, extract_test);
  g_assert_cmpuint (g_hash_table_size (extract_test->unmatched_files), ==, 0);
}

static void
test_one_file_same_name (void)
{
  /* arextract.zip
   * └── arextract.txt
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * └── arextract.txt
   *
   * 0 directories, 1 file
   */

  ExtractTest *extract_test;
  ExtractTestData *data;
  g_autoptr (GFile) archive = NULL;
  AutoarExtract *arextract;

  extract_test = extract_test_new ("test-one-file-same-name");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->in, "arextract.zip");

  arextract = autoar_extract_new (archive, extract_test->out, arpref);

  data = extract_test_data_new_for_extract (arextract);

  autoar_extract_start (arextract, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_ref_and_output_match (extract_test);

  extract_test_free (extract_test);
  extract_test_data_free (data);
  g_object_unref (arextract);
}

static void
test_one_file_different_name (void)
{
  /* arextract.zip
   * └── arextractdifferent.txt
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * └── arextract
   *     └── arextractdifferent.txt
   *
   * 1 directory, 1 file
   */

  ExtractTest *extract_test;
  ExtractTestData *data;
  g_autoptr (GFile) archive = NULL;
  AutoarExtract *arextract;

  extract_test = extract_test_new ("test-one-file-different-name");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->in, "arextract.zip");

  arextract = autoar_extract_new (archive, extract_test->out, arpref);

  data = extract_test_data_new_for_extract (arextract);

  autoar_extract_start (arextract, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_ref_and_output_match (extract_test);

  extract_test_free (extract_test);
  extract_test_data_free (data);
  g_object_unref (arextract);
}

static void
test_multiple_files_same_name (void)
{
  /* arextract.zip
   * └── arextract
   *     ├── arextract_nested
   *     │   └── arextract.txt
   *     └── arextract.txt
   *
   * 2 directories, 2 files
   *
   *
   * ref
   * └── arextract
   *     ├── arextract_nested
   *     │   └── arextract.txt
   *     └── arextract.txt
   *
   * 2 directories, 2 files
   */

  ExtractTest *extract_test;
  ExtractTestData *data;
  g_autoptr (GFile) archive = NULL;
  AutoarExtract *arextract;

  extract_test = extract_test_new ("test-multiple-files-same-name");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->in, "arextract.zip");

  arextract = autoar_extract_new (archive, extract_test->out, arpref);

  data = extract_test_data_new_for_extract (arextract);

  autoar_extract_start (arextract, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 4);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_ref_and_output_match (extract_test);

  extract_test_free (extract_test);
  extract_test_data_free (data);
  g_object_unref (arextract);
}

static void
test_multiple_files_different_name (void)
{
  /* arextract.zip
   * ├── arextract
   * │   ├── arextract_nested
   * │   │   └── arextract.txt
   * │   └── arextract.txt
   * └── arextract.txt
   *
   * 2 directories, 3 files
   *
   *
   * ref
   * └── arextract
   *     ├── arextract
   *     │   ├── arextract_nested
   *     │   │   └── arextract.txt
   *     │   └── arextract.txt
   *     └── arextract.txt
   *
   * 3 directories, 3 files
   */

  ExtractTest *extract_test;
  ExtractTestData *data;
  g_autoptr (GFile) archive = NULL;
  AutoarExtract *arextract;

  extract_test = extract_test_new ("test-multiple-files-different-name");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->in, "arextract.zip");

  arextract = autoar_extract_new (archive, extract_test->out, arpref);

  data = extract_test_data_new_for_extract (arextract);

  autoar_extract_start (arextract, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 5);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_ref_and_output_match (extract_test);

  extract_test_free (extract_test);
  extract_test_data_free (data);
  g_object_unref (arextract);
}

static void
test_one_file_conflict_overwrite (void)
{
  /* arextract.zip
   * └── arextract.txt
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * └── arextract.txt
   *
   * 0 directories, 1 file
   */

  ExtractTest *extract_test;
  ExtractTestData *data;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_file = NULL;
  g_autoptr (GFile) reference_file = NULL;
  AutoarExtract *arextract;

  extract_test = extract_test_new ("test-one-file-conflict-overwrite");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  reference_file = g_file_get_child (extract_test->ref,
                                     "arextract.txt");
  conflict_file = g_file_get_child (extract_test->out,
                                    "arextract.txt");

  g_file_copy (reference_file, conflict_file, G_FILE_COPY_NONE,
               NULL, NULL, NULL, NULL);

  archive = g_file_get_child (extract_test->in, "arextract.zip");

  arextract = autoar_extract_new (archive, extract_test->out, arpref);

  data = extract_test_data_new_for_extract (arextract);

  autoar_extract_start (arextract, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_file));
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_ref_and_output_match (extract_test);

  extract_test_free (extract_test);
  extract_test_data_free (data);
  g_object_unref (arextract);
}

static void
test_one_file_conflict_new_destination (void)
{
  /* arextract.zip
   * └── arextract.txt
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * ├── arextract_new.txt
   * └── arextract.txt
   *
   * 0 directories, 2 files
   */

  ExtractTest *extract_test;
  ExtractTestData *data;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_file = NULL;
  g_autoptr (GFile) reference_file = NULL;
  AutoarExtract *arextract;

  extract_test = extract_test_new ("test-one-file-conflict-new-destination");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  reference_file = g_file_get_child (extract_test->ref,
                                     "arextract.txt");
  conflict_file = g_file_get_child (extract_test->out,
                                    "arextract.txt");

  g_file_copy (reference_file, conflict_file, G_FILE_COPY_NONE,
               NULL, NULL, NULL, NULL);

  archive = g_file_get_child (extract_test->in, "arextract.zip");

  arextract = autoar_extract_new (archive, extract_test->out, arpref);

  data = extract_test_data_new_for_extract (arextract);

  g_hash_table_insert (data->conflict_files_actions,
                       g_object_ref (conflict_file),
                       GUINT_TO_POINTER (AUTOAR_CONFLICT_CHANGE_DESTINATION));

  g_hash_table_insert (data->conflict_files_destinations,
                       g_object_ref (conflict_file),
                       g_file_get_child (extract_test->out,
                                         "arextract_new.txt"));

  autoar_extract_start (arextract, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_file));
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_ref_and_output_match (extract_test);

  extract_test_free (extract_test);
  extract_test_data_free (data);
  g_object_unref (arextract);
}

static void
test_one_file_conflict_skip_file (void)
{
  /* arextract.zip
   * └── arextract.txt
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * └── arextract.txt
   *
   * 0 directories, 1 file
   */

  ExtractTest *extract_test;
  ExtractTestData *data;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_file = NULL;
  g_autoptr (GFile) reference_file = NULL;
  AutoarExtract *arextract;

  extract_test = extract_test_new ("test-one-file-conflict-skip-file");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  reference_file = g_file_get_child (extract_test->ref,
                                     "arextract.txt");
  conflict_file = g_file_get_child (extract_test->out,
                                    "arextract.txt");

  g_file_copy (reference_file, conflict_file, G_FILE_COPY_NONE,
               NULL, NULL, NULL, NULL);

  archive = g_file_get_child (extract_test->in, "arextract.zip");

  arextract = autoar_extract_new (archive, extract_test->out, arpref);

  data = extract_test_data_new_for_extract (arextract);

  g_hash_table_insert (data->conflict_files_actions,
                       g_object_ref (conflict_file),
                       GUINT_TO_POINTER (AUTOAR_CONFLICT_SKIP));

  autoar_extract_start (arextract, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_file));
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_ref_and_output_match (extract_test);

  extract_test_free (extract_test);
  extract_test_data_free (data);
  g_object_unref (arextract);
}

static void
test_one_file_error_file_over_directory (void)
{
  /* The dummy file in this test is not relevant to the test itself, but it
   * was required in order to add the directory to the .git repo
   *
   * arextract.zip
   * └── arextract
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * └── arextract
   *     └── dummy
   *
   * 1 directory, 1 files
   */

  ExtractTest *extract_test;
  ExtractTestData *data;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_directory = NULL;
  g_autoptr (GFile) dummy_file = NULL;
  g_autoptr (GFileOutputStream) out = NULL;
  AutoarExtract *arextract;

  extract_test = extract_test_new ("test-one-file-error-file-over-directory");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  conflict_directory = g_file_get_child (extract_test->out,
                                         "arextract");
  dummy_file = g_file_get_child (conflict_directory, "dummy");

  g_file_make_directory (conflict_directory, NULL, NULL);

  out = g_file_create (dummy_file, G_FILE_CREATE_NONE, NULL, NULL);
  g_output_stream_close (G_OUTPUT_STREAM (out), NULL, NULL);

  archive = g_file_get_child (extract_test->in, "arextract.zip");

  arextract = autoar_extract_new (archive, extract_test->out, arpref);

  data = extract_test_data_new_for_extract (arextract);

  autoar_extract_start (arextract, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_directory));
  g_assert_error (data->error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY);
  g_assert_false (data->completed_signalled);
  assert_ref_and_output_match (extract_test);

  extract_test_free (extract_test);
  extract_test_data_free (data);
  g_object_unref (arextract);
}



static void
test_change_extract_destination (void)
{
  /* arextract.zip
   * └── arextract
   *     ├── arextract_nested
   *     │   └── arextract.txt
   *     └── arextract.txt
   * 
   * 2 directories, 2 files
   *
   *
   * ref
   * └── new_destination
   *     ├── arextract_nested
   *     │   └── arextract.txt
   *     └── arextract.txt
   *
   * 2 directories, 2 files
   */

  ExtractTest *extract_test;
  ExtractTestData *data;
  g_autoptr (GFile) archive = NULL;
  AutoarExtract *arextract;

  extract_test = extract_test_new ("test-change-extract-destination");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->in, "arextract.zip");

  arextract = autoar_extract_new (archive, extract_test->out, arpref);

  data = extract_test_data_new_for_extract (arextract);
  data->destination_to_suggest = g_file_get_child (extract_test->out,
                                                   "new_destination");

  autoar_extract_start (arextract, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 4);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_ref_and_output_match (extract_test);

  extract_test_free (extract_test);
  extract_test_data_free (data);
  g_object_unref (arextract);
}

static void
setup_test_suite (void)
{
  g_test_add_func ("/autoar-extract/test-one-file-same-name",
                   test_one_file_same_name);
  g_test_add_func ("/autoar-extract/test-one-file-different-name",
                   test_one_file_different_name);
  g_test_add_func ("/autoar-extract/test-multiple-files-same-name",
                   test_multiple_files_same_name);
  g_test_add_func ("/autoar-extract/test-multiple-files-different-name",
                   test_multiple_files_different_name);

  g_test_add_func ("/autoar-extract/test-one-file-conflict-overwrite",
                   test_one_file_conflict_overwrite);
  g_test_add_func ("/autoar-extract/test-one-file-conflict-new-destination",
                   test_one_file_conflict_new_destination);
  g_test_add_func ("/autoar-extract/test-one-file-conflict-skip-file",
                   test_one_file_conflict_skip_file);

  g_test_add_func ("/autoar-extract/test-one-file-error-file-over-directory",
                   test_one_file_error_file_over_directory);

  g_test_add_func ("/autoar-extract/test-change-extract-destination",
                   test_change_extract_destination);
}

int
main (int argc,
      char *argv[])
{
  int tests_result;
  g_autoptr (GSettings) settings = NULL;

  if (!setup_extract_tests_dir (argv[0])) {
    return -1;
  }

  settings = g_settings_new (AUTOAR_PREF_DEFAULT_GSCHEMA_ID);
  arpref = autoar_pref_new_with_gsettings (settings);
  autoar_pref_set_delete_if_succeed (arpref, FALSE);

  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  setup_test_suite ();

  tests_result = g_test_run ();

  g_object_unref (arpref);
  g_object_unref (extract_tests_dir);

  return tests_result;
}
