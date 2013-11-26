/*
 * svn_test_main.c:  shared main() & friends for SVN test-suite programs
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#ifdef WIN32
#include <crtdbg.h>
#endif

#include <apr_pools.h>
#include <apr_general.h>
#include <apr_signal.h>
#include <apr_env.h>

#include "svn_cmdline.h"
#include "svn_opt.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_test.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_ctype.h"
#include "svn_utf.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_atomic.h"
#include "private/svn_mutex.h"

#include "svn_private_config.h"

#if APR_HAS_THREADS && APR_VERSION_AT_LEAST(1,3,0)
#  include <apr_thread_pool.h>
#  define HAVE_THREADPOOLS 1
#else
#  define HAVE_THREADPOOLS 0
#endif

/* Some Subversion test programs may want to parse options in the
   argument list, so we remember it here. */
int test_argc;
const char **test_argv;

/* Many tests write to disk. Instead of writing to the current
   directory, they should use this path as the root of the test data
   area. */
static const char *data_path;

/* Test option: Print more output */
static svn_boolean_t verbose_mode = FALSE;

/* Test option: Print only unexpected results */
static svn_boolean_t quiet_mode = FALSE;

/* Test option: Remove test directories after success */
static svn_boolean_t cleanup_mode = FALSE;

/* Test option: Allow segfaults */
static svn_boolean_t allow_segfaults = FALSE;

/* Test option: Limit testing to a given mode (i.e. XFail, Skip,
   Pass, All). */
enum svn_test_mode_t mode_filter = svn_test_all;

/* Test option: Allow concurrent execution of tests */
static svn_boolean_t parallel = FALSE;

/* Option parsing enums and structures */
enum {
  help_opt = SVN_OPT_FIRST_LONGOPT_ID,
  cleanup_opt,
  fstype_opt,
  list_opt,
  verbose_opt,
  quiet_opt,
  config_opt,
  server_minor_version_opt,
  allow_segfault_opt,
  srcdir_opt,
  mode_filter_opt,
  parallel_opt
};

static const apr_getopt_option_t cl_options[] =
{
  {"help",          help_opt, 0,
                    N_("display this help")},
  {"cleanup",       cleanup_opt, 0,
                    N_("remove test directories after success")},
  {"config-file",   config_opt, 1,
                    N_("specify test config file ARG")},
  {"fs-type",       fstype_opt, 1,
                    N_("specify a filesystem backend type ARG")},
  {"list",          list_opt, 0,
                    N_("lists all the tests with their short description")},
  {"mode-filter",   mode_filter_opt, 1,
                    N_("only run/list tests with expected mode ARG = PASS, "
                       "XFAIL, SKIP, or ALL (default)")},
  {"verbose",       verbose_opt, 0,
                    N_("print extra information")},
  {"server-minor-version", server_minor_version_opt, 1,
                    N_("set the minor version for the server ('3', '4', "
                       "'5', or '6')")},
  {"quiet",         quiet_opt, 0,
                    N_("print only unexpected results")},
  {"allow-segfaults", allow_segfault_opt, 0,
                    N_("don't trap seg faults (useful for debugging)")},
  {"srcdir",        srcdir_opt, 1,
                    N_("source directory")},
  {"parallel",      parallel_opt, 0,
                    N_("allow concurrent execution of tests")},
  {0,               0, 0, 0}
};


/* ================================================================= */
/* Stuff for cleanup processing */

/* When non-zero, don't remove test directories */
static svn_boolean_t skip_cleanup = FALSE;

/* All cleanup actions are registered as cleanups on this pool. */
static apr_pool_t *cleanup_pool = NULL;

/* Used by test_thread to serialize access to stdout. */
static svn_mutex__t *log_mutex = NULL;

