#include <string.h>
#include "pkgdet.h"

bool pkgdet_parse (const char *dump, char *out, size_t out_sz)
{
	if (out && out_sz)
		out [0] = 0;

	if (!dump || !out || !out_sz)
		return false;

	const char *p = strstr (dump, "topResumedActivity=ActivityRecord{");
	if (p)
		p += strlen ("topResumedActivity=ActivityRecord{");
	else
	{
		p = strstr (dump, "ResumedActivity: ActivityRecord{");
		if (!p)
			return false;
		p += strlen ("ResumedActivity: ActivityRecord{");
	}

	while (*p)
	{
		const char *sp = strchr (p, ' ');
		if (!sp)
			break;

		/* после поля "u<uid> " сразу идёт компонент "<пакет>/<активити>" */
		if ((sp [1] == 'u') && (sp [2] >= '0') && (sp [2] <= '9'))
		{
			const char *c = sp + 2;
			while ((*c >= '0') && (*c <= '9'))
				c++;
			if (*c == ' ')
				c++;

			const char *slash = strchr (c, '/');
			const char *end = slash ? slash : strpbrk (c, " }");
			if (end && (end > c) && ((size_t)(end - c) < out_sz))
			{
				memcpy (out, c, (size_t)(end - c));
				out [end - c] = 0;
				return true;
			}

			return false;
		}

		p = sp + 1;
	}

	return false;
}
