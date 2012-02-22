# Despot

A Redis-backed Spotify jukebox, in pure C. Bring tyranny to your office stereo.

## Dependencies

- OpenAL
- Hiredis
- libspotify (tested on v10)

Despot should build on any POSIX-compliant system supported by libspotify.
Sorry, Windows.

Despot expects to find `libspotify` in the `lib` subdirectory. You can get a
copy for your operating system from [developer.spotify.com](http://developer.spotify.com).

You'll also need to put a copy of `api.h` in `include/libspotify`. This file
ships with the libspotify distribution.

Licensing prohibits distributing these files with Despot, sorry.

## Install

Despot has been built and tested on:

- Mac OS X 10.7
- Ubuntu 10.04 LTS

### Configuring your API key

Save a 'c-code' API key from [developer.spotify.com](https://developer.spotify.com/en/libspotify/application-key)
into `key.h` in the project root directory. (It should compile fine as-is, but
you really only need the `g_appkey` variable. You can delete the #include lines
and `g_appkey_size`.)

### Building Despot

`make`

## Usage

Despot and [despot-web](https://github.com/arnorhs/despot-web) are like Bonnie
and Clyde. If you are looking for a despot frontend, you really ought to try
despot-web.

### Redis Interface

Despot talks to you via Redis:

- `despot:now_playing`: the current track's Spotify URL
- `despot:queue`: the playlist; despot reads the next track using BLPOP
- `despot:commands`: the command queue; see Commands

### Commands

Despot currently supports two commands. You can issue them by pushing them onto
the `despot:commands` list.

- `NEXT`: skip to the next track
- `VOLUME [0-100]`: set the volume

## License

The MIT License

Copyright (c) Paul Rosania, http://paul.rosania.org

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
