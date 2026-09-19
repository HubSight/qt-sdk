#pragma once

#include <QtCore/qglobal.h>

#if defined(HUBSIGHT_ADMIN_LIBRARY)
#define HUBSIGHT_ADMIN_EXPORT Q_DECL_EXPORT
#else
#define HUBSIGHT_ADMIN_EXPORT Q_DECL_IMPORT
#endif
