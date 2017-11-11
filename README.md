# Chops - Connective Handcrafted Openwork Software Medley

The Chops Medley is a collection of C++ libraries for networking and distributed processing. It is written using modern C++ design idioms and the latest (2017) C++ standard.

# Chops Libraries

## Chops Net

### Overview

Chops Net is an asynchronous general purpose networking library layered on top of the C++ Networking Technical Standard (TS). It is designed to simplify application code for processing data on multiple simultaneous connections or endpoints in an asynchronous, efficient manner. Every interaction with Chops Net operations (methods) is no-wait as well as asynchronous if it involves any network processing. Chops Net:

- simplifies the creation of various IP (Internet Protocol) networking entities including TCP acceptors and connectors, UDP senders and receivers, and UDP multicast senders and receivers.
- simplifies the resolution of network names to IP addresses (i.e. domain name system lookups).
- abstracts the concept of message handling in TCP (Transmission Control Protocol) and provides customization points in two areas:
  1. message framing, which is the code and logic that determines the begin and end of a message within the TCP byte stream.
  2. message processing, which is the code and logic that processes a message when the framing determines a complete message has arrived.
- provides buffer lifetime management for outgoing data.
- provides customization points for state changes in the networking entities, including:
  - a connection has become active or inactive (TCP acceptors and connectors).
  - an endpoint has been created or destroyed (UDP).
  - an error has occurred.
- implements the "plumbing" for asynchronous processing on multiple simultaneous connections.
- abstracts some of the differences between protocols (TCP, UDP, UDP multicast), allowing easier application transitioning between protocol types.

Chops Net is designed to make it easy and efficient for an application to create hundreds or thousands of network connections and handle them simultaneously. In particular, there are no mutexes or threads or thread pools within Chops Net, and it works well with only one application thread invoking the event loop (an executor, in current C++ terminology).

### General Usage and Design Model

Chops Net is generally a "create network entity, provide function objects and let them do the work" API for incoming data, and a "send and forget" API for outgoing data. 

For incoming data, an application provides callable function objects to the appropriate Chops Net object for message framing (if needed), message processing, and connection state transitions.

For outgoing data, an application passes message data (as byte buffers) to the appropriate Chops Net object for queueing and transmission. A callable function object can be provided by the application to monitor the size of an outgoing queue.

Various Chops Net objects are provided to the application (typically in application provided function objects) as connection or endpoint states transition. For example, the object for data sending is only created when a connection becomes active (TCP acceptor connection is created, or a TCP connector connection succeeds). This guarantees that an application cannot start sending data before a connection is active.

Chops Net has the following design goals:

- Encapsulate and simplify the (sometimes complex) details of asynchronous network programming. Managing buffer lifetimes can be tricky, and this library makes sure it is done correctly. Chaining asynchronous events together is handled within this library, so application code is simplified. Error handling is simpler for the application.
- Abstract and separate the message framing and message processing for message streams. Sometimes the same "wire protocol" (i.e. message header and message body definition) is used on multiple connections, but different message processing is required depending on the connection address (or connection type). Chops Net provides specific customization points to ease code sharing and code isolation. In particular, a particular message framing function object might be defined for a TCP stream (and not needed for a UDP entity), but a message processing function object used for both TCP and UDP entities.
- Make it easy to write correct network code, and hard to write incorrect network code. An example is that message sending cannot be started before a TCP connection is active. Another example is that correctly collecting all of the bytes in a TCP message header is easier with this library (this is a common source of errors in TCP network programming).
- Provide customization points so that the application can be notified of anything of interest.

(State transition diagram to be inserted here.)

### Constraints

Chops Net works well with the following communication patterns:

- Receive data, process it quickly (which may involve passing data along to another thread).
- Receive data, process it quickly (as above), send data back through the same connection or endpoint.
- Send data.

Chops Net requires more work with the following communication pattern:

- Send data, wait for reply (request-reply pattern). Everything in Chops Net is no-wait from the application perspective, so request-reply must be emulated through application logic (i.e. store some form of message transaction id in an outgoing table, correlate incoming data using the message transaction id).

