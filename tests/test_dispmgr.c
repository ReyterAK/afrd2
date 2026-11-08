#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../dispmgr.h"

int main (void)
{
	/* Строки ровно в том виде, в каком их отдаёт
	 * `dumpsys display | grep -o -E '...'` на стенде `.50` (Android 14). */
	static const char *dump =
		"mUserPreferredModeId=1\n"
		"DisplayModeRecord{mMode={id=1, width=1920, height=1080, fps=59.94006\n"
		"DisplayModeRecord{mMode={id=2, width=1920, height=1080, fps=60.000004\n"
		"DisplayModeRecord{mMode={id=8, width=1280, height=720, fps=50.0\n"
		"real 1920 x 1080\n";

	dispmgr_mode_t m;
	assert (dispmgr_preferred_parse (dump, &m) == 0);
	assert (m.id == 1);
	assert (m.width == 1920 && m.height == 1080);
	assert (m.hz == (int)(59.94006 * 256.0 + 0.5));	/* 15345 */

	/* Предпочтение не задано */
	assert (dispmgr_preferred_parse ("mUserPreferredModeId=-1\n", &m) == -1);

	/* preferred id есть, но записи о нём в списке нет */
	assert (dispmgr_preferred_parse (
		"mUserPreferredModeId=99\n"
		"DisplayModeRecord{mMode={id=1, width=1920, height=1080, fps=50.0\n",
		&m) == -1);

	/* Пусто/мусор */
	assert (dispmgr_preferred_parse ("", &m) == -1);
	assert (dispmgr_preferred_parse ("real 1920 x 1080\n", &m) == -1);

	return 0;
}
