#include <gnome-autoar/gnome-autoar.h>
#include <gio/gio.h>


typedef void (*FileScannedCallback) (GFile *scanned_file,
                                     GFileInfo *scanned_file_info,
                                     gpointer user_data);

typedef struct {
  GFile *input;
  GFile *output;
  GFile *reference;

  GHashTable *unmatched_files;
} ExtractTest;

static void extract_test_free (ExtractTest *test);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ExtractTest, extract_test_free);

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
  gboolean request_passphrase_signalled;
} ExtractTestData;

static void extract_test_data_free (ExtractTestData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ExtractTestData, extract_test_data_free);


#define TESTS_DIR_NAME "tests"
#define EXTRACT_TESTS_DIR_NAME "files/extract"

GFile *extract_tests_dir;


static gboolean
remove_directory (GFile *directory)
{
  gboolean success = TRUE;
  GError *error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;

  enumerator = g_file_enumerate_children (directory,
                                          G_FILE_ATTRIBUTE_STANDARD_NAME,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL, NULL);

  if (enumerator) {
    GFileInfo *info;

    while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
      g_autoptr (GFile) child = NULL;

      child = g_file_get_child (directory, g_file_info_get_name (info));

      if (!g_file_delete (child, NULL, &error)) {
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY)) {
          success = success && remove_directory (child);
        } else {
          success = FALSE;
        }

        g_clear_error (&error);
      }

      g_object_unref (info);
    }
  }

  g_file_delete (directory, NULL, &error);

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
  g_autoptr (GFile) work_directory = NULL;
  GFile *input;
  GFile *output;
  GFile *reference;

  work_directory = g_file_get_child (extract_tests_dir, test_name);
  if (g_file_query_file_type (work_directory, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY) {
    g_printerr ("%s: work directory does not exist", test_name);

    return NULL;
  }

  input = g_file_get_child (work_directory, "input");
  reference = g_file_get_child (work_directory, "reference");

  if (g_file_query_file_type (input, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY) {
    g_printerr ("%s: input directory does not exist\n", test_name);

    g_object_unref (input);

    return NULL;
  }

  if (!g_file_query_exists (reference, NULL))
    g_message ("%s: reference directory does not exist\n", test_name);

  output = g_file_get_child (work_directory, "output");

  remove_directory (output);

  g_file_make_directory_with_parents (output, NULL, NULL);

  extract_test = g_new0 (ExtractTest, 1);

  extract_test->input = input;
  extract_test->reference = reference;
  extract_test->output = output;

  extract_test->unmatched_files = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         g_free,
                                                         g_object_unref);

  return extract_test;
}

static void
extract_test_free (ExtractTest *extract_test)
{
  g_object_unref (extract_test->input);
  g_object_unref (extract_test->reference);
  g_object_unref (extract_test->output);

  g_hash_table_destroy (extract_test->unmatched_files);

  g_free (extract_test);
}

static void
scanned_handler (AutoarExtractor *extractor,
                 guint files,
                 gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->number_of_files = files;
}

