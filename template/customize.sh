[ "$BOOTMODE" = true ] && [ "$ARCH" = "arm64" ] && [ "$API" -ge 35 ] || abort "! Unsupported environment"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/classes.dex" 0 0 0444
set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0444
am force-stop com.google.android.apps.photos >/dev/null 2>&1 || true
