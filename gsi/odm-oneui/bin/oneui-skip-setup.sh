#!/system/bin/sh
# Skips OneUI's setup wizard on the first start (see oneui-firstboot.rc)
settings put global device_provisioned 1
settings put secure user_setup_complete 1
pm disable-user --user 0 com.sec.android.app.SecSetupWizard
# The China build's Customization Service asks at once for location, phone
# number, browsing history and calendar (pre-ticked): off, the user can enable
# it in Settings > Apps
pm disable-user --user 0 com.samsung.android.rubin.app