static apr_status_t
cleanup_rmtree(void *data)
{
  if (!skip_cleanup)
    {
      apr_pool_t *pool = svn_pool_create(NULL);
      const char *path = data;

      /* Ignore errors here. */
      svn_error_t *err = svn_io_remove_dir2(path, FALSE, NULL, NULL, pool);
      svn_error_clear(err);
      if (verbose_mode)
        {
          if (err)
            printf("FAILED CLEANUP: %s\n", path);
          else
            printf("CLEANUP: %s\n", path);
        }
      svn_pool_destroy(pool);
    }
  return APR_SUCCESS;
}


void
svn_test_add_dir_cleanup(const char *path)
{
  if (cleanup_mode)
    {
      const char *abspath;
      svn_error_t *err;

      /* All cleanup functions use the *same* pool (not subpools of it).
         Thus, we need to synchronize. */
      err = svn_mutex__lock(log_mutex);
      if (err)
        {
          if (verbose_mode) 
            printf("FAILED svn_mutex__lock in svn_test_add_dir_cleanup.\n");
          svn_error_clear(err);
          return;
        }

      err = svn_path_get_absolute(&abspath, path, cleanup_pool);
      svn_error_clear(err);
      if (!err)
        apr_pool_cleanup_register(cleanup_pool, abspath, cleanup_rmtree,
                                  apr_pool_cleanup_null);
      else if (verbose_mode)
        printf("FAILED ABSPATH: %s\n", path);

      err = svn_mutex__unlock(log_mutex, NULL);
      if (err)
        {
          if (verbose_mode) 
            printf("FAILED svn_mutex__unlock in svn_test_add_dir_cleanup.\n");
          svn_error_clear(err);
        }
    }
}


/* ================================================================= */
/* Quite a few tests use random numbers. */

apr_uint32_t
svn_test_rand(apr_uint32_t *seed)
{
  *seed = (*seed * 1103515245UL + 12345UL) & 0xffffffffUL;
  return *seed;
}


/* ================================================================= */


/* Determine the array size of test_funcs[], the inelegant way.  :)  */
static int
get_array_size(void)
{
  int i;

  for (i = 1; test_funcs[i].func2 || test_funcs[i].func_opts; i++)
    {
    }

  return (i - 1);
}

/* Buffer used for setjmp/longjmp. */
static jmp_buf jump_buffer;

/* Our SIGSEGV handler, which jumps back into do_test_num(), which see for
   more information. */
static void
crash_handler(int signum)
{
  longjmp(jump_buffer, 1);
}

/* Write the result of test number TEST_NUM to stdout.  Pretty-print test
   name and dots according to our test-suite spec, and return TRUE if there
   has been a test failure.

   The parameters are basically the internal state of do_test_num() and
   test_thread(). */
/*  */
static svn_boolean_t
log_results(const char *progname,
            int test_num,
            svn_boolean_t msg_only,
            svn_boolean_t run_this_test,
            svn_boolean_t skip,
            svn_boolean_t xfail,
            svn_boolean_t wimp,
            svn_error_t *err,
            const char *msg,
            const struct svn_test_descriptor_t *desc)
{
  svn_boolean_t test_failed;

  if (err && err->apr_err == SVN_ERR_TEST_SKIPPED)
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      skip = TRUE;
    }

  /* Failure means unexpected results -- FAIL or XPASS. */
  test_failed = (!wimp && ((err != SVN_NO_ERROR) != (xfail != 0)));

  /* If we got an error, print it out.  */
  if (err)
    {
      svn_handle_error2(err, stdout, FALSE, "svn_tests: ");
      svn_error_clear(err);
    }

  if (msg_only)
    {
      if (run_this_test)
        printf(" %3d    %-5s  %s%s%s%s\n",
               test_num,
               (xfail ? "XFAIL" : (skip ? "SKIP" : "")),
               msg ? msg : "(test did not provide name)",
               (wimp && verbose_mode) ? " [[" : "",
               (wimp && verbose_mode) ? desc->wip : "",
               (wimp && verbose_mode) ? "]]" : "");
    }
  else if (run_this_test && ((! quiet_mode) || test_failed))
    {
      printf("%s %s %d: %s%s%s%s\n",
             (err
              ? (xfail ? "XFAIL:" : "FAIL: ")
              : (xfail ? "XPASS:" : (skip ? "SKIP: " : "PASS: "))),
             progname,
             test_num,
             msg ? msg : "(test did not provide name)",
             wimp ? " [[WIMP: " : "",
             wimp ? desc->wip : "",
             wimp ? "]]" : "");
    }

  if (msg)
    {
      size_t len = strlen(msg);
      if (len > 50)
        printf("WARNING: Test docstring exceeds 50 characters\n");
      if (msg[len - 1] == '.')
        printf("WARNING: Test docstring ends in a period (.)\n");
      if (svn_ctype_isupper(msg[0]))
        printf("WARNING: Test docstring is capitalized\n");
    }
  if (desc->msg == NULL)
    printf("WARNING: New-style test descriptor is missing a docstring.\n");

  fflush(stdout);

  return test_failed;
}

