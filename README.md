# NAME

mlock — C memory allocator

# SYNOPSIS

```c
#include "mlock.h"

void* init_lock();
void* mlock(size_t size);
void  unlock(void* ptr);
void* relock(void* ptr, size_t size);
```

# DESCRIPTION

# BENCHMARKING

# INSTALL

# CONFIGURATION

# BUGS

Known bugs will be listed here.

Report bugs to [bugs@cwiebe.com](mailto:bugs@cwiebe.com).

# SEE ALSO

- [Source code](https://github.com/ctwiebe23/m-lock)
- [Online README](https://cwiebe.com/m-lock)
- [CHANGELOG](https://cwiebe.com/m-lock/changelog)
