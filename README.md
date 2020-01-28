# libudaemon - micro daemon library

This library provides a small library for developing applications that can run
as daemon. It is aimed at being small, portable and does not have any external
dependencies.

The library is written to create small daemonized "bridge" applications that
bridge information from one server to another server. As such, its 
implementation is heavily opinionated and might not work for other use cases.

### Features

The features of this library are:

- allow applications to run both in foreground as in daemon mode;
- provide basal logging functionality that just works(tm);
- provide simple support for dealing with operating system signals, such as,
  SIGHUP, SIGUSR1 and so on;
- allow for a polling based approach (using `poll(3)`) to wait for events of
  multiple sources;
- provide simple task scheduling, for example, to handle automatic reconnects
  to disconnected servers.

## Usage

See `example/test_complete.c` for a comprehensive example on how udaemon works.

## Development

### Compilation

This project uses CMake. To compile it, do:

```sh
$ cd build
$ cmake ..
$ make
```

Among the various build files are `libudaemon.a` and `test_complete`.


## Installation

After compilation a simple `make install` will install everything.


## License

libudaemon is licensed under Apache License 2.0.


## Author

libudaemon is written by Jan Willem Janssen `j dot w dot janssen at lxtreme dot nl`.


## Copyright

(C) Copyright 2020, Jan Willem Janssen.
