#!/system/bin/sh
# OneUI fixes at every start (see oneui-firstboot.rc)
# SystemUI (this build) asks One UI Home's settings provider whether a DeX
# display is on at every CLOSE_SYSTEM_DIALOGS (screen off, Home, Recents),
# but lacks the permission for that provider and catches only an unknown
# provider: it crashed each time. With the provider off, the call fails the
# caught way; One UI Home itself works without it.
pm disable com.sec.android.app.launcher/com.android.launcher3.LauncherProvider > /dev/null 2>&1