static GFile*
decide_destination_handler (AutoarExtractor *extractor,
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
progress_handler (AutoarExtractor *extractor,
                  guint64 completed_size,
                  guint completed_files,
                  gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->completed_size = completed_size;
  data->completed_files = completed_files;
}

static AutoarConflictAction
conflict_handler (AutoarExtractor *extractor,
                  GFile *file,
                  GFile **new_file,
                  gpointer user_data)
{
  ExtractTestData *data = user_data;
  AutoarConflictAction action = AUTOAR_CONFLICT_UNHANDLED;
  gpointer value;
  gboolean key_found;

  g_hash_table_add (data->conflict_files, g_object_ref (file));

  key_found = g_hash_table_lookup_extended (data->conflict_files_actions,
                                            file,
                                            NULL,
                                            &value);

  if (key_found) {
    action = GPOINTER_TO_UINT (value);
    if (action == AUTOAR_CONFLICT_CHANGE_DESTINATION) {
      *new_file = g_object_ref (g_hash_table_lookup (data->conflict_files_destinations,
                                                     file));
    }
  }

  return action;
}

static void
error_handler (AutoarExtractor *extractor,
               GError *error,
               gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->error = g_error_copy (error);
}

static void
completed_handler (AutoarExtractor *extractor,
                   gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->completed_signalled = TRUE;
}

static void
cancelled_handler (AutoarExtractor *extractor,
                   gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->cancelled_signalled = TRUE;
}

static gchar*
request_passphrase_handler (AutoarExtractor *extractor,
                            gpointer user_data)
{
  ExtractTestData *data = user_data;

  data->request_passphrase_signalled = TRUE;

  return NULL;
}

static ExtractTestData*
extract_test_data_new_for_extract (AutoarExtractor *extractor)
{
  ExtractTestData *data;

  data = g_new0 (ExtractTestData, 1);

  data->cancellable = g_cancellable_new ();

  g_signal_connect (extractor, "scanned",
                    G_CALLBACK (scanned_handler), data);
  g_signal_connect (extractor, "decide-destination",
                    G_CALLBACK (decide_destination_handler), data);
  g_signal_connect (extractor, "progress",
                    G_CALLBACK (progress_handler), data);
  g_signal_connect (extractor, "conflict",
                    G_CALLBACK (conflict_handler), data);
  g_signal_connect (extractor, "completed",
                    G_CALLBACK (completed_handler), data);
  g_signal_connect (extractor, "error",
                    G_CALLBACK (error_handler), data);
  g_signal_connect (extractor, "cancelled",
                    G_CALLBACK (cancelled_handler), data);
  g_signal_connect (extractor, "request-passphrase",
                    G_CALLBACK (request_passphrase_handler), data);

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
    g_autoptr (GFile) file = NULL;
    g_autoptr (GFileInfo) file_info = NULL;

    file = g_queue_pop_tail (files);
    file_info = g_queue_pop_tail (file_infos);

    if (scanned_callback && file != directory) {
      scanned_callback (file, file_info, callback_data);
    }

    if (file_info && g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY) {
      enumerator = g_file_enumerate_children (file,
                                              G_FILE_ATTRIBUTE_STANDARD_NAME","
                                              G_FILE_ATTRIBUTE_STANDARD_TYPE","
                                              G_FILE_ATTRIBUTE_STANDARD_SIZE,
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

  relative_path = scanned_file == extract_test->output ?
                                  g_strdup ("") :
                                  g_file_get_relative_path (extract_test->output,
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
  g_autofree char *relative_path = NULL;
  GFileInfo *corresponding_file_info;

  relative_path = scanned_file == extract_test->reference ?
                                  g_strdup ("") :
                                  g_file_get_relative_path (extract_test->reference,
                                                            scanned_file);

  corresponding_file_info = g_hash_table_lookup (extract_test->unmatched_files,
                                                 relative_path);

  g_assert_nonnull (corresponding_file_info);
  if (corresponding_file_info != NULL) {
    g_assert_cmpuint (g_file_info_get_file_type (scanned_file_info),
                      ==,
                      g_file_info_get_file_type (corresponding_file_info));
    if (g_file_info_get_file_type (scanned_file_info) != G_FILE_TYPE_DIRECTORY) {
      g_assert_cmpuint (g_file_info_get_size (scanned_file_info),
                        ==,
                        g_file_info_get_size (corresponding_file_info));
    }
    g_hash_table_remove (extract_test->unmatched_files, relative_path);
  }
}

/* Asserts that the output and reference directory match */
static void
assert_reference_and_output_match (ExtractTest *extract_test)
{
  scan_directory (extract_test->output, output_file_scanned, extract_test);
  scan_directory (extract_test->reference, reference_file_scanned, extract_test);
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-one-file-same-name");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-one-file-different-name");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

/* Be sure that extra folder is not created in case of output-is-dest. */
static void
test_one_file_different_name_output_is_dest (void)
{
  /* arextract.zip
   * └── arextractdifferent.txt
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * └── arextractdifferent.txt
   *
   * 0 directory, 1 file
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-one-file-different-name-output-is-dest");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);
  autoar_extractor_set_output_is_dest (extractor, TRUE);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-multiple-files-same-name");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 4);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-multiple-files-different-name");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 5);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

static void
test_raw_named (void)
{
  /* arextract.gz
   * └── arextractdifferent
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * └── arextractdifferent
   *
   * 0 directories, 1 file
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-raw-named");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.gz");

  extractor = autoar_extractor_new (archive, extract_test->output);
  autoar_extractor_set_output_is_dest (extractor, TRUE);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

static void
test_raw_unnamed (void)
{
  /* arextract.gz
   * └── arextract
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * └── arextract
   *
   * 0 directories, 1 file
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-raw-unnamed");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.gz");

  extractor = autoar_extractor_new (archive, extract_test->output);
  autoar_extractor_set_output_is_dest (extractor, TRUE);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

static void
test_conflict_overwrite (void)
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_file = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-conflict-overwrite");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  conflict_file = g_file_get_child (extract_test->output,
                                    "arextract.txt");

  g_assert_true (g_file_replace_contents (conflict_file,
                                          "this file should be overwritten", 31,
                                          NULL, FALSE, G_FILE_CREATE_NONE, NULL,
                                          NULL, NULL));

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  g_hash_table_insert (data->conflict_files_actions,
                       g_object_ref (conflict_file),
                       GUINT_TO_POINTER (AUTOAR_CONFLICT_OVERWRITE));

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_file));
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

/* Be sure that nonempty directories are not replaced to prevent data-loss. */
static void
test_conflict_overwrite_nonempty_directory (void)
{
  /* arextract.zip
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_directory = NULL;
  g_autoptr (GFile) dummy_file = NULL;
  g_autoptr (GFileOutputStream) out = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-conflict-overwrite-nonempty-directory");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  conflict_directory = g_file_get_child (extract_test->output,
                                         "arextract");
  dummy_file = g_file_get_child (conflict_directory, "dummy");

  g_file_make_directory (conflict_directory, NULL, NULL);

  out = g_file_create (dummy_file, G_FILE_CREATE_NONE, NULL, NULL);
  g_output_stream_close (G_OUTPUT_STREAM (out), NULL, NULL);

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  g_hash_table_insert (data->conflict_files_actions,
                       g_object_ref (conflict_directory),
                       GUINT_TO_POINTER (AUTOAR_CONFLICT_OVERWRITE));

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_directory));
  g_assert_error (data->error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY);
  g_assert_false (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

/* Be sure that symlink itself is replaced, not its target. */
static void
test_conflict_overwrite_symlink (void)
{
  /* arextract.tar
   * ├── arextract -> arectract.txt
   * └── arextract
   *
   * 0 directories, 2 files
   *
   *
   * ref
   * └── arextract
   *
   * 0 directories, 1 file
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_directory = NULL;
  g_autoptr (GFile) conflict_file = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-conflict-overwrite-symlink");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  conflict_file = g_file_get_child (extract_test->output, "arextract");
  archive = g_file_get_child (extract_test->input, "arextract.tar");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  g_hash_table_insert (data->conflict_files_actions,
                       g_object_ref (conflict_file),
                       GUINT_TO_POINTER (AUTOAR_CONFLICT_OVERWRITE));

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 2);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_file));
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

/* Be sure that hardlink itself is replaced, not its target. */
static void
test_conflict_overwrite_hardlink (void)
{
  /* arextract.tar
   * ├── arectract.txt
   * ├── arextract -> arectract.txt
   * └── arextract
   *
   * 0 directories, 3 files
   *
   *
   * ref
   * ├── arextract.txt
   * └── arextract
   *
   * 0 directories, 2 files
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_file = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-conflict-overwrite-hardlink");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  conflict_file = g_file_get_child (extract_test->output, "arextract");
  archive = g_file_get_child (extract_test->input, "arextract.tar");

  extractor = autoar_extractor_new (archive, extract_test->output);
  autoar_extractor_set_output_is_dest (extractor, TRUE);

  data = extract_test_data_new_for_extract (extractor);

  g_hash_table_insert (data->conflict_files_actions,
                       g_object_ref (conflict_file),
                       GUINT_TO_POINTER (AUTOAR_CONFLICT_OVERWRITE));

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 3);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_file));
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

static void
test_conflict_new_destination (void)
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_file = NULL;
  g_autoptr (GFile) reference_file = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-conflict-new-destination");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  reference_file = g_file_get_child (extract_test->reference,
                                     "arextract.txt");
  conflict_file = g_file_get_child (extract_test->output,
                                    "arextract.txt");

  g_assert_true (g_file_copy (reference_file, conflict_file, G_FILE_COPY_NONE,
                 NULL, NULL, NULL, NULL));

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  g_hash_table_insert (data->conflict_files_actions,
                       g_object_ref (conflict_file),
                       GUINT_TO_POINTER (AUTOAR_CONFLICT_CHANGE_DESTINATION));

  g_hash_table_insert (data->conflict_files_destinations,
                       g_object_ref (conflict_file),
                       g_file_get_child (extract_test->output,
                                         "arextract_new.txt"));

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_file));
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

static void
test_conflict_skip_file (void)
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_file = NULL;
  g_autoptr (GFile) reference_file = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-conflict-skip-file");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  reference_file = g_file_get_child (extract_test->reference,
                                     "arextract.txt");
  conflict_file = g_file_get_child (extract_test->output,
                                    "arextract.txt");

  g_assert_true (g_file_copy (reference_file, conflict_file, G_FILE_COPY_NONE,
                 NULL, NULL, NULL, NULL));

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  g_hash_table_insert (data->conflict_files_actions,
                       g_object_ref (conflict_file),
                       GUINT_TO_POINTER (AUTOAR_CONFLICT_SKIP));

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_file));
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

/* Be sure that the default action is skip to prevent data-loss. */
static void
test_conflict_skip_file_default (void)
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) conflict_file = NULL;
  g_autoptr (GFile) reference_file = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-conflict-skip-file-default");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  reference_file = g_file_get_child (extract_test->reference,
                                     "arextract.txt");
  conflict_file = g_file_get_child (extract_test->output,
                                    "arextract.txt");

  g_assert_true (g_file_copy (reference_file, conflict_file, G_FILE_COPY_NONE,
                 NULL, NULL, NULL, NULL));

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  g_hash_table_insert (data->conflict_files_actions,
                       g_object_ref (conflict_file),
                       GUINT_TO_POINTER (AUTOAR_CONFLICT_UNHANDLED));

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_true (g_hash_table_contains (data->conflict_files,
                                        conflict_file));
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
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

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-change-extract-destination");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);
  data->destination_to_suggest = g_file_get_child (extract_test->output,
                                                   "new_destination");

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 4);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

/* Be sure that the files with symlinks in parents are refused completely for
 * security reasons.
 *
 * If symlinks in parents will be allowed in the future, then the following
 * cases need to be tested to be sure that nothing is written outside of the
 * destination:
 * 1) link -> .. ; link/arextract.txt
 * 2) link -> /tmp ; link/arextract.txt
 * 3) current -> . ; link -> current/.. ; link/arextract.txt
 * 4) tmplink -> /tmp ; link -> tmplink/.. ; link/arextract.txt
 */
static void
test_symlink_parent (void)
{
  /* arextract.tar
   * ├── arextract -> /tmp
   * └── arextract/arextract.txt
   *
   * 0 directories, 2 files
   *
   *
   * ref
   * └── arextract -> /tmp
   *
   * 0 directories, 1 file
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-symlink-parent");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.tar");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 2);
  g_assert_error (data->error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);
  g_assert_false (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

/* Be sure that file with the ".." parent is written in the destination. */
static void
test_sanitize_dotdot_parent (void)
{
  /* arextract.tar
   * └── ./../arextract.txt
   *
   * 0 directories, 1 file
   *
   *
   * ref
   * └── arextract.txt
   *
   * 0 directories, 1 file
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-sanitize-dotdot-parent");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.tar");

  extractor = autoar_extractor_new (archive, extract_test->output);
  autoar_extractor_set_output_is_dest (extractor, TRUE);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

/* Be sure that the absolute paths are relative to the destination. */
static void
test_sanitize_absolute_path (void)
{
  /* arextract.tar
   * ├── /
   * ├── /arextract.txt
   * └── /arextract/arextract.txt
   *
   * 1 directories, 2 file
   *
   *
   * ref
   * ├── arextract.txt
   * └── arextract/arextract.txt
   *
   * 1 directories, 2 file
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-sanitize-absolute-path");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.tar");

  extractor = autoar_extractor_new (archive, extract_test->output);
  autoar_extractor_set_output_is_dest (extractor, TRUE);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 3);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

/* Be sure that extraction of children from a readonly directory doesn't fail. */
static void
test_readonly_directory (void)
{
  /* arextract.tar
   * └── arextract
   *     └── arextract.txt
   *
   * 1 directories, 1 files
   *
   *
   * ref
   * └── arextract
   *     └── arextract.txt
   *
   * 1 directories, 1 files
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (GFile) readonly = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-readonly-directory");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.tar");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 2);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);

  /* Make the directory writable again to avoid issues when deleting. */
  readonly = g_file_get_child (extract_test->output, "arextract");
  g_file_set_attribute_uint32 (readonly, G_FILE_ATTRIBUTE_UNIX_MODE, 0755,
                               G_FILE_QUERY_INFO_NONE, NULL, NULL);
}

static void
test_encrypted (void)
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
   *
   * passphrase is password123
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-encrypted");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);
  autoar_extractor_set_passphrase (extractor, "password123");

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_no_error (data->error);
  g_assert_true (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

static void
test_encrypted_request_passphrase (void)
{
  /* arextract.zip
   * └── arextract.txt
   *
   * 0 directories, 1 file
   *
   *
   * ref
   *
   * 0 directories, 0 files
   *
   *
   * passphrase is password123
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-encrypted-request-passphrase");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_error (data->error, AUTOAR_EXTRACTOR_ERROR, AUTOAR_PASSPHRASE_REQUIRED_ERRNO);
  g_assert_true (data->request_passphrase_signalled);
  g_assert_false (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

static void
test_encrypted_wrong_passphrase (void)
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
   *
   *
   * passphrase is password123
   */

  g_autoptr (ExtractTest) extract_test = NULL;
  g_autoptr (ExtractTestData) data = NULL;
  g_autoptr (GFile) archive = NULL;
  g_autoptr (AutoarExtractor) extractor = NULL;

  extract_test = extract_test_new ("test-encrypted-wrong-passphrase");

  if (!extract_test) {
    g_assert_nonnull (extract_test);
    return;
  }

  archive = g_file_get_child (extract_test->input, "arextract.zip");

  extractor = autoar_extractor_new (archive, extract_test->output);
  autoar_extractor_set_passphrase (extractor, "wrong");

  data = extract_test_data_new_for_extract (extractor);

  autoar_extractor_start (extractor, data->cancellable);

  g_assert_cmpuint (data->number_of_files, ==, 1);
  g_assert_error (data->error, AUTOAR_LIBARCHIVE_ERROR, -1);
  g_assert_cmpstr (data->error->message, ==, "Incorrect passphrase");
  g_assert_false (data->completed_signalled);
  assert_reference_and_output_match (extract_test);
}

static void
setup_test_suite (void)
{
  g_test_add_func ("/autoar-extract/test-one-file-same-name",
                   test_one_file_same_name);
  g_test_add_func ("/autoar-extract/test-one-file-different-name",
                   test_one_file_different_name);
  g_test_add_func ("/autoar-extract/test-one-file-different-name-output-is-dest",
                   test_one_file_different_name_output_is_dest);
  g_test_add_func ("/autoar-extract/test-multiple-files-same-name",
                   test_multiple_files_same_name);
  g_test_add_func ("/autoar-extract/test-multiple-files-different-name",
                   test_multiple_files_different_name);

  g_test_add_func ("/autoar-extract/test-raw-named",
                   test_raw_named);
  g_test_add_func ("/autoar-extract/test-raw-unnamed",
                   test_raw_unnamed);

  g_test_add_func ("/autoar-extract/test-conflict-overwrite",
                   test_conflict_overwrite);
  g_test_add_func ("/autoar-extract/test-conflict-overwrite-nonempty-directory",
                   test_conflict_overwrite_nonempty_directory);
  g_test_add_func ("/autoar-extract/test-conflict-overwrite-symlink",
                   test_conflict_overwrite_symlink);
  g_test_add_func ("/autoar-extract/test-conflict-overwrite-hardlink",
                   test_conflict_overwrite_hardlink);
  g_test_add_func ("/autoar-extract/test-conflict-new-destination",
                   test_conflict_new_destination);
  g_test_add_func ("/autoar-extract/test-conflict-skip-file",
                   test_conflict_skip_file);
  g_test_add_func ("/autoar-extract/test-conflict-skip-file-default",
                   test_conflict_skip_file_default);

  g_test_add_func ("/autoar-extract/test-change-extract-destination",
                   test_change_extract_destination);

  g_test_add_func ("/autoar-extract/test-symlink-parent",
                   test_symlink_parent);
  g_test_add_func ("/autoar-extract/test-sanitize-dotdot-parent",
                   test_sanitize_dotdot_parent);
  g_test_add_func ("/autoar-extract/test-sanitize-absolute-path",
                   test_sanitize_absolute_path);

  g_test_add_func ("/autoar-extract/test-readonly-directory",
                   test_readonly_directory);

  g_test_add_func ("/autoar-extract/test-encrypted",
                   test_encrypted);
  g_test_add_func ("/autoar-extract/test-encrypted-request-passphrase",
                   test_encrypted_request_passphrase);
  g_test_add_func ("/autoar-extract/test-encrypted-wrong-passphrase",
                   test_encrypted_wrong_passphrase);
}

int
main (int argc,
      char *argv[])
{
  int tests_result;
  g_autofree gchar *extract_tests_dir_path = NULL;

  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  extract_tests_dir_path = g_test_build_filename (G_TEST_DIST,
                                                  TESTS_DIR_NAME,
                                                  EXTRACT_TESTS_DIR_NAME,
                                                  NULL);
  extract_tests_dir = g_file_new_for_path (extract_tests_dir_path);
  if (!g_file_query_exists (extract_tests_dir, NULL)) {
    g_printerr ("Extract tests directory not found\n");
    return -1;
  }

  setup_test_suite ();

  tests_result = g_test_run ();

  g_object_unref (extract_tests_dir);

  return tests_result;
}