/* Execute a test number TEST_NUM.  Pretty-print test name and dots
   according to our test-suite spec, and return the result code.
   If HEADER_MSG and *HEADER_MSG are not NULL, print *HEADER_MSG prior
   to pretty-printing the test information, then set *HEADER_MSG to NULL. */
static svn_boolean_t
do_test_num(const char *progname,
            int test_num,
            svn_boolean_t msg_only,
            svn_test_opts_t *opts,
            const char **header_msg,
            apr_pool_t *pool)
{
  svn_boolean_t skip, xfail, wimp;
  svn_error_t *err = NULL;
  const char *msg = NULL;  /* the message this individual test prints out */
  const struct svn_test_descriptor_t *desc;
  const int array_size = get_array_size();
  svn_boolean_t run_this_test; /* This test's mode matches DESC->MODE. */

  /* Check our array bounds! */
  if (test_num < 0)
    test_num += array_size + 1;
  if ((test_num > array_size) || (test_num <= 0))
    {
      if (header_msg && *header_msg)
        printf("%s", *header_msg);
      printf("FAIL: %s: THERE IS NO TEST NUMBER %2d\n", progname, test_num);
      skip_cleanup = TRUE;
      return TRUE;  /* BAIL, this test number doesn't exist. */
    }

  desc = &test_funcs[test_num];
  skip = desc->mode == svn_test_skip;
  xfail = desc->mode == svn_test_xfail;
  wimp = xfail && desc->wip;
  msg = desc->msg;
  run_this_test = mode_filter == svn_test_all || mode_filter == desc->mode;

  if (run_this_test && header_msg && *header_msg)
    {
      printf("%s", *header_msg);
      *header_msg = NULL;
    }

  if (!allow_segfaults)
    {
      /* Catch a crashing test, so we don't interrupt the rest of 'em. */
      apr_signal(SIGSEGV, crash_handler);
    }

  /* We use setjmp/longjmp to recover from the crash.  setjmp() essentially
     establishes a rollback point, and longjmp() goes back to that point.
     When we invoke longjmp(), it instructs setjmp() to return non-zero,
     so we don't end up in an infinite loop.

     If we've got non-zero from setjmp(), we know we've crashed. */
  if (setjmp(jump_buffer) == 0)
    {
      /* Do test */
      if (msg_only || skip || !run_this_test)
        ; /* pass */
      else if (desc->func2)
        err = (*desc->func2)(pool);
      else
        err = (*desc->func_opts)(opts, pool);

      if (err && err->apr_err == SVN_ERR_TEST_SKIPPED)
        {
          svn_error_clear(err);
          err = SVN_NO_ERROR;
          skip = TRUE;
        }
    }
  else
    err = svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                           "Test crashed "
                           "(run in debugger with '--allow-segfaults')");

  if (!allow_segfaults)
    {
      /* Now back to your regularly scheduled program... */
      apr_signal(SIGSEGV, SIG_DFL);
    }

  /* Failure means unexpected results -- FAIL or XPASS. */
  skip_cleanup = log_results(progname, test_num, msg_only, run_this_test,
                             skip, xfail, wimp, err, msg, desc);

  return skip_cleanup;
}