Chops Net works extremely well in environments where there might be a lot of network connections (e.g. thousands), each with a moderate amount of traffic, and each with different kinds of data or data processing. In environments where each connection is very busy, or a lot of processing is required for each incoming message (and it cannot be passed along to another thread), then more traditional communication patterns or designs might be appropriate (e.g. blocking or synchronous I/O, or "thread per connection" models).

Applications that do only one thing and must do it as fast as possible and want the least amount of overhead might not want the abstraction penalties and slight overhead of Chops Net. For example, a high performance web server where buffer lifetimes for incoming data are easily managed might not want the queuing and "shared buffer" overhead of Chops Net.

Applications that need to perform time consuming operations on incoming data and cannot pass that data off to another thread may encounter throughput issues. Multiple threads or thread pools or strands interacting with the event loop method (executor) may be a solution in those environments.

Example environments where Chops Net is a good fit:

- Applications interacting with multiple sensors or inputs or outputs, each with low to moderate throughput needs (i.e. IoT environments, chat networks, gaming networks).
- Small footprint or embedded environments, where all network processing is run inside a single thread.
- Applications with relatively simple network processing that need an easy-to-use and quick-for-development network library.

## Chops Wait Queue

Chops Wait Queue is a multi-reader, multi-writer FIFO queue for transferring data between threads. It is templatized on the type of data passed through the queue as well as the queue container type. Data is passed with value semantics, either by copying or by moving (as opposed to a queue that transfers data by pointer or reference). The wait queue has both wait and no-wait pop semantics, as well as simple "close" and "open" capabilities (to allow graceful shutdown or restart of thread or process communication).

Multiple writer and reader threads can access a Wait Queue simultaneously, although when a value is pushed on the queue, only one reader thread will be notified to consume the value.

Close sementics are simple, and consist of setting an internal flag and notifying all waiting reader threads. Subsequent pushes are disallowed (an error is returned on the push). On close, if the queue is non-empty data is not flushed (elements in the queue will be destructed when the Wait Queue object is destructed, as typical in C++).

Wait Queue uses C++ standard library concurrency facilities (mutex, condition variables) in its implementation. It is not a lock-free queue, but it has been designed to be used in memory constrained environments or where deterministic performance is needed. In particular, Wait Queue:

- Has been tested with Martin Moene's `ring_span` library for the internal container (see the Language Requirements and Alternatives section for more details). A `ring_span` is traditionally known as a "ring buffer". This implies that the Wait Queue can be used in environments where dynamic memory management (heap) is not allowed or is problematic. In particular, no heap memory is directly allocated within the Wait Queue.

- Does not throw or catch exceptions anywhere in its code base. 

The implementation is adapted from the book Concurrency in Action, Practical Multithreading, by Anthony Williams. Anthony is a recognized expert in concurrency including Boost Thread and C++ standards efforts. His web site is http://www.justsoftwaresolutions.co.uk. It is highly recommended to buy his book, whether in paper or electronic form, and Anthony is busy at work on a second edition (covering C++ 14 and C++ 17 concurrency facilities) now available in pre-release form.

The core logic in this library is the same as provided by Anthony in his book, but the API has changed and additional features added. The name of the utility class template in Anthony's book is `threadsafe_queue`.

## Chops Utilities

Useful utility code, including:

- Repeat, a function template to abstract and simplify loops that repeat N times, from Vittorio Romeo, https://vittorioromeo.info/. The C++ range based `for` doesn't directly allow N repetitions of code. Vittorio's utility fills that gap. His blog is excellent and well worth reading.

# Chops C++ Language Requirements and Alternatives

A significant number of C++ 11 features are in the implementation and API. There are also limited C++ 14 and 17 features in use, although they tend to be relatively simple features of those standards (e.g. `std::optional`, `std::byte`, structured bindings, generic lambdas). For users that don't want to use the latest C++ compilers or compile with C++ 17 flags, Martin Moene provides an excellent set of header-only libraries that implement many useful C++ library features, both C++ 17 as well as future C++ standards. These include `std::optional`, `std::variant`, `std::any`, and `std::byte` (from C++ 17) as well as `std::ring_span` (C++ 20, most likely). He also has multiple other useful repositories including an implementation of the C++ Guideline Support Library (GSL). Martin's repositories are available at https://github.com/martinmoene.

