# Event-Driven Chat Server

This project implements an event-driven chat server using C, which forwards each incoming message to all connected clients except the one from which the message was received. The server is implemented using the `select()` system call, making it non-blocking and entirely event-driven.

### Prerequisites

To run this project on a Linux machine, you need the following:

A Linux environment with a C compiler.

### Compilation

To compile the project, use the following command:

gcc -o chatServer chatServer.c

### Running the Server

./chatServer <port>