#if HAVE_THREADPOOLS

/* Per-test parameters used by test_thread */
typedef struct test_params_t
{
  /* Name of the application */
  const char *progname;

  /* Number / index of the test to execute */
  int test_num;

  /* Global test options as provided by main() */
  svn_test_opts_t *opts;

  /* Thread-safe parent pool for the test-specific pool.  We expect the
     test thread to create a sub-pool and destroy it after test completion. */
  apr_pool_t *pool;

  /* Reference to the global failure flag.  Set this if any test failed. */
  svn_atomic_t *got_error;

  /* Reference to the global completed test counter. */
  svn_atomic_t *run_count;
} test_params_t;

/* Thread function similar to do_test_num() but with fewer options.  We do
   catch segfaults.  All parameters are given as a test_params_t in DATA.
 */
static void * APR_THREAD_FUNC
test_thread(apr_thread_t *tid, void *data)
{
  svn_boolean_t skip, xfail, wimp;
  svn_error_t *err = NULL;
  const struct svn_test_descriptor_t *desc;
  svn_boolean_t run_this_test; /* This test's mode matches DESC->MODE. */
  test_params_t *params = data;

  apr_pool_t *test_pool = svn_pool_create(params->pool);

  desc = &test_funcs[params->test_num];
  skip = desc->mode == svn_test_skip;
  xfail = desc->mode == svn_test_xfail;
  wimp = xfail && desc->wip;
  run_this_test = mode_filter == svn_test_all || mode_filter == desc->mode;

  /* Do test */
  if (skip || !run_this_test)
    ; /* pass */
  else if (desc->func2)
    err = (*desc->func2)(test_pool);
  else
    err = (*desc->func_opts)(params->opts, test_pool);

  /* Write results to console */
  svn_error_clear(svn_mutex__lock(log_mutex));
  if (log_results(params->progname, params->test_num, FALSE, run_this_test,
                  skip, xfail, wimp, err, desc->msg, desc))
    svn_atomic_set(params->got_error, TRUE);
  svn_error_clear(svn_mutex__unlock(log_mutex, NULL));

  /* release all test memory */
  svn_pool_destroy(test_pool);

  /* one more test completed */
  svn_atomic_inc(params->run_count);
    
  return NULL;
}

/* Execute all ARRAY_SIZE tests concurrently using MAX_THREADS threads.
   Pass PROGNAME and OPTS to the individual tests.  Return TRUE if at least
   one of the tests failed.  Allocate all data in POOL.

   Note that cleanups are delayed until all tests have been completed.
 */
static svn_boolean_t
do_tests_concurrently(const char *progname,
                      int array_size,
                      int max_threads,
                      svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  apr_thread_pool_t *threads;
  apr_status_t status;
  svn_atomic_t got_error = FALSE;
  int i;
  svn_atomic_t run_count = 0;

  /* Create the thread pool. */
  status = apr_thread_pool_create(&threads, max_threads, max_threads, pool);
  if (status)
    {
      printf("apr_thread_pool_create() failed.\n");
      return TRUE;
    }

  /* Don't queue requests unless we reached the worker thread limit. */
  apr_thread_pool_threshold_set(threads, 0);

  /* Generate one task per test and queue them in the thread pool. */
  for (i = 1; i <= array_size; i++)
    {
      test_params_t *params = apr_pcalloc(pool, sizeof(*params));
      params->got_error = &got_error;
      params->opts = opts;
      params->pool = pool;
      params->progname = progname;
      params->test_num = i;
      params->run_count = &run_count;

      apr_thread_pool_push(threads, test_thread, params, 0, NULL);
    }

  /* Wait for all tasks (tests) to complete.  As it turns out, this is the
     variant with the least run-time overhead to the test threads. */
  while (   apr_thread_pool_tasks_count(threads)
         || apr_thread_pool_busy_count(threads))
    apr_thread_yield();
  
  /* For some unknown reason, cleaning POOL (TEST_POOL in main()) does not
     call the following reliably for all users. */
  apr_thread_pool_destroy(threads);

  /* Verify that we didn't skip any tasks. */
  if (run_count != array_size)
    {
      printf("Parallel test failure: only %d of %d tests executed.\n",
             (int)run_count, array_size);
      return TRUE;
    }

  return got_error != FALSE;
}

