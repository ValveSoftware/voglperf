//
// Fake libGL.so just so we can get a DT_NEEDED libGL.so section in libvoglperf.so...
//
// gcc -m32 -fPIC -g -fvisibility=hidden -fno-exceptions -g -O0 -Wl,--no-undefined -shared -Wl,-soname,libGL.so -o i386-linux-gnu/libGL.so libGL.c
// gcc -fPIC -g -fvisibility=hidden -fno-exceptions -g -O0 -Wl,--no-undefined -shared -Wl,-soname,libGL.so -o x86_64-linux-gnu/libGL.so libGL.c
//

static int blah()
{
}

