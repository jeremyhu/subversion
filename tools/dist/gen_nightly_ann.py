#!/usr/bin/env python
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#

webpagetext = '''<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html>
<head>
<title>Subversion nightly tarballs (r%d)</title>
</head>
<body style="font-size: 14pt; text-align: justify;
  background-color: #f0f0f0; padding: 0 5%%">
<h1 style="font-size: 30pt; text-align: center;
  text-decoration: underline">WARNING</h1>

<p>The code you are about to download is an <i>automatically generated</i>
nightly release of Subversion (r%d).</p>

<p>This distribution is automatically generated from the latest sources from
<a href="http://svn.collab.net/repos/svn/trunk/">Subversion trunk</a>.
It may not even compile, and is certainly <i>not</i> suitable for any sort of
production use.  This distribution has not been tested, and may cause any
number of problems, up to and including death and bodily injury.  Only use this
distribution on data you aren't afraid to lose.  You have been warned.</p>

<p>We provide these for testing by those members of the community who
are interested in testing it.  As such, if you are interested in helping
us test this code, you're very welcome to download and test these packages.
If you are looking for a copy of Subversion for production use, this
is <i>not it</i>; you should instead grab the latest stable release
from the <a
href="http://subversion.tigris.org/project_packages.html">Download
area</a>.</p>

<h2 style="font-size: 18pt">Note to operating system distro package
maintainers</h2>

<p>As stated above, this is <i>not</i> an official, end-user release
of Subversion.  It is a distribution intended for testing only. Please
do <i>not</i> package this distribution in any way. It should not be
made available to users who rely on their operating system distro's
packages.</p>

<p>If you want to help us test this distribution of Subversion, you
can find the files <a href="dist/">here</a>.</p>

</body>
</html>'''

import sys
rev = int(sys.argv[1])
print(webpagetext % (rev, rev))