#endif

static void help(const char *progname, apr_pool_t *pool)
{
  int i;

  svn_error_clear(svn_cmdline_fprintf(stdout, pool,
                                      _("usage: %s [options] [test-numbers]\n"
                                      "\n"
                                      "Valid options:\n"),
                                      progname));
  for (i = 0; cl_options[i].name && cl_options[i].optch; i++)
    {
      const char *optstr;

      svn_opt_format_option(&optstr, cl_options + i, TRUE, pool);
      svn_error_clear(svn_cmdline_fprintf(stdout, pool, "  %s\n", optstr));
    }
  svn_error_clear(svn_cmdline_fprintf(stdout, pool, "\n"));
}

static svn_error_t *init_test_data(const char *argv0, apr_pool_t *pool)
{
  const char *temp_path;
  const char *base_name;

  /* Convert the program path to an absolute path. */
  SVN_ERR(svn_utf_cstring_to_utf8(&temp_path, argv0, pool));
  temp_path = svn_dirent_internal_style(temp_path, pool);
  SVN_ERR(svn_dirent_get_absolute(&temp_path, temp_path, pool));
  SVN_ERR_ASSERT(!svn_dirent_is_root(temp_path, strlen(temp_path)));

  /* Extract the interesting bits of the path. */
  temp_path = svn_dirent_dirname(temp_path, pool);
  base_name = svn_dirent_basename(temp_path, pool);
  if (0 == strcmp(base_name, ".libs"))
    {
      /* This is a libtoolized binary, skip the .libs directory. */
      temp_path = svn_dirent_dirname(temp_path, pool);
      base_name = svn_dirent_basename(temp_path, pool);
    }
  temp_path = svn_dirent_dirname(temp_path, pool);

  /* temp_path should now point to the root of the test
     builddir. Construct the path to the transient dir.  Note that we
     put the path insinde the cmdline/svn-test-work area. This is
     because trying to get the cmdline tests to use a different work
     area is unprintable; so we put the C test transient dir in the
     cmdline tests area, as the lesser of evils ... */
  temp_path = svn_dirent_join_many(pool, temp_path,
                                   "cmdline", "svn-test-work",
                                   base_name, SVN_VA_NULL);

  /* Finally, create the transient directory. */
  SVN_ERR(svn_io_make_dir_recursively(temp_path, pool));

  data_path = temp_path;
  return SVN_NO_ERROR;
}

const char *
svn_test_data_path(const char *base_name, apr_pool_t *result_pool)
{
  return svn_dirent_join(data_path, base_name, result_pool);
}


