# About
Network
Execution
Wrapper
That
Operates
Nonblockingly

Is a C++ multi-platform multi-threaded networking framework/library that was built on top of the Berkeley API for a networking class.
I'm happy with the way the network has been designed to encapsulate the concepts of networking into two objects:
A data reciever
And a data sender

Which user can inherit from to give give custom to the Expect/Send data functions.

The only bad thing is that this library takes about 1MB of memory to run for some reason.  I expect it's all the polymorphism that's
going on, but I haven't gotten around to debugging that.

# To Build
Just run the cmake stuff, preferably from a different folder.