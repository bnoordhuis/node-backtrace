NODE-BACKTRACE
==============

Prints a stack trace on SIGABRT.  What sets it apart from other stack
trace tools is that it knows how to decode V8 JS frames.

This tool is probably only useful for node.js add-on and core
developers.


USAGE
=====

The only requirement is that you load the module, i.e.:

    require('backtrace');
    process.abort();  // Trigger the SIGABRT handler.


API
===

backtrace.backtrace()  -  Prints a backtrace to stderr.


KNOWN BUGS
==========

* Only supports i386 and x86_64 (ia32 and x64).
* Only tested on Linux and OS X.


LICENSE
=======

Copyright (c) 2013, Ben Noordhuis <info@bnoordhuis.nl>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
