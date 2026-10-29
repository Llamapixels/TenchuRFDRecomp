// The generated freestanding DLL uses these standard rounding functions.
// Match the SDK's existing ucrtbase math forwarding in cpu/dispatch.cpp.
#pragma comment(linker, "/EXPORT:floor=ucrtbase.floor")
#pragma comment(linker, "/EXPORT:ceil=ucrtbase.ceil")
#pragma comment(linker, "/EXPORT:trunc=ucrtbase.trunc")
#pragma comment(linker, "/EXPORT:nearbyint=ucrtbase.nearbyint")
#pragma comment(linker, "/EXPORT:floorf=ucrtbase.floorf")
#pragma comment(linker, "/EXPORT:ceilf=ucrtbase.ceilf")
#pragma comment(linker, "/EXPORT:truncf=ucrtbase.truncf")
#pragma comment(linker, "/EXPORT:nearbyintf=ucrtbase.nearbyintf")
