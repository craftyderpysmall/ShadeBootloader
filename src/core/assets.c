#include <string.h>
#include "assets.h"
#include "str.h"

const Asset *asset_find(const char *name)
{
    for (const Asset *a = g_assets; a->name; a++)
        if (str_eq_ci(a->name, name))
            return a;
    return NULL;
}
