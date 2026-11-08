package com.rev4n.unlimitedphotos;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

public final class EntryPoint implements InvocationHandler {

    private final Object base;

    private EntryPoint(Object base) {
        this.base = base;
    }

    public static Object createProxy(Object realPm) throws ClassNotFoundException {
        Class<?> iPm = Class.forName("android.content.pm.IPackageManager", false, realPm.getClass().getClassLoader());
        return Proxy.newProxyInstance(iPm.getClassLoader(), new Class<?>[]{iPm}, new EntryPoint(realPm));
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
        if (args != null && args.length > 0 && args[0] instanceof String feature
                && "hasSystemFeature".equals(method.getName())) {
            switch (feature) {
                case "com.google.android.apps.photos.NEXUS_PRELOAD",
                     "com.google.android.apps.photos.PIXEL_PRELOAD",
                     "com.google.android.apps.photos.PIXEL_2016_PRELOAD",
                     "com.google.android.feature.PIXEL_EXPERIENCE",
                     "com.google.android.feature.GOOGLE_BUILD",
                     "com.google.android.feature.GOOGLE_EXPERIENCE" -> {
                    return Boolean.TRUE;
                }
                case "com.google.android.apps.photos.PIXEL_2017_PRELOAD",
                     "com.google.android.apps.photos.PIXEL_2018_PRELOAD",
                     "com.google.android.apps.photos.PIXEL_2019_MIDYEAR_PRELOAD",
                     "com.google.android.apps.photos.PIXEL_2019_PRELOAD",
                     "com.google.android.apps.photos.PIXEL_2020_EXPERIENCE",
                     "com.google.android.apps.photos.PIXEL_2020_MIDYEAR_EXPERIENCE" -> {
                    return Boolean.FALSE;
                }
            }
        }
        try {
            return method.invoke(base, args);
        } catch (InvocationTargetException e) {
            Throwable cause = e.getCause();
            throw (cause != null) ? cause : e;
        }
    }
}
