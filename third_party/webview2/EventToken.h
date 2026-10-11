// mingw-w64 nie ma tego nagłówka z Windows SDK; WebView2.h potrzebuje tylko tej struktury.
#pragma once
#ifndef __eventtoken_h__
#define __eventtoken_h__
typedef struct EventRegistrationToken { __int64 value; } EventRegistrationToken;
#endif
