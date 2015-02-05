def make_diff_header(path, old_tag, new_tag, src_label=None, dst_label=None):
  """Generate the expected diff header for file PATH, with its old and new
  versions described in parentheses by OLD_TAG and NEW_TAG. SRC_LABEL and
  DST_LABEL are paths or urls that are added to the diff labels if we're
  diffing against the repository or diffing two arbitrary paths.
  Return the header as an array of newline-terminated strings."""
  if src_label:
    src_label = src_label.replace('\\', '/')
    src_label = '\t(.../' + src_label + ')'
  else:
    src_label = ''
  if dst_label:
    dst_label = dst_label.replace('\\', '/')
    dst_label = '\t(.../' + dst_label + ')'
  else:
    dst_label = ''
  path_as_shown = path.replace('\\', '/')
  return [
    "Index: " + path_as_shown + "\n",
    "===================================================================\n",
    "--- " + path_as_shown + src_label + "\t(" + old_tag + ")\n",
    "+++ " + path_as_shown + dst_label + "\t(" + new_tag + ")\n",
    ]

def make_no_diff_deleted_header(path, old_tag, new_tag):
  """Generate the expected diff header for a deleted file PATH when in
  'no-diff-deleted' mode. (In that mode, no further details appear after the
  header.) Return the header as an array of newline-terminated strings."""
  path_as_shown = path.replace('\\', '/')
  return [
    "Index: " + path_as_shown + " (deleted)\n",
    "===================================================================\n",
    ]

def make_git_diff_header(target_path, repos_relpath,
                         old_tag, new_tag, add=False, src_label=None,
                         dst_label=None, delete=False, text_changes=True,
                         cp=False, mv=False, copyfrom_path=None,
                         copyfrom_rev=None):
  """ Generate the expected 'git diff' header for file TARGET_PATH.
  REPOS_RELPATH is the location of the path relative to the repository root.
  The old and new versions ("revision X", or "working copy") must be
  specified in OLD_TAG and NEW_TAG.
  SRC_LABEL and DST_LABEL are paths or urls that are added to the diff
  labels if we're diffing against the repository. ADD, DELETE, CP and MV
  denotes the operations performed on the file. COPYFROM_PATH is the source
  of a copy or move.  Return the header as an array of newline-terminated
  strings."""

  path_as_shown = target_path.replace('\\', '/')
  if src_label:
    src_label = src_label.replace('\\', '/')
    src_label = '\t(.../' + src_label + ')'
  else:
    src_label = ''
  if dst_label:
    dst_label = dst_label.replace('\\', '/')
    dst_label = '\t(.../' + dst_label + ')'
  else:
    dst_label = ''

  output = [
    "Index: " + path_as_shown + "\n",
    "===================================================================\n"
  ]
  if add:
    output.extend([
      "diff --git a/" + repos_relpath + " b/" + repos_relpath + "\n",
      "new file mode 10644\n",
    ])
    if text_changes:
      output.extend([
        "--- /dev/null\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + dst_label + "\t(" + new_tag + ")\n"
      ])
  elif delete:
    output.extend([
      "diff --git a/" + repos_relpath + " b/" + repos_relpath + "\n",
      "deleted file mode 10644\n",
    ])
    if text_changes:
      output.extend([
        "--- a/" + repos_relpath + src_label + "\t(" + old_tag + ")\n",
        "+++ /dev/null\t(" + new_tag + ")\n"
      ])
  elif cp:
    if copyfrom_rev:
      copyfrom_rev = '@' + copyfrom_rev
    else:
      copyfrom_rev = ''
    output.extend([
      "diff --git a/" + copyfrom_path + " b/" + repos_relpath + "\n",
      "copy from " + copyfrom_path + copyfrom_rev + "\n",
      "copy to " + repos_relpath + "\n",
    ])
    if text_changes:
      output.extend([
        "--- a/" + copyfrom_path + src_label + "\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + "\t(" + new_tag + ")\n"
      ])
  elif mv:
    output.extend([
      "diff --git a/" + copyfrom_path + " b/" + path_as_shown + "\n",
      "rename from " + copyfrom_path + "\n",
      "rename to " + repos_relpath + "\n",
    ])
    if text_changes:
      output.extend([
        "--- a/" + copyfrom_path + src_label + "\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + "\t(" + new_tag + ")\n"
      ])
  else:
    output.extend([
      "diff --git a/" + repos_relpath + " b/" + repos_relpath + "\n",
      "--- a/" + repos_relpath + src_label + "\t(" + old_tag + ")\n",
      "+++ b/" + repos_relpath + dst_label + "\t(" + new_tag + ")\n",
    ])
  return output

def make_diff_prop_header(path):
  """Return a property diff sub-header, as a list of newline-terminated
     strings."""
  return [
    "\n",
    "Property changes on: " + path.replace('\\', '/') + "\n",
    "___________________________________________________________________\n"
  ]

def make_diff_prop_val(plus_minus, pval):
  "Return diff for prop value PVAL, with leading PLUS_MINUS (+ or -)."
  if len(pval) > 0 and pval[-1] != '\n':
    return [plus_minus + pval + "\n","\\ No newline at end of property\n"]
  return [plus_minus + pval]

def make_diff_prop_deleted(pname, pval):
  """Return a property diff for deletion of property PNAME, old value PVAL.
     PVAL is a single string with no embedded newlines.  Return the result
     as a list of newline-terminated strings."""
  return [
    "Deleted: " + pname + "\n",
    "## -1 +0,0 ##\n"
  ] + make_diff_prop_val("-", pval)

