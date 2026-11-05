p="o/linker_soinfo.cpp"; s=open(p).read()
old="""    bionic___system_properties_init();
    constructors_called = true;
    return;
  }
"""
new="""    /* hybris: bionic's property area (/dev/__properties__) only exists on an
     * Android host; all __system_property_* calls are hooked anyway, so skip
     * the bionic init when the area is absent instead of faulting in it. */
    if (access("/dev/__properties__", F_OK) == 0)
      bionic___system_properties_init();
    else
      DEBUG("HYBRIS: no /dev/__properties__, skipping bionic property init\\n");
    constructors_called = true;
    return;
  }
"""
assert s.count(old)==1; s=s.replace(old,new)
if "#include <unistd.h>" not in s:
    s=s.replace('#include "linker_soinfo.h"','#include "linker_soinfo.h"\n#include <unistd.h>',1)
open(p,"w").write(s); print("patched")