/* Standard svn test program */
int
main(int argc, const char *argv[])
{
  const char *prog_name;
  int i;
  svn_boolean_t got_error = FALSE;
  apr_pool_t *pool, *test_pool;
  svn_boolean_t ran_a_test = FALSE;
  svn_boolean_t list_mode = FALSE;
  int opt_id;
  apr_status_t apr_err;
  apr_getopt_t *os;
  svn_error_t *err;
  char errmsg[200];
  /* How many tests are there? */
  int array_size = get_array_size();

  svn_test_opts_t opts = { NULL };

  opts.fs_type = DEFAULT_FS_TYPE;

  /* Initialize APR (Apache pools) */
  if (apr_initialize() != APR_SUCCESS)
    {
      printf("apr_initialize() failed.\n");
      exit(1);
    }

  /* set up the global pool.  Use a separate allocator to limit memory
   * usage but make it thread-safe to allow for multi-threaded tests.
   */
  pool = apr_allocator_owner_get(svn_pool_create_allocator(TRUE));
  err = svn_mutex__init(&log_mutex, TRUE, pool);
  if (err)
    {
      svn_handle_error2(err, stderr, TRUE, "svn_tests: ");
      svn_error_clear(err);
    }

  /* Remember the command line */
  test_argc = argc;
  test_argv = argv;

  err = init_test_data(argv[0], pool);
  if (err)
    {
      svn_handle_error2(err, stderr, TRUE, "svn_tests: ");
      svn_error_clear(err);
    }

  err = svn_cmdline__getopt_init(&os, argc, argv, pool);
  if (err)
    {
      svn_handle_error2(err, stderr, TRUE, "svn_tests: ");
      svn_error_clear(err);
    }


  os->interleave = TRUE; /* Let options and arguments be interleaved */

  /* Strip off any leading path components from the program name.  */
  prog_name = strrchr(argv[0], '/');
  if (prog_name)
    prog_name++;
  else
    {
      /* Just check if this is that weird platform that uses \ instead
         of / for the path separator. */
      prog_name = strrchr(argv[0], '\\');
      if (prog_name)
        prog_name++;
      else
        prog_name = argv[0];
    }

#ifdef WIN32
#if _MSC_VER >= 1400
  /* ### This should work for VC++ 2002 (=1300) and later */
  /* Show the abort message on STDERR instead of a dialog to allow
     scripts (e.g. our testsuite) to continue after an abort without
     user intervention. Allow overriding for easier debugging. */
  if (!getenv("SVN_CMDLINE_USE_DIALOG_FOR_ABORT"))
    {
      /* In release mode: Redirect abort() errors to stderr */
      _set_error_mode(_OUT_TO_STDERR);

      /* In _DEBUG mode: Redirect all debug output (E.g. assert() to stderr.
         (Ignored in releas builds) */
      _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDERR);
      _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
      _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
      _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    }
#endif /* _MSC_VER >= 1400 */
#endif

  if (err)
    return svn_cmdline_handle_exit_error(err, pool, prog_name);
  while (1)
    {
      const char *opt_arg;

      /* Parse the next option. */
      apr_err = apr_getopt_long(os, cl_options, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err && (apr_err != APR_BADCH))
        {
          /* Ignore invalid option error to allow passing arbitary options */
          fprintf(stderr, "apr_getopt_long failed : [%d] %s\n",
                  apr_err, apr_strerror(apr_err, errmsg, sizeof(errmsg)));
          exit(1);
        }

      switch (opt_id) {
        case help_opt:
          help(prog_name, pool);
          exit(0);
        case cleanup_opt:
          cleanup_mode = TRUE;
          break;
        case config_opt:
          opts.config_file = apr_pstrdup(pool, opt_arg);
          break;
        case fstype_opt:
          opts.fs_type = apr_pstrdup(pool, opt_arg);
          break;
        case srcdir_opt:
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&opts.srcdir, opt_arg, pool));
          opts.srcdir = svn_dirent_internal_style(opts.srcdir, pool);
          break;
        case list_opt:
          list_mode = TRUE;
          break;
        case mode_filter_opt:
          if (svn_cstring_casecmp(opt_arg, "PASS") == 0)
            mode_filter = svn_test_pass;
          else if (svn_cstring_casecmp(opt_arg, "XFAIL") == 0)
            mode_filter = svn_test_xfail;
          else if (svn_cstring_casecmp(opt_arg, "SKIP") == 0)
            mode_filter = svn_test_skip;
          else if (svn_cstring_casecmp(opt_arg, "ALL") == 0)
            mode_filter = svn_test_all;
          else
            {
              fprintf(stderr, "FAIL: Invalid --mode-filter option.  Try ");
              fprintf(stderr, " PASS, XFAIL, SKIP or ALL.\n");
              exit(1);
            }
          break;
        case verbose_opt:
          verbose_mode = TRUE;
          break;
        case quiet_opt:
          quiet_mode = TRUE;
          break;
        case allow_segfault_opt:
          allow_segfaults = TRUE;
          break;
        case server_minor_version_opt:
          {
            char *end;
            opts.server_minor_version = (int) strtol(opt_arg, &end, 10);
            if (end == opt_arg || *end != '\0')
              {
                fprintf(stderr, "FAIL: Non-numeric minor version given\n");
                exit(1);
              }
            if ((opts.server_minor_version < 3)
                || (opts.server_minor_version > 6))
              {
                fprintf(stderr, "FAIL: Invalid minor version given\n");
                exit(1);
              }
            break;
          }