def make_diff_prop_added(pname, pval):
  """Return a property diff for addition of property PNAME, new value PVAL.
     PVAL is a single string with no embedded newlines.  Return the result
     as a list of newline-terminated strings."""
  return [
    "Added: " + pname + "\n",
    "## -0,0 +1 ##\n",
  ] + make_diff_prop_val("+", pval)

def make_diff_prop_modified(pname, pval1, pval2):
  """Return a property diff for modification of property PNAME, old value
     PVAL1, new value PVAL2.

     PVAL is a single string with no embedded newlines.  A newline at the
     end is significant: without it, we add an extra line saying '\ No
     newline at end of property'.

     Return the result as a list of newline-terminated strings.
  """
  return [
    "Modified: " + pname + "\n",
    "## -1 +1 ##\n",
  ] + make_diff_prop_val("-", pval1) + make_diff_prop_val("+", pval2)
  exit_code, diff_output, err_output = svntest.main.run_svn(
    1, 'diff', sbox.ospath('A/D/foo'))

  if count_diff_output(diff_output) != 0: raise svntest.Failure

  # At one point this would crash, so we would only get a 'Segmentation Fault'
  # error message.  The appropriate response is a few lines of errors.  I wish
  # there was a way to figure out if svn crashed, but all run_svn gives us is
  # the output, so here we are...
  for line in err_output:
    if re.search("foo' is not under version control$", line):
      break
  else:
    raise svntest.Failure
  "show diffs for binary files with --force"
  # Check that we get diff when the first, the second and both files are
  # marked as binary.
  exit_code, stdout, stderr = svntest.main.run_svn(None,
                                                   'diff', '-r1:2', iota_path,
                                                   '--force')

  for line in stdout:
    if (re_nodisplay.match(line)):
      raise svntest.Failure

  exit_code, stdout, stderr = svntest.main.run_svn(None,
                                                   'diff', '-r2:1', iota_path,
                                                   '--force')

  for line in stdout:
    if (re_nodisplay.match(line)):
      raise svntest.Failure

  exit_code, stdout, stderr = svntest.main.run_svn(None,
                                                   'diff', '-r2:3', iota_path,
                                                   '--force')

  for line in stdout:
    if (re_nodisplay.match(line)):
      raise svntest.Failure
                                                "working copy") + [
                                                "working copy") + [
  expected_output_base_r2 = make_diff_header("foo", "revision 0",
  expected_output_r1_base = make_diff_header("foo", "revision 0",
                                                "revision 1") + [
  expected_output_base_working[3] = "+++ foo\t(working copy)\n"
  diff_X_r1_base = make_diff_header("X", "revision 0",
  diff_X_base_r3 = make_diff_header("X", "revision 0",
  diff_foo_r1_base = make_diff_header("foo", "revision 0",
  diff_foo_base_r3 = make_diff_header("foo", "revision 0",
  diff_X_bar_r1_base = make_diff_header("X/bar", "revision 0",
  diff_X_bar_base_r3 = make_diff_header("X/bar", "revision 0",
  expected_output_r1_BASE = make_diff_header("X/bar", "revision 0",
  expected_output_r1_WORKING = make_diff_header("X/bar", "revision 0",
  diff_repos_wc = make_diff_header("A/mucopy", "revision 2", "working copy")
                         "revision 0", "working copy",
                                         "working copy",
  ] + make_git_diff_header(new_path, "new", "revision 0",
                                         "revision 1", "working copy",
                           "revision 1", "working copy",
                           "revision 1", "working copy",
  expected_output = make_git_diff_header(new_path, "new", "revision 0",
  ] + make_git_diff_header(mu_path, "A/mu", "revision 1", "working copy",
                                         "revision 2",
    ] + make_git_diff_header("new", "new", "revision 0", "revision 2",
  expected_output = make_git_diff_header(new_path, "new", "revision 0",
                                         "revision 0", "working copy",
  # The same again, but specifying the target explicity. This should
  expected_output = make_diff_header('newdir/newfile', 'working copy',
                                         'working copy',
                    ] + make_diff_header('A/B/F', 'working copy',
                        make_diff_prop_modified("newprop", "propval-old\n",
                                         'working copy',
                    ] + make_diff_header('A/D/G/pi', 'working copy',
                                         'working copy',
                                         'working copy',
                                         'working copy',
                    ]
  # Files in diff may be in any order.
  expected_output = make_diff_header("chi", "revision 1", "revision 2") + [
                                         "revision 2") + [
                                         "revision 2") + [
  expected_output = make_diff_header("G/pi", "working copy", "working copy",
                    ] + make_diff_header("G/rho", "working copy",
                    ] + make_diff_header("G/tau", "working copy",
                    ] + make_diff_header("H/chi", "working copy",
                    ] + make_diff_header("H/omega", "working copy",
                    ] + make_diff_header("H/psi", "working copy",
                                         "working copy", "B/E", "D") + [
                                         "working copy", "B/E", "D") + [
                    ] + make_diff_header("gamma", "working copy",
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E/alpha'),
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E/beta'),
    '--- %s\t(revision 0)\n' % sbox.path('A/B/E'),
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E/alpha'),
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E/beta'),
    '--- %s\t(revision 0)\n' % sbox.path('A/B/E/beta'),
    '--- %s\t(revision 0)\n' % sbox.path('A/B/E'),
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E/alpha'),