Using Boost libraries instead of `std::optional` (and similar C++ 17 features) is also an option, and should require minimal porting.

While the main production branch of Chops will always be developed and tested with C++ 17 features (and the latest compilers), alternative branches and forks for older compiler versions are expected. In particular, a branch using Martin's libraries and general C++ 11 (or C++ 14) conformance is expected for the future, and collaboration (through forking, change requests, etc) is 
very welcome. A branch supporting a pre-C++ 11 compiler or language conformance is not likely to be directly supported through this repository (since it would require so many changes that it would result in a defacto different codebase).

# Dependencies

The libraries and API's have minimal library dependencies. Currently the non-test code depends on the standard C++ library and Chris Kohlhoff's Asio library. Asio is available at https://think-async.com/ as well as https://github.com/chriskohlhoff/. Asio forms the basis for the C++ Networking Technical Standard (TS), which will (almost surely) be standardized in C++ 20. Currently the Chops Net library uses the `networking-ts-impl` repository from Chris' Github account.

The test suites have additional dependencies, including Phil Nash's Catch 2.0 for the unit test framework. The Catch library is available at https://github.com/catchorg/Catch2. Various tests for templatized queues use Martin Moene's `ring_span` library for fixed buffer queue semantics.

# Supported Compilers

Chops is compiled and tested on g++ 7.2, clang xx, and VS xx.

# Installation

All Chops libraries are header-only, so installation consists of downloading or cloning and setting compiler include paths appropriately. No compile time configuration macros are defined in Chops.

## Author Note on the Name

>Yes, the name / acronym is a stretch. Quite a stretch. I like the word "chops", which is a jazz term for strong technique, so I decided on that for a name. For example, "Check out Tal Wilkenfeld, she's got mad chops."

>I considered many names, at least two or three dozen, but my favorite ideas were all taken (or too close to existing names and could create confusion). It seems that a lot of software developers have similar creative ideas when it comes to names.

>I wasn't thinking of any Chinese related terms when I came up with "chops", such as chopsticks or the Chinese meaning of chop as a seal or fingerprint. It was weeks later before I remembered the Chinese associations.

>The crucial word in the acronym is "Connective". Most of my software engineering career has involved networking or distributed software and this collection of libraries and utilities is centered on that theme. Making it easier to write high performing networked applications, specially in C++, is one of my goals as someone writing infrastructure and core utility libraries.

>The second word of the acronym is just a "feel good" word for me. Most software is handcrafted, so there's nothing distinctive about my use of the term. I'm using the word because I like it, plus it has good mental associations for me with things like "handcrafted beer" (yum), or "handcrafted furniture" (good stuff). Handcrafted also implies quality, and I believe this software excels in its implementation and design.

>I originally was going to use the term "openhearted" as part of the project name, but decided that "openwork" is a nice alternate form of "open source" and a little more technically consistent. The dictionary says that "openwork" is work constructed so as to show openings through its substance (such as wrought-iron openwork or embroidery, and is typically ornamental), so my use of the term is not exactly correct. I don't care, I still like the way I use it.

>Even though I didn't use "openhearted" in the name, it's an aspect that I aspire to in every part of my life. I fall short, very often, but not because of lack of effort or desire.

>I picked "medley" because I think it is more representative than "suite", which (to me) implies a cohesive set of libraries, or possibly a framework.

>I mentioned Tal earlier in this note, so here are two representative YouTube videos of her playing with Jeff Beck, who I consider one of the all time best guitarists. The first video is a blend of jazz, rock, and funk, the second is the two of them playing one of my favorite rock / jazz fusion ballads: https://www.youtube.com/watch?v=jVb-izZVCwQ, https://www.youtube.com/watch?v=blp7hPFaIfU

>And since I'm on a music digression, an artist I often listen to while writing code is Helios. His music is instrumental, space music'ey, ambient, contains a nice amount of harmonic and rhythmic complexity, and is very melodic. Most important it doesn't interfere with my concentration. A good album to start with is Eingya: https://www.youtube.com/watch?v=fud-Lz76MHg
