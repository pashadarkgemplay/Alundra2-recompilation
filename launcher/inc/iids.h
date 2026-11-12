// interface ids copied from WebView2.h (the sdk ships them in a separate .c file we do not use)
#pragma once
#include <guiddef.h>

template <class T> const IID& iidOf();
#define A2_IID(T, a, b, c, d0,d1,d2,d3,d4,d5,d6,d7) \
    template <> inline const IID& iidOf<T>() { static const IID v = { 0x##a, 0x##b, 0x##c, { 0x##d0, 0x##d1, 0x##d2, 0x##d3, 0x##d4, 0x##d5, 0x##d6, 0x##d7 } }; return v; }

A2_IID(ICoreWebView2Controller2, c979903e, d4ca, 4228, 92, eb, 47, ee, 3f, a9, 6e, ab)
A2_IID(ICoreWebView2Settings3, fdb5ab74, af33, 4854, 84, f0, 0a, 63, 1d, eb, 5e, ba)
A2_IID(ICoreWebView2_3, A0D6DF20, 3B92, 416D, AA, 0C, 43, 7A, 9C, 72, 78, 57)
A2_IID(ICoreWebView2WebMessageReceivedEventHandler, 57213f19, 00e6, 49fa, 8e, 07, 89, 8e, a0, 1e, cb, d2)
A2_IID(ICoreWebView2NavigationStartingEventHandler, 9adbe429, f36d, 432b, 9d, dc, f8, 88, 1f, bd, 76, e3)
A2_IID(ICoreWebView2NewWindowRequestedEventHandler, d4c185fe, c81c, 4989, 97, af, 2d, 3f, a7, ab, 56, 51)
A2_IID(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, 6c4819f3, c9b7, 4260, 81, 27, c9, f5, bd, e7, f6, 8c)
A2_IID(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, 4e8a3389, c9d8, 4bd2, b6, b5, 12, 4f, ee, 6c, c1, 4d)
