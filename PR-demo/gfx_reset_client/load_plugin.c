/* Select the test-only channel while retaining the installed X11 client. */
#include <dlfcn.h>
#include <string.h>
#include <freerdp/addin.h>
#include <freerdp/dvc.h>

extern UINT VCAPITYPE rdpgfx_DVCPluginEntry(IDRDYNVC_ENTRY_POINTS *entry);

PVIRTUALCHANNELENTRY
freerdp_load_channel_addin_entry(LPCSTR name, LPCSTR subsystem,
                                 LPCSTR type, DWORD flags)
{
    FREERDP_LOAD_CHANNEL_ADDIN_ENTRY_FN original;

    if (name != NULL && strcmp(name, "rdpgfx") == 0)
    {
        return (PVIRTUALCHANNELENTRY)(void *)rdpgfx_DVCPluginEntry;
    }
    original = dlsym(RTLD_NEXT, "freerdp_load_channel_addin_entry");
    return original == NULL ? NULL : original(name, subsystem, type, flags);
}