#if HAVE_THREADPOOLS
        case parallel_opt:
          parallel = TRUE;
          break;
#endif
      }
    }

  /* Disable sleeping for timestamps, to speed up the tests. */
  apr_env_set(
         "SVN_I_LOVE_CORRUPTED_WORKING_COPIES_SO_DISABLE_SLEEP_FOR_TIMESTAMPS",
         "yes", pool);

  /* You can't be both quiet and verbose. */
  if (quiet_mode && verbose_mode)
    {
      fprintf(stderr, "FAIL: --verbose and --quiet are mutually exclusive\n");
      exit(1);
    }

  /* Create an iteration pool for the tests */
  cleanup_pool = svn_pool_create(pool);
  test_pool = svn_pool_create(pool);

  if (!allow_segfaults)
    svn_error_set_malfunction_handler(svn_error_raise_on_malfunction);

  if (argc >= 2)  /* notice command-line arguments */
    {
      if (! strcmp(argv[1], "list") || list_mode)
        {
          const char *header_msg;
          ran_a_test = TRUE;

          /* run all tests with MSG_ONLY set to TRUE */
          header_msg = "Test #  Mode   Test Description\n"
                       "------  -----  ----------------\n";
          for (i = 1; i <= array_size; i++)
            {
              if (do_test_num(prog_name, i, TRUE, &opts, &header_msg,
                              test_pool))
                got_error = TRUE;

              /* Clear the per-function pool */
              svn_pool_clear(test_pool);
              svn_pool_clear(cleanup_pool);
            }
        }
      else
        {
          for (i = 1; i < argc; i++)
            {
              if (svn_ctype_isdigit(argv[i][0]) || argv[i][0] == '-')
                {
                  int test_num = atoi(argv[i]);
                  if (test_num == 0)
                    /* A --option argument, most likely. */
                    continue;

                  ran_a_test = TRUE;
                  if (do_test_num(prog_name, test_num, FALSE, &opts, NULL,
                                  test_pool))
                    got_error = TRUE;

                  /* Clear the per-function pool */
                  svn_pool_clear(test_pool);
                  svn_pool_clear(cleanup_pool);
                }
            }
        }
    }

  if (! ran_a_test)
    {
      /* just run all tests */
      if (svn_test_max_threads < 1)
        svn_test_max_threads = array_size;

      if (svn_test_max_threads == 1 || !parallel)
        {
          for (i = 1; i <= array_size; i++)
            {
              if (do_test_num(prog_name, i, FALSE, &opts, NULL, test_pool))
                got_error = TRUE;

              /* Clear the per-function pool */
              svn_pool_clear(test_pool);
              svn_pool_clear(cleanup_pool);
            }
        }
#if HAVE_THREADPOOLS
      else
        {
          got_error = do_tests_concurrently(prog_name, array_size,
                                            svn_test_max_threads,
                                            &opts, test_pool);

          /* Execute all cleanups */
          svn_pool_clear(test_pool);
          svn_pool_clear(cleanup_pool);
        }
#endif
    }

  /* Clean up APR */
  svn_pool_destroy(pool);      /* takes test_pool with it */
  apr_terminate();

  return got_error;
}
