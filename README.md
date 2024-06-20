jakhex
======

*jakhex* is a curses based full screen hex editor.

It supports the following **features**:

- curses based full screen interface hex editor
- POSIX C99 with a single dependency on a curses library
- ability to truncate or extend files, including inserting bytes in the middle
- read a file and insert it in the buffer at an arbitrary position
- cursor based navigation
- jump to absolute addresses
- jump to relative offsets from the current cursor position
- shows interpretation of the next few bytes as ints, floats and string
  with both big- and little endianness
- shows a binary stream starting at the current cursor location
- punch in (overwrite) data in hex, ASCII, or full int- and floating point
  numbers with either big- or little endianness
- 26 address registers you can use to save your favourite locations
- region based delete, copy, insert and overwrite commands using a single
  clipboard buffer
- efficiently search both forward and backwards for an arbitrary binary string,
  typed in either in hex or ASCII

It has the following *limitations*:

- its in-memory buffer is stored as a big contiugous array
  * meaning, loading files implies trying to allocate that much memory
  * you can derive from that what kind of file sizes you can load at any time
- the screen width is fixed to 80 columns, 32 bytes per line
  * other hex editors annoy me in that I need to fiddle with the screen size
    to get the line width to align with a round number that's easy to do maths with
- searching doesn't remember what you previously searched for (but you can use
  tmux's/X11's kill buffer to repeatedly paste the same search string)
- the details pane can't be hidden, so you need at least 13 lines of screen
- searching doesn't support patterns nor regular expressions
- cursor position and file size showed only in hex
- region commands always prompt you for a pair of markers
- keys are not rebindable, and the bindings are brain dead. Be sure to print
  out a cheat sheet!
- single buffer only
- any file related commands update the "last mentioned file name" which may not be what you want
- markers are not relocated if you insert bytes before their current address
  (they hold a number which can be used as an absolute address)
- ASCII only; I imagine you're here mostly to look at bits and bytes, possibly
  to diagnose why you have invalid UTF-8 at address 0x145f2200. The only issue
  is with filenames containing non-ASCII or non-printable characters; you can
  symlink your file to ~/link to work around this.
- there are no preferences and no rc files
- while the `g`, `+` and `-` commands accept both dec and hex input, the
  command line invocation only accepts decimal input
- string search limits you to a 2^31-2 long needle, but I hope that doesn't
  bother you. I haven't actually tested with anything longer than what you
  can type off the top of your head, so it's more of a theoretical limit

You can edit a file and jump to offset 0xA000 with

```
jakhex file +40960
```

See `jakhex -h` or `man ./jakhex.1` for more information.
